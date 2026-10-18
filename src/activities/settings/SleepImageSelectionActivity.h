#pragma once

#include <set>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

// Browse and toggle which images are eligible for the random sleep-screen
// rotation. Lists every BMP/PNG under SLEEP_IMAGE_DIRS, one list and one
// selection for all sleep screen modes; the mode only changes how the preview
// is drawn (Transparent: over a blank page). Persists via
// SleepImageSelectionStore.
//
// UX:
//   - Up / Down / Left / Right : navigate previous / next image
//   - Confirm                  : toggle the current image's selected state
//   - Back                     : save the store and exit
//   Touch boards: swipe left/right or tap the right/left half of the image
//   to change image, tap the bottom strip to toggle, header back arrow (or
//   edge swipe) to exit.
//
// Each image is rendered full-screen by SleepActivity's own renderers
// (placement, cover mode/filter, grays), with a small overlay strip for the
// counter and the selected indicator, so what the user sees here is what
// they will see at sleep time.
class SleepImageSelectionActivity final : public Activity {
 public:
  explicit SleepImageSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SleepImageSelection", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Walks SLEEP_IMAGE_DIRS, collects any readable image, and runs the
  // store's prune pass against the discovered set so deleted files drop out
  // of the saved selection.
  void scanDirs();
  // Picker UI (strip, counter, hints) drawn over the preview image. Also called in each gray
  // plane, where it only clears its area on differential planes.
  void drawUi() const;
  void toggleCurrent();
  void step(int delta);
  // Touch layout: titled header (back arrow) on top, selection strip at the
  // bottom where the hidden button hints would sit. Returns the header rect.
  Rect touchHeaderRect() const;

  std::vector<std::string> imagePaths;  // full paths, e.g. "/.sleep/foo.bmp"
  int currentIndex = 0;
  bool dirty = false;        // any toggle since onEnter
  bool overlayMode = false;  // Transparent sleep screen: preview images as overlays
  // The framebuffer holds the current preview's B/W frame (with UI) and matches the panel.
  bool previewOnScreen = false;
  bool uiOnly = false;  // next render only redraws the UI (selection toggled)
  ButtonNavigator buttonNavigator;
};
