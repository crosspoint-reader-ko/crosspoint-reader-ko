#include "SleepImageSelectionActivity.h"

#include <Bitmap.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SleepImageSelectionStore.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Top overlay strip — keeps the bottom of the preview clear so the button
// hints don't fight for space with status text. The strip is intentionally
// thin and uses an icon (filled/outlined square) for selection state plus a
// short "i/N" counter so we don't have to letterbox the full filename.
constexpr int OVERLAY_HEIGHT = 32;
constexpr int ICON_SIZE = 20;
constexpr int ICON_MARGIN = 6;
// Touch-board selection strip at the bottom (button hints are hidden there).
constexpr int TOUCH_STRIP_HEIGHT = 56;
constexpr int TOUCH_ICON_SIZE = 28;
constexpr int TOUCH_ICON_GAP = 12;
}  // namespace

Rect SleepImageSelectionActivity::touchHeaderRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  return Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight};
}

void SleepImageSelectionActivity::toggleCurrent() {
  const auto& path = imagePaths[currentIndex];
  SLEEP_IMAGE_SELECTION.setSelected(path, !SLEEP_IMAGE_SELECTION.isSelected(path));
  dirty = true;
  uiOnly = previewOnScreen;
  requestUpdate();
}

void SleepImageSelectionActivity::step(const int delta) {
  const int count = static_cast<int>(imagePaths.size());
  currentIndex = (currentIndex + delta + count) % count;
  uiOnly = false;
  requestUpdate();
}

void SleepImageSelectionActivity::onEnter() {
  Activity::onEnter();
  overlayMode = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM;
  scanDirs();
  SLEEP_IMAGE_SELECTION.loadFromFile();

  // Drop any saved entries whose underlying file is gone. We do this here
  // (rather than at sleep time) so the user sees an accurate selection state
  // immediately and we don't keep stale paths alive longer than needed.
  std::set<std::string> existing(imagePaths.begin(), imagePaths.end());
  if (SLEEP_IMAGE_SELECTION.pruneMissing(existing, SLEEP_IMAGE_DIRS, SLEEP_IMAGE_DIR_COUNT)) {
    dirty = true;
  }

  currentIndex = 0;
  requestUpdate();
}

void SleepImageSelectionActivity::onExit() {
  if (dirty) {
    SLEEP_IMAGE_SELECTION.saveToFile();
  }
  Activity::onExit();
}

void SleepImageSelectionActivity::scanDirs() {
  imagePaths.clear();
  char nameBuf[256];

  for (const char* dirPath : SLEEP_IMAGE_DIRS) {
    auto dir = Storage.open(dirPath);
    if (!dir || !dir.isDirectory()) {
      continue;
    }
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(nameBuf, sizeof(nameBuf));
      std::string fname = nameBuf;
      if (fname.empty() || fname[0] == '.') continue;
      if (FsHelpers::hasPngExtension(fname)) {
        std::string fullPath = std::string(dirPath) + "/" + fname;
        ImageDimensions dims{};
        if (!PngToFramebufferConverter::getDimensionsStatic(fullPath, dims)) {
          LOG_DBG("SIS", "Skipping invalid PNG: %s", fullPath.c_str());
          continue;
        }
        imagePaths.emplace_back(std::move(fullPath));
        continue;
      }
      if (!FsHelpers::hasBmpExtension(fname)) continue;
      // Validate header so we don't ship the user a "selected" image that
      // SleepActivity will silently skip later.
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SIS", "Skipping invalid BMP: %s/%s", dirPath, fname.c_str());
        continue;
      }
      imagePaths.emplace_back(std::string(dirPath) + "/" + fname);
    }
  }
  // Stable order across runs so navigation is predictable as the user adds
  // files: SD enumeration order is filesystem-dependent, so sort here.
  std::sort(imagePaths.begin(), imagePaths.end());
  LOG_DBG("SIS", "Found %u sleep image candidate(s)", static_cast<unsigned>(imagePaths.size()));
}

void SleepImageSelectionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (imagePaths.empty()) {
    // Nothing to navigate or toggle — wait for Back. Avoid touching the
    // navigator so we don't trigger spurious requestUpdate() loops.
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrent();
    return;
  }

  if (mappedInput.hasTouch()) {
    // Back-arrow taps and the edge back swipe already surfaced as Back above.
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left) {
      step(1);
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Right) {
      step(-1);
      return;
    }
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasScreenTapped(tapX, tapY)) {
      const Rect header = touchHeaderRect();
      if (tapY < header.y + header.height) return;
      // The bottom strip toggles the selection; the image itself pages (right half = next).
      if (tapY >= renderer.getScreenHeight() - TOUCH_STRIP_HEIGHT) {
        toggleCurrent();
      } else {
        step(tapX >= renderer.getScreenWidth() / 2 ? 1 : -1);
      }
      return;
    }
  }

  buttonNavigator.onNextRelease([this] { step(1); });
  buttonNavigator.onPreviousRelease([this] { step(-1); });
}

