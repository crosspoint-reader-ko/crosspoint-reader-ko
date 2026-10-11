#include "FileCommands.h"

#include <Arduino.h>
#include <Logging.h>
#include <common/FsApiConstants.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace FileCommands {
namespace {

inline uint16_t readU16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }

inline uint32_t readU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t readU64LE(const uint8_t* p) {
  uint64_t lo = readU32LE(p);
  uint64_t hi = readU32LE(p + 4);
  return lo | (hi << 32);
}

inline void writeU16LE(uint8_t* p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

inline void writeU32LE(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

inline void writeU64LE(uint8_t* p, uint64_t v) {
  writeU32LE(p, v & 0xFFFFFFFFu);
  writeU32LE(p + 4, (v >> 32) & 0xFFFFFFFFu);
}

bool popLString(const uint8_t*& p, size_t& remaining, std::string& out) {
  if (remaining < 2) return false;
  uint16_t len = readU16LE(p);
  p += 2;
  remaining -= 2;
  if (remaining < len) return false;
  out.assign(reinterpret_cast<const char*>(p), len);
  p += len;
  remaining -= len;
  return true;
}

void sendOk(Context& ctx, uint8_t opcode, uint16_t reqid, const uint8_t* payload, size_t len) {
  std::vector<uint8_t> frame;
  frame.reserve(3 + len);
  frame.push_back(opcode | OP_RESPONSE_BIT);
  frame.push_back(reqid & 0xFF);
  frame.push_back((reqid >> 8) & 0xFF);
  if (len > 0 && payload != nullptr) {
    frame.insert(frame.end(), payload, payload + len);
  }
  if (ctx.sink.send) ctx.sink.send(ctx.sink.ctx, frame.data(), frame.size());
}

void sendError(Context& ctx, uint16_t reqid, uint8_t code, const char* msg) {
  size_t mlen = msg ? strlen(msg) : 0;
  if (mlen > 255) mlen = 255;
  std::vector<uint8_t> frame;
  frame.reserve(3 + 1 + 2 + mlen);
  frame.push_back(OP_ERROR);
  frame.push_back(reqid & 0xFF);
  frame.push_back((reqid >> 8) & 0xFF);
  frame.push_back(code);
  frame.push_back(mlen & 0xFF);
  frame.push_back((mlen >> 8) & 0xFF);
  if (mlen > 0) frame.insert(frame.end(), msg, msg + mlen);
  if (ctx.sink.send) ctx.sink.send(ctx.sink.ctx, frame.data(), frame.size());
}

// Unsolicited OP_WRITE_ACK; see FileCommands.h. ackedSize only moves when the
// sink took the frame, so a refused send is retried on the next data frame.
void sendWriteAck(Context& ctx) {
  uint8_t frame[3 + 2 + 8 + 1];
  frame[0] = OP_WRITE_ACK | OP_RESPONSE_BIT;
  frame[1] = 0;
  frame[2] = 0;
  writeU16LE(frame + 3, ctx.writeSession.id);
  writeU64LE(frame + 5, ctx.writeSession.writtenSize);
  frame[13] = ctx.writeSession.failed ? 1 : 0;
  if (ctx.sink.send && ctx.sink.send(ctx.sink.ctx, frame, sizeof(frame))) {
    ctx.writeSession.ackedSize = ctx.writeSession.writtenSize;
  }
}

void abortActiveWrite(Context& ctx) {
  if (ctx.writeSession.active) {
    ctx.writeSession.file.close();
    ctx.writeSession.active = false;
    ctx.writeSession.id = 0;
  }
}

void handlePing(Context& ctx, uint16_t reqid) {
  char tag[80];
  const int n = snprintf(tag, sizeof(tag), "crosspoint-reader%s%s", ctx.pingTagSuffix ? " " : "",
                         ctx.pingTagSuffix ? ctx.pingTagSuffix : "");
  const size_t tagLen = n > 0 ? static_cast<size_t>(n) : 0;
  ctx.txScratch.resize(4 + tagLen + 4);
  writeU32LE(ctx.txScratch.data(), tagLen);
  memcpy(ctx.txScratch.data() + 4, tag, tagLen);
  writeU32LE(ctx.txScratch.data() + 4 + tagLen, ESP.getFreeHeap());
  sendOk(ctx, OP_PING, reqid, ctx.txScratch.data(), ctx.txScratch.size());
}

void handleListDir(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad list_dir args");
    return;
  }
  if (path.empty()) path = "/";

  HalFile root = Storage.open(path.c_str());
  if (!root) {
    sendError(ctx, reqid, ERR_NOT_FOUND, "directory not found");
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    sendError(ctx, reqid, ERR_NOT_DIR, "not a directory");
    return;
  }

  ctx.txScratch.clear();
  ctx.txScratch.resize(2);
  uint16_t count = 0;

  HalFile child = root.openNextFile();
  char nameBuf[256];
  while (child) {
    size_t nameLen = child.getName(nameBuf, sizeof(nameBuf));
    if (nameLen > 0 && nameLen <= 255) {
      bool isDir = child.isDirectory();
      uint64_t size = isDir ? 0 : child.size();
      size_t off = ctx.txScratch.size();
      ctx.txScratch.resize(off + 1 + 8 + 2 + nameLen);
      ctx.txScratch[off] = isDir ? 1 : 0;
      writeU64LE(ctx.txScratch.data() + off + 1, size);
      writeU16LE(ctx.txScratch.data() + off + 9, static_cast<uint16_t>(nameLen));
      memcpy(ctx.txScratch.data() + off + 11, nameBuf, nameLen);
      ++count;
    }
    child.close();
    child = root.openNextFile();
  }
  root.close();

  writeU16LE(ctx.txScratch.data(), count);
  sendOk(ctx, OP_LIST_DIR, reqid, ctx.txScratch.data(), ctx.txScratch.size());
}

void handleStat(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad stat args");
    return;
  }
  HalFile f = Storage.open(path.c_str());
  if (!f) {
    sendError(ctx, reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  uint8_t type = f.isDirectory() ? 1 : 0;
  uint64_t size = type == 1 ? 0 : f.size();
  f.close();

  uint8_t buf[1 + 8];
  buf[0] = type;
  writeU64LE(buf + 1, size);
  sendOk(ctx, OP_STAT, reqid, buf, sizeof(buf));
}

void handleRead(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad read args");
    return;
  }
  if (n < 12) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "read missing offset/length");
    return;
  }
  uint64_t offset = readU64LE(p);
  uint32_t length = readU32LE(p + 8);
  if (length > MAX_READ_CHUNK) length = MAX_READ_CHUNK;

  HalFile f = Storage.open(path.c_str());
  if (!f) {
    sendError(ctx, reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  if (f.isDirectory()) {
    f.close();
    sendError(ctx, reqid, ERR_IS_DIR, "is a directory");
    return;
  }
  uint64_t total = f.size();
  if (offset > total) offset = total;
  if (offset + length > total) length = static_cast<uint32_t>(total - offset);

  if (!f.seekSet(static_cast<size_t>(offset))) {
    f.close();
    sendError(ctx, reqid, ERR_IO, "seek failed");
    return;
  }

  ctx.txScratch.resize(4 + length);
  writeU32LE(ctx.txScratch.data(), length);
  if (length > 0) {
    int got = f.read(ctx.txScratch.data() + 4, length);
    if (got < 0) {
      f.close();
      sendError(ctx, reqid, ERR_IO, "read failed");
      return;
    }
    ctx.txScratch.resize(4 + static_cast<size_t>(got));
    writeU32LE(ctx.txScratch.data(), static_cast<uint32_t>(got));
  }
  f.close();
  sendOk(ctx, OP_READ, reqid, ctx.txScratch.data(), ctx.txScratch.size());
}

void handleWriteBegin(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  const size_t origN = n;
  std::string path;
  if (!popLString(p, n, path)) {
    char msg[64];
    snprintf(msg, sizeof(msg), "bad write_begin args (got %u bytes)", static_cast<unsigned>(origN));
    sendError(ctx, reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  if (n < 8) {
    char msg[96];
    snprintf(msg, sizeof(msg), "missing total_size: payload %u, path '%s'(%u), left %u", static_cast<unsigned>(origN),
             path.c_str(), static_cast<unsigned>(path.size()), static_cast<unsigned>(n));
    sendError(ctx, reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  uint64_t totalSize = readU64LE(p);

  if (ctx.writeSession.active) abortActiveWrite(ctx);

  HalFile f = Storage.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  if (!f) {
    sendError(ctx, reqid, ERR_IO, "cannot open for write");
    return;
  }

  ctx.writeSession.active = true;
  ctx.writeSession.id = ctx.nextSessionId++;
  if (ctx.nextSessionId == 0) ctx.nextSessionId = 1;
  ctx.writeSession.totalSize = totalSize;
  ctx.writeSession.writtenSize = 0;
  ctx.writeSession.ackedSize = 0;
  ctx.writeSession.file = std::move(f);

  uint8_t buf[2];
  writeU16LE(buf, ctx.writeSession.id);
  sendOk(ctx, OP_WRITE_BEGIN, reqid, buf, sizeof(buf));
}

void handleWriteChunk(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  if (n < 6) {
    char msg[64];
    snprintf(msg, sizeof(msg), "bad write_chunk args (got %u bytes)", static_cast<unsigned>(n));
    sendError(ctx, reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  uint16_t sid = readU16LE(p);
  uint32_t chunkLen = readU32LE(p + 2);
  if (n < 6u + chunkLen) {
    char msg[96];
    snprintf(msg, sizeof(msg), "truncated: claimed %u payload %u expected %u", static_cast<unsigned>(chunkLen),
             static_cast<unsigned>(n), static_cast<unsigned>(6u + chunkLen));
    sendError(ctx, reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  if (chunkLen > MAX_WRITE_CHUNK) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "chunk too large");
    return;
  }
  if (!ctx.writeSession.active || ctx.writeSession.id != sid) {
    sendError(ctx, reqid, ERR_NO_SESSION, "no matching session");
    return;
  }

  size_t written = ctx.writeSession.file.write(p + 6, chunkLen);
  if (written != chunkLen) {
    abortActiveWrite(ctx);
    sendError(ctx, reqid, ERR_IO, "short write (disk full?)");
    return;
  }
  ctx.writeSession.writtenSize += chunkLen;

  uint8_t buf[8];
  writeU64LE(buf, ctx.writeSession.writtenSize);
  sendOk(ctx, OP_WRITE_CHUNK, reqid, buf, sizeof(buf));
}

void handleWriteEnd(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  if (n < 2) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad write_end args");
    return;
  }
  uint16_t sid = readU16LE(p);
  if (!ctx.writeSession.active || ctx.writeSession.id != sid) {
    sendError(ctx, reqid, ERR_NO_SESSION, "no matching session");
    return;
  }
  const bool failed = ctx.writeSession.failed;
  ctx.writeSession.file.flush();
  ctx.writeSession.file.close();
  ctx.writeSession.active = false;
  ctx.writeSession.id = 0;
  ctx.writeSession.failed = false;
  if (failed) {
    sendError(ctx, reqid, ERR_IO, "stream had write failure");
    return;
  }
  sendOk(ctx, OP_WRITE_END, reqid, nullptr, 0);
}

// Fire-and-forget chunk for streaming uploads. Same wire layout as
// OP_WRITE_CHUNK minus the per-chunk response — caller batches many of
// these between OP_WRITE_BEGIN and OP_WRITE_END, and gets a final pass/
// fail at OP_WRITE_END time. Removing the round-trip is what closes the
// throughput gap with the firmware's WiFi web server.
void handleWriteData(Context& ctx, const uint8_t* p, size_t n) {
  if (n < 2) return;  // no reqid to respond on; just drop malformed
  uint16_t sid = readU16LE(p);
  if (!ctx.writeSession.active || ctx.writeSession.id != sid) return;
  if (ctx.writeSession.failed) return;  // already in failed state
  if (n < 2u) return;
  const uint8_t* data = p + 2;
  const size_t dataLen = n - 2;
  if (dataLen == 0) return;
  size_t written = ctx.writeSession.file.write(data, dataLen);
  if (written != dataLen) {
    ctx.writeSession.failed = true;
    sendWriteAck(ctx);  // tell the cloud now instead of at OP_WRITE_END
    return;
  }
  ctx.writeSession.writtenSize += dataLen;
  if (ctx.writeSession.writtenSize - ctx.writeSession.ackedSize >= WRITE_ACK_EVERY ||
      ctx.writeSession.writtenSize == ctx.writeSession.totalSize) {
    sendWriteAck(ctx);
  }
}

void handleReadBegin(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad read_begin args");
    return;
  }
  if (ctx.readStream.active) {
    // Drop any prior stream — the new request wins.
    ctx.readStream.file.close();
    ctx.readStream.active = false;
  }
  HalFile f = Storage.open(path.c_str(), O_RDONLY);
  if (!f) {
    sendError(ctx, reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  if (f.isDirectory()) {
    f.close();
    sendError(ctx, reqid, ERR_IS_DIR, "is a directory");
    return;
  }
  const uint64_t size = f.size();
  uint16_t sid = ctx.nextSessionId++;
  if (ctx.nextSessionId == 0) ctx.nextSessionId = 1;
  ctx.readStream.active = true;
  ctx.readStream.id = sid;
  ctx.readStream.reqid = reqid;
  ctx.readStream.totalSize = size;
  ctx.readStream.sentSize = 0;
  ctx.readStream.file = std::move(f);

  uint8_t resp[2 + 8];
  writeU16LE(resp, sid);
  writeU64LE(resp + 2, size);
  sendOk(ctx, OP_READ_BEGIN, reqid, resp, sizeof(resp));
  // Subsequent OP_READ_DATA frames are emitted by pumpReadStream() from
  // the transport's main loop — keeps the WS RX loop unblocked.
}

void handleDelete(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad delete args");
    return;
  }
  HalFile probe = Storage.open(path.c_str());
  if (!probe) {
    sendError(ctx, reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  bool isDir = probe.isDirectory();
  probe.close();

  bool ok = isDir ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str());
  if (!ok) {
    sendError(ctx, reqid, ERR_IO, "delete failed");
    return;
  }
  sendOk(ctx, OP_DELETE, reqid, nullptr, 0);
}

void handleMkdir(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad mkdir args");
    return;
  }
  if (!Storage.mkdir(path.c_str(), true)) {
    sendError(ctx, reqid, ERR_IO, "mkdir failed");
    return;
  }
  sendOk(ctx, OP_MKDIR, reqid, nullptr, 0);
}

void handleRename(Context& ctx, uint16_t reqid, const uint8_t* p, size_t n) {
  std::string src;
  std::string dst;
  if (!popLString(p, n, src) || !popLString(p, n, dst)) {
    sendError(ctx, reqid, ERR_BAD_REQUEST, "bad rename args");
    return;
  }
  if (!Storage.rename(src.c_str(), dst.c_str())) {
    sendError(ctx, reqid, ERR_IO, "rename failed");
    return;
  }
  sendOk(ctx, OP_RENAME, reqid, nullptr, 0);
}

}  // namespace

void dispatch(Context& ctx, const uint8_t* frame, size_t frameLen) {
  if (frameLen < 3) return;  // need opcode + reqid
  const uint8_t opcode = frame[0];
  const uint16_t reqid = readU16LE(frame + 1);
  const uint8_t* payload = frame + 3;
  const size_t plen = frameLen - 3;

  switch (opcode) {
    case OP_PING:
      handlePing(ctx, reqid);
      break;
    case OP_LIST_DIR:
      handleListDir(ctx, reqid, payload, plen);
      break;
    case OP_STAT:
      handleStat(ctx, reqid, payload, plen);
      break;
    case OP_READ:
      handleRead(ctx, reqid, payload, plen);
      break;
    case OP_WRITE_BEGIN:
      handleWriteBegin(ctx, reqid, payload, plen);
      break;
    case OP_WRITE_CHUNK:
      handleWriteChunk(ctx, reqid, payload, plen);
      break;
    case OP_WRITE_END:
      handleWriteEnd(ctx, reqid, payload, plen);
      break;
    case OP_DELETE:
      handleDelete(ctx, reqid, payload, plen);
      break;
    case OP_MKDIR:
      handleMkdir(ctx, reqid, payload, plen);
      break;
    case OP_RENAME:
      handleRename(ctx, reqid, payload, plen);
      break;
    case OP_WRITE_DATA:
      handleWriteData(ctx, payload, plen);
      break;
    case OP_READ_BEGIN:
      handleReadBegin(ctx, reqid, payload, plen);
      break;
    default:
      sendError(ctx, reqid, ERR_BAD_REQUEST, "unknown opcode");
      break;
  }
}

void reset(Context& ctx) {
  if (ctx.writeSession.active) {
    ctx.writeSession.file.close();
    ctx.writeSession.active = false;
    ctx.writeSession.id = 0;
    ctx.writeSession.failed = false;
  }
  if (ctx.readStream.active) {
    ctx.readStream.file.close();
    ctx.readStream.active = false;
    ctx.readStream.id = 0;
  }
  ctx.txScratch.clear();
  ctx.txScratch.shrink_to_fit();
}

void pumpReadStream(Context& ctx) {
  if (!ctx.readStream.active) return;
  // Reuse a single send-side scratch buffer per stream rather than
  // allocating per chunk — this is the same trick the firmware's HTTP
  // server uses for its 8 KiB upload buffer, and it keeps cloud-relayed
  // downloads off the per-frame heap-alloc treadmill.
  const uint64_t remaining = ctx.readStream.totalSize - ctx.readStream.sentSize;
  if (remaining == 0) {
    uint8_t end[2];
    writeU16LE(end, ctx.readStream.id);
    // Send unsolicited OP_READ_END echoing the original reqid so the
    // cloud relay knows which HTTP response to close.
    std::vector<uint8_t> frame;
    frame.reserve(3 + 2);
    frame.push_back(OP_READ_END | OP_RESPONSE_BIT);
    frame.push_back(ctx.readStream.reqid & 0xFF);
    frame.push_back((ctx.readStream.reqid >> 8) & 0xFF);
    frame.push_back(end[0]);
    frame.push_back(end[1]);
    if (ctx.sink.send) {
      // Don't tear down the stream until END is on the wire — if the
      // sink can't take it right now, leave state in place and retry on
      // the next pump tick.
      if (!ctx.sink.send(ctx.sink.ctx, frame.data(), frame.size())) return;
    }

    ctx.readStream.file.close();
    ctx.readStream.active = false;
    ctx.readStream.id = 0;
    return;
  }

  size_t want = remaining > MAX_READ_CHUNK ? MAX_READ_CHUNK : static_cast<size_t>(remaining);
  ctx.txScratch.resize(3 + 2 + want);
  ctx.txScratch[0] = OP_READ_DATA | OP_RESPONSE_BIT;
  ctx.txScratch[1] = ctx.readStream.reqid & 0xFF;
  ctx.txScratch[2] = (ctx.readStream.reqid >> 8) & 0xFF;
  writeU16LE(ctx.txScratch.data() + 3, ctx.readStream.id);
  // Re-read on every retry: the file's seek position only advances
  // after a successful send below, so this stays idempotent even when
  // we bail out partway through.
  const uint32_t seekTarget = static_cast<uint32_t>(ctx.readStream.sentSize);
  if (!ctx.readStream.file.seekSet(seekTarget)) {
    ctx.readStream.file.close();
    ctx.readStream.active = false;
    sendError(ctx, ctx.readStream.reqid, ERR_IO, "seek failed");
    return;
  }
  int got = ctx.readStream.file.read(ctx.txScratch.data() + 3 + 2, want);
  if (got <= 0) {
    // Read failure mid-stream: surface as OP_ERROR and stop streaming.
    ctx.readStream.file.close();
    ctx.readStream.active = false;
    sendError(ctx, ctx.readStream.reqid, ERR_IO, "read failed");
    return;
  }
  ctx.txScratch.resize(3 + 2 + static_cast<size_t>(got));
  if (ctx.sink.send) {
    // Honour the sink's success bit: a transient false (TLS-record
    // alloc failed because the device is heap-fragmented under SD-load)
    // would otherwise show up as a silently truncated download.
    if (!ctx.sink.send(ctx.sink.ctx, ctx.txScratch.data(), ctx.txScratch.size())) return;
  }
  ctx.readStream.sentSize += static_cast<uint64_t>(got);
}

}  // namespace FileCommands
