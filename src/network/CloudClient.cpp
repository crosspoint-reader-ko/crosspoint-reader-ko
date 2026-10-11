#include "CloudClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* CONFIG_PATH = "/.crosspoint/cloud.json";

// The server pings every 5 s, so silence this long means the link is dead.
// Any inbound frame counts, data included: a device-side ping/pong heartbeat
// timed out during uploads because the server's pong queued behind the
// WRITE_DATA frames already in flight.
constexpr uint32_t RX_IDLE_TIMEOUT_MS = 30000;

// One global is unavoidable because WebSocketsClient's onEvent callback
// has no userdata pointer.
WebSocketsClient ws;
CloudClient* gInstance = nullptr;

struct ParsedUrl {
  bool ssl = false;
  std::string host;
  uint16_t port = 0;
  std::string path = "/";
  bool valid = false;
};

ParsedUrl parseUrl(const std::string& url) {
  ParsedUrl out;
  size_t i = 0;
  if (url.rfind("wss://", 0) == 0) {
    out.ssl = true;
    out.port = 443;
    i = 6;
  } else if (url.rfind("ws://", 0) == 0) {
    out.ssl = false;
    out.port = 80;
    i = 5;
  } else {
    return out;  // invalid scheme
  }
  size_t pathStart = url.find('/', i);
  std::string authority = pathStart == std::string::npos ? url.substr(i) : url.substr(i, pathStart - i);
  size_t colon = authority.find(':');
  if (colon != std::string::npos) {
    out.host = authority.substr(0, colon);
    out.port = static_cast<uint16_t>(strtoul(authority.c_str() + colon + 1, nullptr, 10));
  } else {
    out.host = authority;
  }
  if (pathStart != std::string::npos) out.path = url.substr(pathStart);
  out.valid = !out.host.empty() && out.port != 0;
  return out;
}

std::string toBaseHttpUrl(const std::string& wsUrl) {
  // wss://host[:port]/api/devices/ws → https://host[:port]
  auto u = parseUrl(wsUrl);
  if (!u.valid) return {};
  std::string scheme = u.ssl ? "https://" : "http://";
  char buf[256];
  if ((u.ssl && u.port == 443) || (!u.ssl && u.port == 80)) {
    snprintf(buf, sizeof(buf), "%s%s", scheme.c_str(), u.host.c_str());
  } else {
    snprintf(buf, sizeof(buf), "%s%s:%u", scheme.c_str(), u.host.c_str(), u.port);
  }
  return buf;
}

}  // namespace

CloudClient& CloudClient::getInstance() {
  static CloudClient inst;
  return inst;
}

void CloudClient::begin() {
  if (initialized) return;
  initialized = true;
  gInstance = this;

  hardwareId = deriveHardwareId();
  loadConfig();

  snprintf(pingSuffix, sizeof(pingSuffix), "cloud");
  cmdCtx.pingTagSuffix = pingSuffix;
  cmdCtx.sink = {.ctx = this, .send = &CloudClient::sinkSend};
}

bool CloudClient::start() {
  begin();
  if (!configured) return false;
  if (taskHandle) return true;
  stopRequested = false;
  // 8 KiB stack matches the WebServer task — diagnostic logs confirmed
  // SdFat's UTF-8 LFN paths fit comfortably here. The bigger stack we
  // tried earlier just stole heap from the WS RX buffer pool and made
  // OOM disconnects mid-upload more frequent.
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(&CloudClient::taskTrampoline, "CloudClient", 8192, this, 1, &handle) != pdPASS) {
    LOG_ERR("CLOUD", "task create failed, freeHeap=%u", ESP.getFreeHeap());
    return false;
  }
  taskHandle = handle;
  LOG_INF("CLOUD", "tunnel task started");
  return true;
}

void CloudClient::stop() {
  if (!taskHandle) return;
  stopRequested = true;
  // The task closes the socket and frees the upload state itself; wait for
  // it so the caller can tear WiFi down without a live client underneath.
  for (int i = 0; i < 300 && taskHandle; i++) {
    delay(10);
  }
  if (taskHandle) LOG_ERR("CLOUD", "tunnel task did not stop within 3 s");
}