void SleepImageSelectionActivity::drawUi() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& path = imagePaths[currentIndex];
  const bool selected = SLEEP_IMAGE_SELECTION.isSelected(path);
  char counter[24];
  snprintf(counter, sizeof(counter), "%d / %u", currentIndex + 1, static_cast<unsigned>(imagePaths.size()));
  // Differential gray planes: zero bits leave the UI from the B/W pass untouched. Absolute planes
  // store black/white like the B/W frame, so the UI is drawn normally there.
  const bool deltaPlane = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  if (mappedInput.hasTouch()) {
    const Rect header = touchHeaderRect();
    const int stripY = pageHeight - TOUCH_STRIP_HEIGHT;
    if (deltaPlane) {
      renderer.fillRect(0, 0, pageWidth, header.y + header.height, true);
      renderer.fillRect(0, stripY, pageWidth, TOUCH_STRIP_HEIGHT, true);
      return;
    }
    renderer.fillRect(0, 0, pageWidth, header.y + header.height, false);
    GUI.drawHeader(renderer, header, tr(STR_SELECT_SLEEP_SCREENS), counter);

    // Bottom strip: checkbox + label, centered; tapping the strip toggles it.
    renderer.fillRect(0, stripY, pageWidth, TOUCH_STRIP_HEIGHT, false);
    renderer.fillRect(0, stripY, pageWidth, 1, true);
    const char* label = tr(STR_SELECTED);
    const int labelWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
    const int groupX = (pageWidth - (TOUCH_ICON_SIZE + TOUCH_ICON_GAP + labelWidth)) / 2;
    const int iconY = stripY + (TOUCH_STRIP_HEIGHT - TOUCH_ICON_SIZE) / 2;
    if (selected) {
      renderer.fillRect(groupX, iconY, TOUCH_ICON_SIZE, TOUCH_ICON_SIZE, true);
    } else {
      renderer.drawRect(groupX, iconY, TOUCH_ICON_SIZE, TOUCH_ICON_SIZE, 2, true);
    }
    const int labelY = stripY + (TOUCH_STRIP_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, groupX + TOUCH_ICON_SIZE + TOUCH_ICON_GAP, labelY, label);
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  if (deltaPlane) {
    renderer.fillRect(0, 0, pageWidth, OVERLAY_HEIGHT, true);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  // Top strip: white background + 1px bottom separator, drawn over the image.
  renderer.fillRect(0, 0, pageWidth, OVERLAY_HEIGHT, false);
  renderer.fillRect(0, OVERLAY_HEIGHT - 1, pageWidth, 1, true);

  // Selection icon (left edge). Filled square = selected, outlined = not
  // selected. Using rect primitives keeps the indicator independent of
  // font glyph coverage and clearly readable on e-ink.
  const int iconY = (OVERLAY_HEIGHT - ICON_SIZE) / 2;
  if (selected) {
    renderer.fillRect(ICON_MARGIN, iconY, ICON_SIZE, ICON_SIZE, true);
  } else {
    renderer.drawRect(ICON_MARGIN, iconY, ICON_SIZE, ICON_SIZE, 2, true);
  }

  // Position counter (right edge): "i / N" — no filename, by request.
  const int counterWidth = renderer.getTextWidth(UI_10_FONT_ID, counter);
  const int counterX = pageWidth - counterWidth - ICON_MARGIN;
  renderer.drawText(UI_10_FONT_ID, counterX, (OVERLAY_HEIGHT - 14) / 2, counter);

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SleepImageSelectionActivity::render(RenderLock&&) {
  if (uiOnly) {
    // Only the selection changed: the framebuffer still holds the preview's B/W frame, which the
    // panel uses as its baseline, so a fast refresh updates just the UI pixels and keeps the grays.
    uiOnly = false;
    drawUi();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  previewOnScreen = false;
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();

  if (imagePaths.empty()) {
    if (mappedInput.hasTouch()) {
      GUI.drawHeader(renderer, touchHeaderRect(), tr(STR_SELECT_SLEEP_SCREENS));
    }
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 10, tr(STR_NO_FILES_FOUND));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, "/.sleep, /sleep, /.sleep-overlay, /sleep-overlay");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // The image is drawn full screen by the sleep screen's own renderer (placement, cover
  // mode/filter, grays), so the preview matches what sleep shows; the UI sits on top.
  const auto& path = imagePaths[currentIndex];
  if (FsHelpers::hasPngExtension(path)) {
    // The PNG decoder needs a large contiguous block; SD font glyph caches are rebuilt on demand.
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  }
  SleepActivity::PreviewUi ui;
  ui.ctx = const_cast<SleepImageSelectionActivity*>(this);
  ui.draw = [](void* ctx) { static_cast<const SleepImageSelectionActivity*>(ctx)->drawUi(); };
  if (SleepActivity::renderPreview(renderer, mappedInput, path, overlayMode, ui)) {
    previewOnScreen = true;
    return;
  }

  LOG_ERR("SIS", "Preview failed: %s", path.c_str());
  renderer.clearScreen();
  drawUi();
  renderer.displayBuffer();
  previewOnScreen = true;
}
