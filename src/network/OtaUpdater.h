#pragma once

#include <string>

class OtaUpdater {
  friend struct InstallCallbackClearer;

  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  bool render = false;
  void (*installProgressCallback)(void*) = nullptr;
  void* installProgressCallbackCtx = nullptr;

 public:
  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    /** Download ended before full image (per HTTP / OTA state). */
    OTA_DOWNLOAD_INCOMPLETE,
    /** Manual post-download image header check or otadata write failed (see OtaUpdater.cpp). */
    OTA_IMAGE_VALIDATE_FAILED,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  bool getRender() const { return render; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate();
  void setInstallProgressCallback(void (*cb)(void*), void* ctx);
};
