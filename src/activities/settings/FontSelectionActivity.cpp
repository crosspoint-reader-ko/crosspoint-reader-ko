#include "FontSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* DEFAULT_FONT_NAME = "KoPub 바탕 (기본)";
constexpr const char* CACHE_DIR = "/.crosspoint/cache";

// Index of `path` in `fontFiles`, or 0 (the built-in default entry) when it is not listed.
int indexOfFont(const std::vector<std::string>& fontFiles, const char* path) {
  if (fontFiles.size() < 2) return 0;
  const auto it =
      std::find_if(fontFiles.begin() + 1, fontFiles.end(), [path](const std::string& file) { return file == path; });
  return it == fontFiles.end() ? 0 : static_cast<int>(it - fontFiles.begin());
}

// Recursively delete a directory and its contents
void deleteDirectory(const char* path) {
  HalFile dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    char entryName[64];
    entry.getName(entryName, sizeof(entryName));
    bool entryIsDir = entry.isDirectory();
    entry.close();

    std::string fullPath = std::string(path) + "/" + entryName;
    if (entryIsDir) {
      deleteDirectory(fullPath.c_str());
    } else {
      Storage.remove(fullPath.c_str());
    }
  }
  dir.close();
  Storage.rmdir(path);
}

// Invalidate rendering caches for EPUB and TXT readers
// Keeps progress.bin (reading position) but removes layout caches
void invalidateReaderCaches() {
  LOG_DBG("FNT", "Invalidating reader rendering caches...");

  HalFile cacheDir = Storage.open(CACHE_DIR);
  if (!cacheDir || !cacheDir.isDirectory()) {
    if (cacheDir) cacheDir.close();
    LOG_DBG("FNT", "No cache directory found");
    return;
  }

  int deletedCount = 0;
  while (true) {
    HalFile bookCache = cacheDir.openNextFile();
    if (!bookCache) break;
    char bookCacheName[64];
    bookCache.getName(bookCacheName, sizeof(bookCacheName));
    bookCache.close();

    std::string bookCachePath = std::string(CACHE_DIR) + "/" + bookCacheName;

    // For EPUB: delete sections/ folder (keeps progress.bin)
    std::string sectionsPath = bookCachePath + "/sections";
    HalFile sectionsDir = Storage.open(sectionsPath.c_str());
    if (sectionsDir && sectionsDir.isDirectory()) {
      sectionsDir.close();
      deleteDirectory(sectionsPath.c_str());
      LOG_DBG("FNT", "Deleted EPUB sections cache: %s", sectionsPath.c_str());
      deletedCount++;
    } else {
      if (sectionsDir) sectionsDir.close();
    }

    // For TXT: delete index.bin (keeps progress.bin)
    std::string indexPath = bookCachePath + "/index.bin";
    if (Storage.exists(indexPath.c_str())) {
      Storage.remove(indexPath.c_str());
      LOG_DBG("FNT", "Deleted TXT index cache: %s", indexPath.c_str());
      deletedCount++;
    }
  }
  cacheDir.close();

  LOG_DBG("FNT", "Invalidated %d cache entries", deletedCount);
}
}  // namespace

void FontSelectionActivity::scanFontsInDirectory(const char* dirPath, bool recurseIntoSubdirs) {
  HalFile dir = Storage.open(dirPath);
  if (!dir) {
    LOG_DBG("FNT", "Font folder %s not found", dirPath);
    return;
  }

  if (!dir.isDirectory()) {
    LOG_DBG("FNT", "%s is not a directory", dirPath);
    dir.close();
    return;
  }

  // Scan for .epdfont files here, and one level into family subfolders. The per-family
  // layout places each file at /fonts/<Family>/<Family>_<size>.epdfont, so we must look
  // inside per-family subfolders; flat files directly in dirPath are also accepted.
  while (true) {
    HalFile file = dir.openNextFile();
    if (!file) break;
    char name[64];
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    file.close();  // close before recursing/continuing (serialize SD handle use)

    if (isDir) {
      // Recurse exactly one level into a family subfolder. Skip hidden/system entries.
      if (recurseIntoSubdirs && name[0] != '.' && strncmp(name, "._", 2) != 0) {
        std::string subPath = std::string(dirPath) + "/" + name;
        scanFontsInDirectory(subPath.c_str(), false);
      }
      continue;
    }

    // Accept .epdfont files (the format SdFont::load reads); skip macOS hidden files (._*)
    const size_t len = strlen(name);
    if (len > 8 && strcasecmp(name + len - 8, ".epdfont") == 0 && strncmp(name, "._", 2) != 0) {
      std::string fullPath = std::string(dirPath) + "/" + name;
      fontFiles.push_back(fullPath);
      fontNames.push_back(std::string(name, len - 8));  // display name without extension
      LOG_DBG("FNT", "Found font: %s", fullPath.c_str());
    }
  }
  dir.close();
}

