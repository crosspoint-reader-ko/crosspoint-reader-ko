#include "SleepImageSelectionStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace {
constexpr char SLEEP_SELECTION_FILE[] = "/.crosspoint/sleep_selection.txt";
constexpr char CROSSPOINT_DIR[] = "/.crosspoint";
constexpr size_t MAX_LINE_BYTES = 256;  // SD path max is well below this
}  // namespace

void SleepImageSelectionStore::loadFromFile() {
  selected.clear();

  if (!Storage.exists(SLEEP_SELECTION_FILE)) {
    loaded = true;
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("SIS", SLEEP_SELECTION_FILE, file)) {
    LOG_ERR("SIS", "Failed to open %s for read", SLEEP_SELECTION_FILE);
    loaded = true;
    return;
  }

  // Buffer one line at a time. SD paths shouldn't approach 256 bytes; if a
  // garbage file produces an over-long line, drop it and continue.
  char lineBuf[MAX_LINE_BYTES];
  size_t pos = 0;
  while (file.available()) {
    int c = file.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (pos > 0) {
        lineBuf[pos] = '\0';
        selected.emplace(lineBuf);
        pos = 0;
      }
      continue;
    }
    if (pos + 1 < sizeof(lineBuf)) {
      lineBuf[pos++] = static_cast<char>(c);
    } else {
      // Line too long — discard and resync at next newline.
      LOG_ERR("SIS", "Discarding over-long line in %s", SLEEP_SELECTION_FILE);
      pos = 0;
      while (file.available()) {
        int d = file.read();
        if (d == '\n' || d == '\r') break;
      }
    }
  }
  // Trailing line without newline.
  if (pos > 0) {
    lineBuf[pos] = '\0';
    selected.emplace(lineBuf);
  }
  file.close();

  loaded = true;
  LOG_DBG("SIS", "Loaded %u sleep image selection(s)", static_cast<unsigned>(selected.size()));
}

bool SleepImageSelectionStore::saveToFile() const {
  // The store lives in the same hidden dir as recent.bin etc. Mirror that
  // create-if-missing pattern so a fresh SD card works without manual setup.
  if (!Storage.exists(CROSSPOINT_DIR)) {
    Storage.mkdir(CROSSPOINT_DIR);
  }

  HalFile file;
  if (!Storage.openFileForWrite("SIS", SLEEP_SELECTION_FILE, file)) {
    LOG_ERR("SIS", "Failed to open %s for write", SLEEP_SELECTION_FILE);
    return false;
  }

  for (const auto& path : selected) {
    file.write(reinterpret_cast<const uint8_t*>(path.c_str()), path.size());
    file.write(reinterpret_cast<const uint8_t*>("\n"), 1);
  }
  file.close();
  LOG_DBG("SIS", "Saved %u sleep image selection(s)", static_cast<unsigned>(selected.size()));
  return true;
}

void SleepImageSelectionStore::setSelected(const std::string& fullPath, bool isSelected) {
  if (isSelected) {
    selected.insert(fullPath);
  } else {
    selected.erase(fullPath);
  }
}

bool SleepImageSelectionStore::pruneMissing(const std::set<std::string>& existingPaths, const char* const* dirs,
                                            const size_t dirCount) {
  const auto inScannedDir = [dirs, dirCount](const std::string& path) {
    for (size_t i = 0; i < dirCount; i++) {
      const size_t len = strlen(dirs[i]);
      if (path.size() > len && path.compare(0, len, dirs[i]) == 0 && path[len] == '/') return true;
    }
    return false;
  };
  bool changed = false;
  for (auto it = selected.begin(); it != selected.end();) {
    if (inScannedDir(*it) && existingPaths.count(*it) == 0) {
      LOG_DBG("SIS", "Pruning missing selection: %s", it->c_str());
      it = selected.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  return changed;
}
