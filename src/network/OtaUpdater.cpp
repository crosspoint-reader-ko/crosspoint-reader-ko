#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include "FirmwareFlasher.h"
#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_wifi.h>
// clang-format on

#include <cstdio>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"

namespace {
// Korean fork release URL
constexpr char latestReleaseUrl[] =
    "https://api.github.com/repos/crosspoint-reader-ko/crosspoint-reader-ko/releases/latest";
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  ReleaseJsonParser releaseParser;
  releaseParser.setFirmwareAssetName("");
  // Each board updates from crosspoint-<version>-<device>.bin. The combined
  // C3 image uses x3-x4; other asset suffixes match their firmware board tag.
  // Korean releases publish the X3/X4 image under both this name and the legacy
  // firmware.bin (for 1.5.x-ko devices); other boards have no Korean asset and
  // simply report no update.
  const bool isX4 = board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0;
  char assetSuffix[20] = "-x3-x4";
  if (!isX4) {
    snprintf(assetSuffix, sizeof(assetSuffix), "-%.*s", static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
  }
  char assetName[48] = {};
  bool assetNameSet = false;
  const bool ok = HttpDownloader::fetchUrl(latestReleaseUrl, [&](const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (!assetNameSet && offset < len) {
      releaseParser.feed(reinterpret_cast<const char*>(data + offset), 1);
      offset++;
      if (releaseParser.foundTag()) {
        snprintf(assetName, sizeof(assetName), "crosspoint-%s%s.bin", releaseParser.getTagName(), assetSuffix);
        releaseParser.setFirmwareAssetName(assetName);
        assetNameSet = true;
      }
    }
    if (offset < len) releaseParser.feed(reinterpret_cast<const char*>(data + offset), len - offset);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_INF("OTA", "No %s asset in latest release", assetName);
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor, currentMinor, currentPatch;
  int latestMajor, latestMinor, latestPatch;

  const auto currentVersion = CROSSPOINT_VERSION;

  // semantic version check (only match on 3 segments)
  sscanf(latestVersion.c_str(), "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

bool OtaUpdater::isUpdateNewerKO() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  // Parse version: major.minor.patch-ko.koVersion
  auto parseVersion = [](const std::string& version, int& major, int& minor, int& patch, int& ko) {
    major = minor = patch = ko = 0;

    // Find -ko. suffix
    size_t koPos = version.find("-ko.");
    std::string baseVersion = (koPos != std::string::npos) ? version.substr(0, koPos) : version;

    // Parse ko version if present
    if (koPos != std::string::npos) {
      ko = stoi(version.substr(koPos + 4));
    }

    // Parse major.minor.patch
    size_t firstDot = baseVersion.find('.');
    size_t lastDot = baseVersion.find_last_of('.');

    if (firstDot != std::string::npos) {
      major = stoi(baseVersion.substr(0, firstDot));
      if (lastDot != firstDot) {
        minor = stoi(baseVersion.substr(firstDot + 1, lastDot - firstDot - 1));
        patch = stoi(baseVersion.substr(lastDot + 1));
      } else {
        minor = stoi(baseVersion.substr(firstDot + 1));
      }
    }
  };

  int updateMajor, updateMinor, updatePatch, updateKo;
  int currentMajor, currentMinor, currentPatch, currentKo;

  parseVersion(latestVersion, updateMajor, updateMinor, updatePatch, updateKo);
  parseVersion(CROSSPOINT_VERSION, currentMajor, currentMinor, currentPatch, currentKo);

  if (updateMajor != currentMajor) return updateMajor > currentMajor;
  if (updateMinor != currentMinor) return updateMinor > currentMinor;
  if (updatePatch != currentPatch) return updatePatch > currentPatch;
  return updateKo > currentKo;
}

namespace {
constexpr const char* kOtaSdPath = "/.crosspoint/ota_firmware.bin";

// Stash the activity-side progress callback so the firmware-flasher's free
// progress callback can fan back into it (we need a void* ctx hop).
struct FlashCtx {
  OtaUpdater* updater;
  OtaUpdater::ProgressCallback onProgress;
  void* userCtx;
};

}  // namespace

void OtaUpdater::setExpectedSize(size_t s) {
  totalSize = s;
  render = true;
}
void OtaUpdater::setProcessed(size_t s) {
  processedSize = s;
  render = true;
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  lastError.clear();
  if (!isUpdateNewerKO()) {
    lastError = "not_newer";
    return UPDATE_OLDER_ERROR;
  }

  // Two-phase: (1) stream firmware.bin to SD, (2) flash that file with
  // firmware_flash::flashFromSdPath. Skipping esp_https_ota_* avoids the running
  // ESP-IDF's bogus esp_image_verify efuse-blk-rev rejection on X4 silicon, and the
  // cached SD file lets the user retry the SD update flow if anything dies mid-flash.
  //
  // The download goes through HttpDownloader, not esp_http_client: the precompiled
  // mbedTLS in this package has TLS 1.3 stubbed out (see freeink-sdk SecureClient.h),
  // so esp_http_client cannot reach the release CDN and fails the connect outright.
  // HttpDownloader runs on wolfSSL when FREEINK_NET_WOLFSSL is set and handles the
  // GitHub -> CDN redirect hop itself.
  phase = Phase::Downloading;
  totalSize = otaSize;  // GitHub release asset size — pre-populated from checkForUpdate
  processedSize = 0;
  render = true;
  if (onProgress) onProgress(ctx);

  Storage.mkdir("/.crosspoint", true);

  HalFile sdFile;
  if (!Storage.openFileForWrite("OTA", kOtaSdPath, sdFile) || !sdFile) {
    LOG_ERR("OTA", "open SD cache for write failed: %s", kOtaSdPath);
    lastError = "sd_open";
    return INTERNAL_UPDATE_ERROR;
  }

  size_t written = 0;
  bool writeFailed = false;
  // Upstream wrong-device guard, applied while streaming so a wrong image is abandoned
  // early: buffer the first 14 header bytes to read chip_id (esp_image_header_t offset 12),
  // and scan for an embedded board tag naming another board (FirmwareBoardTag.h).
  // flashFromSdPath() re-validates both before touching flash.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  board_tag::Scanner tagScanner;

  esp_wifi_set_ps(WIFI_PS_NONE);
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, const size_t len) {
    if (len == 0) return true;
    if (hdrLen < sizeof(hdr)) {
      const size_t take = len < sizeof(hdr) - hdrLen ? len : sizeof(hdr) - hdrLen;
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", tagScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      return false;  // abort the transfer
    }
    const size_t wrote = sdFile.write(data, len);
    if (wrote != len) {
      LOG_ERR("OTA", "SD write short @%u (got=%u want=%u)", static_cast<unsigned>(written),
              static_cast<unsigned>(wrote), static_cast<unsigned>(len));
      writeFailed = true;
      lastError = "sd_write";
      return false;  // abort the transfer
    }
    written += len;
    setProcessed(written);
    if (onProgress) onProgress(ctx);
    return true;
  });
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  // Explicit close required before flashFromSdPath re-opens the same path for read.
  sdFile.close();

  if (wrongChip || tagScanner.mismatch()) {
    // Abort before anything reaches flash; drop the partial SD cache so the SD update
    // screen never offers it.
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    Storage.remove(kOtaSdPath);
    lastError = "wrong_device";
    return WRONG_DEVICE_ERROR;
  }
  if (writeFailed) {
    return INTERNAL_UPDATE_ERROR;  // lastError already set
  }
  if (!fetchOk) {
    LOG_ERR("OTA", "firmware download failed after %u bytes", static_cast<unsigned>(written));
    lastError = "http_fetch";
    return HTTP_ERROR;
  }
  if (written == 0) {
    LOG_ERR("OTA", "no body bytes received");
    lastError = "empty_body";
    return HTTP_ERROR;
  }
  // Reject truncated downloads before flashing. The firmware-flasher only does a magic-byte /
  // min-size check on the SD file, so a short body (network drop mid-transfer) would otherwise
  // still go through and brick on reboot.
  const size_t expectedSize = totalSize > 0 ? totalSize : otaSize;
  if (expectedSize > 0 && written != expectedSize) {
    LOG_ERR("OTA", "short body: got=%u want=%u", static_cast<unsigned>(written), static_cast<unsigned>(expectedSize));
    char buf[48];
    snprintf(buf, sizeof(buf), "short_body:%u/%u", static_cast<unsigned>(written), static_cast<unsigned>(expectedSize));
    lastError = buf;
    return HTTP_ERROR;
  }
  LOG_INF("OTA", "download complete: %u bytes -> %s", static_cast<unsigned>(written), kOtaSdPath);

  // Phase 2: flash from SD using the shared firmware flasher.
  phase = Phase::Flashing;
  totalSize = written;
  processedSize = 0;
  render = true;
  if (onProgress) onProgress(ctx);

  FlashCtx flashCtx{this, onProgress, ctx};
  auto progressCb = +[](size_t written, size_t total, void* fctx) {
    auto* fc = static_cast<FlashCtx*>(fctx);
    fc->updater->setProcessed(written);
    fc->updater->setExpectedSize(total);
    if (fc->onProgress) fc->onProgress(fc->userCtx);
  };

  const auto fr = firmware_flash::flashFromSdPath(kOtaSdPath, progressCb, &flashCtx);
  if (fr != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "flash failed: %s", firmware_flash::resultName(fr));
    char buf[32];
    snprintf(buf, sizeof(buf), "flash:%s", firmware_flash::resultName(fr));
    lastError = buf;
    if (fr == firmware_flash::Result::BAD_CHIP || fr == firmware_flash::Result::WRONG_BOARD) {
      return WRONG_DEVICE_ERROR;
    }
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
