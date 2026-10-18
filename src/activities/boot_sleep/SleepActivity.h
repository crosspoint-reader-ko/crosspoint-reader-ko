#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

  // UI drawn over a preview in its B/W pass (ctx is passed back to draw).
  struct PreviewUi {
    void* ctx = nullptr;
    void (*draw)(void* ctx) = nullptr;
  };
  // Korean: draws a sleep image exactly as the sleep screen would, for Sleep Image Selection.
  // overlay = Transparent mode art (drawn over a blank page). Restores the B/W frame afterwards.
  static bool renderPreview(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& path,
                            bool overlay, const PreviewUi& ui);

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderPngSleepImage(const std::string& path, bool overlay) const;
  bool renderSleepImagePath(const std::string& path) const;
  bool useDirectGrayBitmap() const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
};