void FontSelectionActivity::loadFontList() {
  fontFiles.clear();
  fontNames.clear();

  // First entry is always the default font (empty path means default).
  // Reader default is KoPub Batang; UI system-font default is Pretendard.
  fontFiles.emplace_back("");
  fontNames.emplace_back(isSystemTarget() ? "Pretendard (기본)" : DEFAULT_FONT_NAME);

  // Ensure fonts directory exists
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(FONTS_DIR);

  // Scan /.crosspoint/fonts, the visible /fonts root, and the hidden /.fonts root.
  // Each scan also descends one level into per-family subfolders (the layout:
  // /fonts/<Family>/<Family>_<size>.epdfont, or /.fonts/<Family>/... when hidden).
  scanFontsInDirectory(FONTS_DIR);
  scanFontsInDirectory(ROOT_FONTS_DIR);
  scanFontsInDirectory(HIDDEN_FONTS_DIR);

  LOG_DBG("FNT", "Total fonts found: %zu (including default)", fontFiles.size());
}

int FontSelectionActivity::currentFontIndex() const {
  const bool hasCurrent = isSystemTarget() ? SETTINGS.hasSystemFont() : SETTINGS.hasCustomFont();
  if (!hasCurrent) return 0;
  return indexOfFont(fontFiles, isSystemTarget() ? SETTINGS.systemFontPath : SETTINGS.customFontPath);
}

void FontSelectionActivity::onEnter() {
  loadFontList();

  // Built once: activateIndex() leaves the screen, so the "Selected" marker cannot go stale.
  const int current = currentFontIndex();
  rowItems.assign(fontNames.size(), fui::ListItem{});
  for (size_t i = 0; i < fontNames.size(); i++) {
    rowItems[i].label = fontNames[i].c_str();
    rowItems[i].actionValue = static_cast<int16_t>(i);
    if (static_cast<int>(i) == current) rowItems[i].value = tr(STR_SELECTED);
  }

  UiListActivity::onEnter();
  // Open on the current font; the first build pulls the viewport to it.
  nav.selected = current;
}

const char* FontSelectionActivity::headerTitle() const { return isSystemTarget() ? tr(STR_SYSTEM_FONT) : tr(STR_FONT); }

void FontSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(fontFiles.size())) return;
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  RenderLock lock(*this);
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 10, "글꼴 적용 중...");
  renderer.displayBuffer();

  // Update the target font path in settings
  char* destPath = isSystemTarget() ? SETTINGS.systemFontPath : SETTINGS.customFontPath;
  const size_t destSize = isSystemTarget() ? sizeof(SETTINGS.systemFontPath) : sizeof(SETTINGS.customFontPath);
  if (index == 0) {
    // Default selected - clear the path
    destPath[0] = '\0';
  } else {
    strncpy(destPath, fontFiles[index].c_str(), destSize - 1);
    destPath[destSize - 1] = '\0';
  }

  SETTINGS.saveToFile();
  LOG_DBG("FNT", "Font selected (%s): %s", isSystemTarget() ? "system" : "reader", index == 0 ? "default" : destPath);

  if (isSystemTarget()) {
    // Reload UI system font dynamically (no reboot needed). UI-only, so reader caches
    // are unaffected and must NOT be invalidated.
    reloadSystemFont();
  } else {
    // Reload custom reader font dynamically (no reboot needed)
    reloadCustomReaderFont();
    // Invalidate EPUB/TXT caches since the reader font changed
    invalidateReaderCaches();
  }

  finish();
}

void FontSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
