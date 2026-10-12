#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class TxtReaderMenuActivity final : public UiListActivity {
 public:
  // Reader-side menu actions for TXT files. A subset of the EPUB menu — TXT
  // has no chapters, footnotes, or sync targets, so we expose only the
  // operations that make sense against a flat byte-offset reader.
  enum class MenuAction {
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    PAGE_JUMP_STEP,
    ROTATE_SCREEN,
    SCREENSHOT,
    GO_HOME,
    RESET_READING_TIMER,
    READER_OPTIONS,
  };

  explicit TxtReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                 int currentPage, int totalPages, int bookProgressPercent, uint8_t currentOrientation,
                                 uint8_t currentPageTurnOption, uint8_t currentPageJumpOption,
                                 uint32_t totalReadingSeconds);

  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool handleHomeGesture() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static constexpr size_t MENU_ITEM_COUNT = 8;
  static const MenuItem MENU_ITEMS[MENU_ITEM_COUNT];
  // Labels/actionValue are set once in the constructor; buildScreen() only
  // refreshes the rows whose values reflect live state.
  freeink::ui::ListItem menuRowItems[MENU_ITEM_COUNT]{};

  int listCount() const override { return static_cast<int>(MENU_ITEM_COUNT); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; row navigation
  // falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;

  void closeCancelled();

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  uint8_t selectedPageJumpOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  // Auto-turn rate options match the EPUB reader to keep behavior consistent.
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  // Long-press multi-page jump options. Index 0 = OFF (single-page navigation
  // unchanged); other entries become the jump step when the navigation button
  // is held >= 1 second.
  const std::vector<const char*> pageJumpLabels = {I18N.get(StrId::STR_STATE_OFF), "10", "20", "50", "100"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
  uint32_t totalReadingSeconds = 0;
};
