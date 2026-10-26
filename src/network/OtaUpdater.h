#pragma once

#include <string>

class OtaUpdater {
 public:
  enum class Phase { Idle, Downloading, Flashing };

 private:
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  bool render = false;
  Phase phase = Phase::Idle;
  std::string lastError;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
    OTHER_APP_ERROR,  // dual-boot: the update slot holds another app
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  bool getRender() const { return render; }

  Phase getPhase() const { return phase; }

  const std::string& getLastError() const { return lastError; }

  // For internal callbacks (esp_http_client event handler, firmware flasher).
  void setExpectedSize(size_t s);
  void setProcessed(size_t s);

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  bool isUpdateNewerKO() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