void CloudClient::taskTrampoline(void* arg) {
  auto* self = static_cast<CloudClient*>(arg);
  self->taskLoop();
  self->taskHandle = nullptr;
  vTaskDelete(nullptr);
}

void CloudClient::taskLoop() {
  // Reserve enough headroom for the largest READ response: 4 byte length
  // header + MAX_READ_CHUNK payload. Pre-reserving avoids a vector grow on
  // every chunk, which would fragment the heap during a long download.
  cmdCtx.txScratch.reserve(8 + FileCommands::MAX_READ_CHUNK);

  ws.onEvent([](WStype_t type, uint8_t* payload, size_t length) {
    if (gInstance) gInstance->onWsEvent(static_cast<int>(type), payload, length);
  });
  ws.setReconnectInterval(5000);  // 5s between reconnect attempts
  ws.disableHeartbeat();

  while (!stopRequested) {
    // Wait for paired + WiFi before opening a connection.
    if (!configured || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    ParsedUrl url = parseUrl(serverUrl);
    if (!url.valid) {
      LOG_ERR("CLOUD", "Invalid server URL: %s", serverUrl.c_str());
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    // Auth lives in the URL query string, not Sec-WebSocket-Protocol. Two
    // reasons: (1) Railway's edge negotiates the subprotocol itself and only
    // forwards the first offered value to the origin, so a `cpr.v1, <creds>`
    // pair loses the credentials before the handler ever sees them; (2) HTTP
    // token grammar bans ':' in subprotocol values, so the MAC has to be
    // colon-free anyway. The query string passes through proxies unchanged
    // and accepts the full token alphabet (base64url + hex).
    std::string protoHwId;
    protoHwId.reserve(hardwareId.size());
    for (char c : hardwareId) {
      if (c != ':') protoHwId.push_back(c);
    }
    const char sep = url.path.find('?') == std::string::npos ? '?' : '&';
    char fullPath[256];
    // caps=ack: this firmware sends OP_WRITE_ACK, so the cloud may window uploads.
    snprintf(fullPath, sizeof(fullPath), "%s%chw=%s&t=%s&caps=ack", url.path.c_str(), sep, protoHwId.c_str(),
             token.c_str());
    const char* proto = "cpr.v1";

    if (url.ssl) {
      ws.beginSSL(url.host.c_str(), url.port, fullPath, "", proto);
    } else {
      ws.begin(url.host.c_str(), url.port, fullPath, proto);
    }
    LOG_INF("CLOUD", "Connecting to %s:%u%s (ssl=%d)", url.host.c_str(), url.port, url.path.c_str(), url.ssl ? 1 : 0);

    needsRestart = false;
    while (configured && !needsRestart && !stopRequested) {
      ws.loop();
      // Drive any in-flight OP_READ_BEGIN stream forward — the handler
      // sets up state on the first frame, this is what actually pushes
      // OP_READ_DATA frames out without bottlenecking on per-chunk
      // request/response RTT.
      FileCommands::pumpReadStream(cmdCtx);
      if (connected && millis() - lastRxMs > RX_IDLE_TIMEOUT_MS) {
        LOG_INF("CLOUD", "nothing received for %u ms, reconnecting", static_cast<unsigned>(millis() - lastRxMs));
        ws.disconnect();  // the lib reconnects after the reconnect interval
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }

    LOG_INF("CLOUD", "Tearing down connection (configured=%d needsRestart=%d stop=%d)", configured ? 1 : 0,
            needsRestart ? 1 : 0, stopRequested ? 1 : 0);
    ws.disconnect();
    connected = false;
  }
  FileCommands::reset(cmdCtx);
}

void CloudClient::onWsEvent(int type, uint8_t* payload, size_t length) {
  // Every inbound event is proof the link is alive (see RX_IDLE_TIMEOUT_MS).
  lastRxMs = millis();

  switch (type) {
    case WStype_CONNECTED:
      connected = true;
      connectedAtMs = millis();
      // Payload here is the path the server accepted — useful when debugging
      // path-rewriting proxies.
      LOG_INF("CLOUD", "WS connected url=%.*s", static_cast<int>(length),
              payload ? reinterpret_cast<const char*>(payload) : "");
      break;
    case WStype_DISCONNECTED: {
      uint32_t lifetime = connected && connectedAtMs ? (millis() - connectedAtMs) : 0;
      connected = false;
      connectedAtMs = 0;
      // The Links2004 lib forwards the close reason string as the payload,
      // e.g. "WebSocket handshake failed - HTTP 200" or "TCP connection
      // cleanup". Print it so we can tell handshake failures (server returns
      // wrong status) apart from network drops.
      LOG_INF("CLOUD", "WS disconnected (was connected for %u ms) reason=\"%.*s\"", static_cast<unsigned>(lifetime),
              static_cast<int>(length > 96 ? 96 : length), payload ? reinterpret_cast<const char*>(payload) : "");
      // Drop any in-flight upload state so the SD file handle and tx
      // scratch buffer don't leak across reconnect cycles. Without this,
      // every aborted upload pinned ~512 bytes of SdFat sector cache,
      // and after a few attempts MaxAlloc collapsed below the WS RX
      // buffer floor, causing the next transfer to die mid-flight.
      FileCommands::reset(cmdCtx);
      break;
    }
    case WStype_BIN:
      // [opcode][reqid LE][payload] — straight to FileCommands.
      FileCommands::dispatch(cmdCtx, payload, length);
      break;
    case WStype_TEXT:
      // Text frames aren't part of the binary protocol, but log the first
      // bytes — server may send a close-reason string before dropping us.
      LOG_INF("CLOUD", "WS text: %.*s", static_cast<int>(length > 96 ? 96 : length),
              payload ? reinterpret_cast<const char*>(payload) : "");
      break;
    case WStype_ERROR:
      LOG_ERR("CLOUD", "WS error: %.*s", static_cast<int>(length > 96 ? 96 : length),
              payload ? reinterpret_cast<const char*>(payload) : "");
      break;
    default:
      break;
  }
}

bool CloudClient::sinkSend(void* /*ctx*/, const uint8_t* data, size_t len) {
  // const_cast: WebSocketsClient takes non-const buffer but does not modify.
  // sendBIN returns false on transient send failure (TLS record alloc
  // failure, TCP buffer full + non-blocking, etc.) — surfacing it lets
  // the streaming pump retry on the next loop tick instead of advancing
  // its position on lost bytes.
  return ws.sendBIN(const_cast<uint8_t*>(data), len);
}

bool CloudClient::loadConfig() {
  // Always seed serverUrl from the compile-time default first; loadConfig
  // may then overwrite it from SD if the user set a custom URL.
  if (serverUrl.empty() && CROSSPOINT_CLOUD_DEFAULT_URL[0] != '\0') {
    serverUrl = CROSSPOINT_CLOUD_DEFAULT_URL;
  }

  HalFile f;
  if (!Storage.openFileForRead("CLOUD", CONFIG_PATH, f)) {
    LOG_INF("CLOUD", "no config at %s (using default url='%s')", CONFIG_PATH, serverUrl.c_str());
    return false;
  }
  String body = Storage.readFile(CONFIG_PATH);
  f.close();
  if (body.length() == 0) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("CLOUD", "config json parse failed");
    return false;
  }
  std::string savedUrl = doc["url"] | "";
  if (!savedUrl.empty()) serverUrl = savedUrl;
  // Deliberately ignore the saved hardwareId. The hwId is intrinsic to the
  // device (eFuse MAC) and `begin()` already populated it from esp_read_mac.
  // Older configs persisted from before the eFuse fix had the all-zero MAC
  // because WiFi.macAddress() was queried pre-WiFi-init — overwriting here
  // would resurrect that bug after every reboot.
  token = doc["token"] | "";
  deviceId = doc["deviceId"] | "";
  configured = !serverUrl.empty() && !token.empty() && !hardwareId.empty();
  if (configured) {
    LOG_INF("CLOUD", "loaded config: url=%s deviceId=%s", serverUrl.c_str(), deviceId.c_str());
  }
  return configured;
}

bool CloudClient::saveConfig(const std::string& url, const std::string& hwId, const std::string& tok,
                             const std::string& devId) {
  Storage.ensureDirectoryExists("/.crosspoint");
  JsonDocument doc;
  doc["url"] = url;
  doc["hardwareId"] = hwId;
  doc["token"] = tok;
  doc["deviceId"] = devId;
  String out;
  serializeJson(doc, out);
  return Storage.writeFile(CONFIG_PATH, out);
}

std::string CloudClient::deriveHardwareId() {
  // Read the factory MAC straight from eFuse — WiFi.macAddress() returns all
  // zeros until WiFi.begin() runs, but begin() is called from setup() before
  // the WiFi stack is up (the connect step waits for WiFi internally). Using
  // esp_read_mac() means the hwId is stable from the very first call.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

bool CloudClient::pairDevice(const std::string& baseHttpUrl, const std::string& code, const std::string& deviceName) {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLOUD", "pair: WiFi not connected");
    return false;
  }
  if (hardwareId.empty()) hardwareId = deriveHardwareId();

  bool ssl = baseHttpUrl.rfind("https://", 0) == 0;
  std::string url = baseHttpUrl + "/api/devices/pair";

  // Pull host:port out of the base URL so we can pre-flight a raw TCP/TLS
  // connect — HTTPClient swallows mbedTLS errors and reports them all as -1.
  std::string host;
  uint16_t port = ssl ? 443 : 80;
  {
    size_t schemeLen = ssl ? 8 : 7;
    if (baseHttpUrl.size() <= schemeLen) {
      LOG_ERR("CLOUD", "pair: malformed url '%s'", baseHttpUrl.c_str());
      return false;
    }
    std::string rest = baseHttpUrl.substr(schemeLen);
    size_t colon = rest.find(':');
    size_t slash = rest.find('/');
    size_t end =
        std::min(colon == std::string::npos ? rest.size() : colon, slash == std::string::npos ? rest.size() : slash);
    host = rest.substr(0, end);
    if (colon != std::string::npos && colon < slash) {
      port = static_cast<uint16_t>(strtoul(rest.c_str() + colon + 1, nullptr, 10));
    }
  }

  LOG_INF("CLOUD", "pair: target=%s:%u ssl=%d freeHeap=%u maxAlloc=%u", host.c_str(), port, ssl ? 1 : 0,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  HTTPClient http;
  WiFiClientSecure secureClient;

  bool ok;
  if (ssl) {
    secureClient.setInsecure();  // pairing relies on the token, not on cert pinning
    secureClient.setHandshakeTimeout(15);
    // Read timeout must absorb server-side DB cold-start retries (~30s
    // worst case on Railway hibernated Postgres).
    secureClient.setTimeout(45);

    // Pre-flight: explicit TLS connect so we surface the actual mbedTLS
    // error if the handshake fails. HTTPClient just returns -1 either way.
    if (!secureClient.connect(host.c_str(), port)) {
      char errBuf[128] = {0};
      int errCode = secureClient.lastError(errBuf, sizeof(errBuf));
      LOG_ERR("CLOUD", "pair: TLS connect failed code=%d hex=-0x%04x (%s) freeHeap=%u", errCode,
              errCode < 0 ? -errCode : errCode, errBuf, ESP.getFreeHeap());
      return false;
    }
    LOG_INF("CLOUD", "pair: TLS connected, reusing socket for HTTP request");
    ok = http.begin(secureClient, url.c_str());
  } else {
    ok = http.begin(url.c_str());
  }
  if (!ok) {
    LOG_ERR("CLOUD", "pair: http begin failed");
    return false;
  }
  // Long timeout to absorb Postgres cold-start retries on the server (Railway
  // hibernates the DB after idle; pair route retries up to ~15s on its end).
  http.setTimeout(40000);
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["code"] = code;
  req["hardwareId"] = hardwareId;
  if (!deviceName.empty()) req["deviceName"] = deviceName;
  req["firmwareVersion"] = CROSSPOINT_VERSION;
  String body;
  serializeJson(req, body);

  int status = http.POST(body);
  if (status != 200) {
    // Pull the body too — server returns {error, code, message} on 4xx/5xx,
    // which makes diagnosing a missing migration / FK violation possible
    // without grepping cloud logs.
    String resp = http.getString();
    LOG_ERR("CLOUD", "pair: HTTP %d (errno=%s) body=%s", status, http.errorToString(status).c_str(),
            resp.length() ? resp.c_str() : "<empty>");
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  JsonDocument res;
  if (deserializeJson(res, resp)) {
    LOG_ERR("CLOUD", "pair: bad response json");
    return false;
  }
  std::string newDeviceId = res["deviceId"] | "";
  std::string newToken = res["token"] | "";
  if (newDeviceId.empty() || newToken.empty()) {
    LOG_ERR("CLOUD", "pair: response missing fields");
    return false;
  }

  // Default the WS URL to the base host's wss endpoint.
  std::string wsUrl;
  if (ssl) {
    wsUrl = "wss://" + baseHttpUrl.substr(8) + "/api/devices/ws";
  } else {
    wsUrl = "ws://" + baseHttpUrl.substr(7) + "/api/devices/ws";
  }
  if (!saveConfig(wsUrl, hardwareId, newToken, newDeviceId)) {
    LOG_ERR("CLOUD", "pair: failed to save config");
    return false;
  }
  serverUrl = wsUrl;
  token = newToken;
  deviceId = newDeviceId;
  configured = true;

  needsRestart = true;  // task will reconnect with new credentials
  LOG_INF("CLOUD", "paired: deviceId=%s", deviceId.c_str());
  return true;
}

bool CloudClient::setServerUrl(const std::string& wsUrl) {
  ParsedUrl u = parseUrl(wsUrl);
  if (!u.valid) {
    LOG_ERR("CLOUD", "setServerUrl: invalid url '%s'", wsUrl.c_str());
    return false;
  }
  if (hardwareId.empty()) hardwareId = deriveHardwareId();
  // Persist, preserving existing token/deviceId if any.
  if (!saveConfig(wsUrl, hardwareId, token, deviceId)) {
    LOG_ERR("CLOUD", "setServerUrl: save failed");
    return false;
  }
  serverUrl = wsUrl;
  configured = !token.empty();  // still need a token to actually connect
  needsRestart = true;
  LOG_INF("CLOUD", "server url set to %s (paired=%d)", wsUrl.c_str(), configured ? 1 : 0);
  return true;
}

bool CloudClient::pairWithCode(const std::string& code, const std::string& deviceName) {
  if (serverUrl.empty()) {
    LOG_ERR("CLOUD", "pairWithCode: server URL not set");
    return false;
  }
  std::string baseHttp = toBaseHttpUrl(serverUrl);
  if (baseHttp.empty()) {
    LOG_ERR("CLOUD", "pairWithCode: cannot derive http URL from '%s'", serverUrl.c_str());
    return false;
  }
  return pairDevice(baseHttp, code, deviceName);
}

bool CloudClient::forgetDevice() {
  // Drop runtime state so the task loop tears down the live socket.
  token.clear();
  deviceId.clear();
  configured = false;
  needsRestart = true;

  if (Storage.exists(CONFIG_PATH)) {
    if (!Storage.remove(CONFIG_PATH)) {
      LOG_ERR("CLOUD", "forgetDevice: failed to remove %s", CONFIG_PATH);
      return false;
    }
  }
  LOG_INF("CLOUD", "device unpaired");
  return true;
}
