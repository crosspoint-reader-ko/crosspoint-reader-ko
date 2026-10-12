#include "SleepImageSelectionActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cmath>

#include "MappedInputManager.h"
#include "SleepImageSelectionStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Probed in priority order; both names are accepted by SleepActivity.
constexpr const char* SLEEP_DIRS[] = {"/.sleep", "/sleep"};

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
  requestUpdate();
}

void SleepImageSelectionActivity::step(const int delta) {
  const int count = static_cast<int>(imagePaths.size());
  currentIndex = (currentIndex + delta + count) % count;
  requestUpdate();
}

void SleepImageSelectionActivity::onEnter() {
  Activity::onEnter();
  scanDirs();
  SLEEP_IMAGE_SELECTION.loadFromFile();

  // Drop any saved entries whose underlying file is gone. We do this here
  // (rather than at sleep time) so the user sees an accurate selection state
  // immediately and we don't keep stale paths alive longer than needed.
  std::set<std::string> existing(imagePaths.begin(), imagePaths.end());
  if (SLEEP_IMAGE_SELECTION.pruneMissing(existing)) {
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

  for (const char* dirPath : SLEEP_DIRS) {
    auto dir = Storage.open(dirPath);
    if (!dir || !dir.isDirectory()) {
      continue;
    }
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(nameBuf, sizeof(nameBuf));
      std::string fname = nameBuf;
      if (fname.empty() || fname[0] == '.') continue;
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
      if (tapY >= header.y + header.height) {
        toggleCurrent();
      }
      return;
    }
  }

  buttonNavigator.onNextRelease([this] { step(1); });
  buttonNavigator.onPreviousRelease([this] { step(-1); });
}

void SleepImageSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool touch = mappedInput.hasTouch();

  if (imagePaths.empty()) {
    if (touch) {
      GUI.drawHeader(renderer, touchHeaderRect(), tr(STR_SELECT_SLEEP_SCREENS));
    }
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 10, tr(STR_NO_FILES_FOUND));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, "/.sleep, /sleep");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Top strip reserves a thin band for the position counter + selected
  // icon. Keeps the bottom of the screen clear so the button hints don't
  // collide with status text (which was getting clipped before).
  int previewY = OVERLAY_HEIGHT;
  int previewHeight = pageHeight - OVERLAY_HEIGHT;
  Rect header{};
  if (touch) {
    header = touchHeaderRect();
    previewY = header.y + header.height;
    previewHeight = pageHeight - previewY - TOUCH_STRIP_HEIGHT;
  }

  HalFile file;
  const auto& path = imagePaths[currentIndex];
  if (!Storage.openFileForRead("SIS", path, file)) {
    LOG_ERR("SIS", "Failed to open %s", path.c_str());
  } else {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      // Fit-style placement (no crop) inside the preview area, offset down
      // by the overlay height so the image lives strictly under the strip.
      int x = 0;
      int y = previewY;
      const float bmpRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(previewHeight);
      if (bmpRatio > screenRatio) {
        y = previewY + std::round((previewHeight - pageWidth / bmpRatio) / 2);
      } else {
        x = std::round((pageWidth - previewHeight * bmpRatio) / 2);
      }
      renderer.drawBitmap(bitmap, x, y, pageWidth, previewHeight, 0, 0);
    } else {
      LOG_ERR("SIS", "Header parse failed: %s", path.c_str());
    }
  }

  const bool selected = SLEEP_IMAGE_SELECTION.isSelected(path);
  if (touch) {
    char counter[24];
    snprintf(counter, sizeof(counter), "%d / %u", currentIndex + 1, static_cast<unsigned>(imagePaths.size()));
    GUI.drawHeader(renderer, header, tr(STR_SELECT_SLEEP_SCREENS), counter);

    // Bottom strip: checkbox + label, centered; tapping the preview toggles it.
    const int stripY = pageHeight - TOUCH_STRIP_HEIGHT;
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

    renderer.displayBuffer();
    return;
  }

  // Top overlay: white background + 1px bottom separator.
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
  char counter[24];
  snprintf(counter, sizeof(counter), "%d / %u", currentIndex + 1, static_cast<unsigned>(imagePaths.size()));
  const int counterWidth = renderer.getTextWidth(UI_10_FONT_ID, counter);
  const int counterX = pageWidth - counterWidth - ICON_MARGIN;
  renderer.drawText(UI_10_FONT_ID, counterX, (OVERLAY_HEIGHT - 14) / 2, counter);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
