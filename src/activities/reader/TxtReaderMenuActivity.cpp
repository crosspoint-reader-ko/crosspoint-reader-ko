#include "TxtReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/settings/ReaderOptionsActivity.h"
#include "components/UITheme.h"
#include "util/ReadingStats.h"

namespace fui = freeink::ui;

const TxtReaderMenuActivity::MenuItem TxtReaderMenuActivity::MENU_ITEMS[MENU_ITEM_COUNT] = {
    {MenuAction::READER_OPTIONS, StrId::STR_READER_OPTIONS},
    {MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION},
    {MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN},
    {MenuAction::PAGE_JUMP_STEP, StrId::STR_PAGE_JUMP_STEP},
    {MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT},
    {MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON},
    {MenuAction::RESET_READING_TIMER, StrId::STR_RESET_READING_TIMER},
    {MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON},
};

TxtReaderMenuActivity::TxtReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::string& title, const int currentPage, const int totalPages,
                                             const int bookProgressPercent, const uint8_t currentOrientation,
                                             const uint8_t currentPageTurnOption, const uint8_t currentPageJumpOption,
                                             const uint32_t totalReadingSeconds)
    : UiListActivity("TxtReaderMenu", renderer, mappedInput),
      title(title),
      pendingOrientation(currentOrientation),
      selectedPageTurnOption(currentPageTurnOption),
      selectedPageJumpOption(currentPageJumpOption),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent),
      totalReadingSeconds(totalReadingSeconds) {
  for (size_t i = 0; i < MENU_ITEM_COUNT; i++) {
    menuRowItems[i].label = I18N.get(MENU_ITEMS[i].labelId);
    menuRowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void TxtReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption, selectedPageJumpOption};
  setResult(std::move(result));
  finish();
}

bool TxtReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void TxtReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  const auto selectedAction = MENU_ITEMS[index].action;
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Only the renderer turns; SETTINGS.orientation stays unchanged so
                       // the reader's result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::PAGE_JUMP_STEP) {
    optionPopup.show(I18N.get(StrId::STR_PAGE_JUMP_STEP), pageJumpLabels.data(),
                     static_cast<int>(pageJumpLabels.size()), selectedPageJumpOption, [this](int idx) {
                       selectedPageJumpOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  // Reader Options is a sub-activity of this menu — Back from options returns
  // to the menu, and changes are only applied when the menu itself is
  // dismissed (TxtReaderActivity reconciles on the menu result).
  if (selectedAction == MenuAction::READER_OPTIONS) {
    // forTxtReader=true drops Images and Embedded Style (EPUB-only).
    startActivityForResult(std::make_unique<ReaderOptionsActivity>(renderer, mappedInput, /*forTxtReader=*/true),
                           [this](const ActivityResult&) { requestUpdate(true); });
    return;
  }

  setResult(
      MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption, selectedPageJumpOption});
  finish();
}

bool TxtReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool TxtReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void TxtReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Progress summary (page N/M, book %, cumulative reading time).
  std::string progressLine;
  if (totalPages > 0) {
    progressLine =
        std::to_string(currentPage) + "/" + std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  char timeBuf[12];
  ReadingStats::format(totalReadingSeconds, timeBuf, sizeof(timeBuf));
  progressLine += "  |  " + std::string(tr(STR_READING_TIME)) + ": " + timeBuf;
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (size_t i = 0; i < MENU_ITEM_COUNT; i++) {
    const auto action = MENU_ITEMS[i].action;
    if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::PAGE_JUMP_STEP) {
      menuRowItems[i].value = pageJumpLabels[selectedPageJumpOption];
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Same row styling as the EPUB reader menu.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void TxtReaderMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void TxtReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();

  renderUi();

  drawFooter();
  renderer.displayBuffer();
}
