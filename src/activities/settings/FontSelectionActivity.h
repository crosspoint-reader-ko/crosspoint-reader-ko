#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Picks an SD-card .epdfont for the reader body or the UI fallback slot.
 * Rows come from /.crosspoint/fonts, /fonts and /.fonts; row 0 is the built-in default.
 */
class FontSelectionActivity final : public UiListActivity {
 public:
  // Which font slot this picker configures:
  //   Reader  -> SETTINGS.customFontPath (EPUB body font; default KoPub Batang)
  //   System  -> SETTINGS.systemFontPath (UI glyph fallback; default Pretendard)
  enum class Target { Reader, System };

  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Target target = Target::Reader)
      : UiListActivity("FontSelection", renderer, mappedInput), target_(target) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  Target target_ = Target::Reader;
  std::vector<std::string> fontFiles;  // List of font file paths
  std::vector<std::string> fontNames;  // Display names (without path and extension)
  // Row storage, built once in onEnter(); labels point into fontNames.
  std::vector<freeink::ui::ListItem> rowItems;

  bool isSystemTarget() const { return target_ == Target::System; }

  // Index of the font currently saved for this slot (0 = built-in default).
  int currentFontIndex() const;
  void loadFontList();

  static constexpr const char* FONTS_DIR = "/.crosspoint/fonts";
  static constexpr const char* ROOT_FONTS_DIR = "/fonts";
  static constexpr const char* HIDDEN_FONTS_DIR = "/.fonts";  // upstream hidden font root

  // Scans dirPath for .epdfont files; when recurseIntoSubdirs is true, also descends one level
  // into per-family subfolders (layout /fonts/<Family>/<Family>_<size>.epdfont).
  void scanFontsInDirectory(const char* dirPath, bool recurseIntoSubdirs = true);
};
