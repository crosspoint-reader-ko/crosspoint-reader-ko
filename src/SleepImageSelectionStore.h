#pragma once

#include <cstddef>
#include <set>
#include <string>

// Persists which sleep-screen images the user has selected for random
// rotation: Custom wallpapers (BMP under /.sleep, /sleep) and Transparent
// overlays (BMP/PNG under /.sleep-overlay, /sleep-overlay) share one file. Stored on the SD card
// alongside the images themselves so a card swap moves the selection with
// the images, and so an OTA / SPIFFS reset doesn't wipe it.
//
// File format: plain text, one absolute path per line (e.g. "/.sleep/foo.bmp").
// Selection rules consumed by SleepActivity, applied per mode's directories:
//   - nothing selected in those dirs -> use ALL discovered images
//   - otherwise                      -> use only the selected entries
class SleepImageSelectionStore {
 public:
  static SleepImageSelectionStore& getInstance() {
    static SleepImageSelectionStore instance;
    return instance;
  }

  void loadFromFile();
  bool saveToFile() const;

  bool isSelected(const std::string& fullPath) const { return selected.count(fullPath) > 0; }
  void setSelected(const std::string& fullPath, bool isSelected);
  bool empty() const { return selected.empty(); }
  const std::set<std::string>& getSelected() const { return selected; }

  // Drop entries inside one of `dirs` whose path is not in `existingPaths`;
  // entries for other directories are kept. Returns true if anything was pruned.
  bool pruneMissing(const std::set<std::string>& existingPaths, const char* const* dirs, size_t dirCount);

 private:
  SleepImageSelectionStore() = default;
  std::set<std::string> selected;
  bool loaded = false;
};

#define SLEEP_IMAGE_SELECTION SleepImageSelectionStore::getInstance()
