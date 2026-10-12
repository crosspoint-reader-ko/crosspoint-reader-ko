#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "SettingsActivity.h"
#include "activities/UiListActivity.h"

// Reader-side settings flyout reachable from the EPUB/TXT reader menu (Korean replacement for
// upstream's TextSettingsActivity, which belongs to the .cpfont engine this build does not carry).
//
// The setting definitions are SHARED with the home settings screen via the
// global `getSettingsList()` registry — this activity simply filters that
// registry by category and reuses the same toggle semantics. New settings
// added to `SettingsList.h` automatically surface here as long as they belong
// to the included categories (e.g. upstream's word/character spacing).
class ReaderOptionsActivity final : public UiListActivity {
 public:
  // `forTxtReader` drops EPUB-only settings (Images, Embedded Style) — TXT
  // files have no embedded images and no HTML/CSS styling, so those toggles
  // are meaningless in TXT context.
  explicit ReaderOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forTxtReader = false);
  void onEnter() override;
  void onExit() override;
  bool isReaderActivity() const override { return true; }

 private:
  // Build the filtered list. Reader Options exposes:
  //   - All STR_CAT_READER entries EXCEPT STR_ORIENTATION (the reader menu
  //     already offers orientation — duplicate exposure is confusing)
  //   - All STR_CAT_CONTROLS entries
  //   - STR_SUNLIGHT_FADING_FIX (display category, last)
  // STR_IMAGES and STR_EMBEDDED_STYLE are also dropped when forTxtReader is
  // true. Only TOGGLE/ENUM/VALUE types are kept here.
  static std::vector<SettingInfo> buildSettings(bool forTxtReader);

  int listCount() const override { return static_cast<int>(settings.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return tr(STR_READER_OPTIONS); }
  void drawFooter() override;

  void toggleSetting(int index);

  std::vector<SettingInfo> settings;
  // Row storage, sized once in onEnter(). valueTexts backs the const char* values of VALUE rows.
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::string> valueTexts;
  bool forTxtReader = false;
};
