#include "DosTheme.h"

#include <Arduino.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "DosHomeLayoutGeometry.h"
#include "RecentBooksStore.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace {
namespace fui = freeink::ui;

int scalePermille(const int value, const int permille) { return (value * permille + 500) / 1000; }

dos_layout::FontMetrics homeFontMetrics(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return {renderer.getLineHeight(uiScaleSpec().titleFontId),
          renderer.getLineHeight(UI_10_FONT_ID),
          renderer.getLineHeight(UI_12_FONT_ID),
          renderer.getLineHeight(UI_10_FONT_ID),
          renderer.getLineHeight(UI_12_FONT_ID),
          metrics.batteryBarHeight,
          metrics.topPadding};
}

std::string coreFirmwareVersion(const char* version) {
  if (!version || version[0] < '0' || version[0] > '9') return version ? version : "";

  size_t cursor = 0;
  for (int component = 0; component < 3; ++component) {
    const size_t start = cursor;
    while (version[cursor] >= '0' && version[cursor] <= '9') ++cursor;
    if (cursor == start) return version;
    if (component == 2) return std::string(version, cursor);
    if (version[cursor] != '.') return version;
    ++cursor;
  }
  return version;
}

void drawDosHomeHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const DosHomeLayout& layout) {
  const auto spec = uiScaleSpec();
  fui::GfxRendererFrame<1> ui(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
  const fui::ThemeTokens& tokens = refreshSharedUiThemeTokens(ui.target);
  ui.target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  ui.target.setFont(fui::GfxRendererTarget::FONT_LABEL, SMALL_FONT_ID);

  const fui::Rect band{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                       static_cast<int16_t>(rect.height)};
  fui::HeaderProps props;
  // Draw only the existing status chrome here. The DOS title, heap value, and
  // ASCII rule are placed explicitly from the font-derived home geometry.
  props.title = nullptr;
  BaseTheme::applyHeaderStatus(renderer, props);
  props.titleText = tokens.titleText;
  props.titleText.align = fui::TextAlign::Center;
  props.titleText.color = fui::Color::Black;
  props.titleText.bold = true;
  props.subtitleText = tokens.smallText;
  props.subtitleText.color = fui::Color::Black;
  props.styles = tokens.popup;
  props.styles.explicitlySet = true;
  props.styles.normal.background = fui::Paint::solid(fui::Color::White);
  props.styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  props.styles.normal.border = fui::Paint::none();
  props.styles.normal.borderWidth = 0;
  props.styles.normal.radius = 0;
  props.sidePadding = static_cast<int16_t>(UITheme::getInstance().getMetrics().headerSidePadding);
  props.borderEdges = 0;
  props.status.battery.color = fui::Color::Black;
  props.status.battery.text.color = fui::Color::Black;

  HeaderBackTapTarget::clear();
  fui::header(ui.frame, band, props);

  const int titleFontId = uiScaleSpec().titleFontId;
  const int screenWidth = renderer.getScreenWidth();
  const int titleWidth = renderer.getTextWidth(titleFontId, title);
  renderer.drawText(titleFontId, std::max(0, (screenWidth - titleWidth) / 2), layout.titleY, title, true);

  // ESP.getFreeHeap() is currently available heap, not the device's total
  // physical RAM capacity.
  char memoryLabel[32];
  dos_layout::formatFreeHeapLabel(memoryLabel, sizeof(memoryLabel), ESP.getFreeHeap());
  const int memoryWidth = renderer.getTextWidth(UI_10_FONT_ID, memoryLabel);
  renderer.drawText(UI_10_FONT_ID, std::max(0, (screenWidth - memoryWidth) / 2), layout.memoryY, memoryLabel, true);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sideMargin = std::max(12, metrics.headerSidePadding);
  // UI_10_FONT_ID follows the selected system font; its configured UI-font
  // glyph fallback keeps this ASCII character printable when an .epdfont omits it.
  const int equalsGlyphWidth = std::max(1, renderer.getTextWidth(UI_10_FONT_ID, "="));
  char rule[256];
  int characterCount = dos_layout::asciiRuleCharacterCount(screenWidth, sideMargin, equalsGlyphWidth,
                                                           static_cast<int>(sizeof(rule) - 1));
  std::fill_n(rule, characterCount, '=');
  rule[characterCount] = '\0';
  int ruleWidth = renderer.getTextWidth(UI_10_FONT_ID, rule);
  while (characterCount > 0 && ruleWidth > screenWidth - sideMargin * 2) {
    rule[--characterCount] = '\0';
    ruleWidth = renderer.getTextWidth(UI_10_FONT_ID, rule);
  }
  if (characterCount > 0) {
    renderer.drawText(UI_10_FONT_ID, (screenWidth - ruleWidth) / 2, layout.separatorY, rule, true);
  }
}

const char* recentBookTitle(const RecentBook& book) {
  if (!book.title.empty()) return book.title.c_str();
  const size_t separator = book.path.find_last_of("/\\");
  if (separator != std::string::npos && separator + 1 < book.path.size()) return book.path.c_str() + separator + 1;
  if (!book.path.empty()) return book.path.c_str();
  return tr(STR_LIBRARY_UNKNOWN_TITLE);
}

void drawProgressLine(GfxRenderer& renderer, const int y, const int rightEdge, const int leftLimit,
                      const int progress) {
  constexpr int MAX_BAR_SEGMENTS = 20;
  const int hashWidth = renderer.getTextWidth(UI_10_FONT_ID, "#");
  const int dotWidth = renderer.getTextWidth(UI_10_FONT_ID, ".");
  const int openWidth = renderer.getTextWidth(UI_10_FONT_ID, "[");
  const int closeWidth = renderer.getTextWidth(UI_10_FONT_ID, "]");
  const int gap = std::max(4, scalePermille(renderer.getScreenWidth(), 12));

  char percentage[8];
  if (progress < 0) {
    std::snprintf(percentage, sizeof(percentage), "--%%");
  } else {
    std::snprintf(percentage, sizeof(percentage), "%d%%", std::clamp(progress, 0, 100));
  }
  const int percentageWidth = renderer.getTextWidth(UI_10_FONT_ID, percentage);
  const int percentageColumnWidth =
      std::max(renderer.getTextWidth(UI_10_FONT_ID, "100%"), renderer.getTextWidth(UI_10_FONT_ID, "--%"));
  const dos_layout::ProgressLine line =
      dos_layout::progressLine(renderer.getScreenWidth(), rightEdge, leftLimit, hashWidth, dotWidth, openWidth,
                               closeWidth, percentageColumnWidth, gap, MAX_BAR_SEGMENTS);
  if (line.segmentCount <= 0) {
    renderer.drawText(UI_10_FONT_ID, line.percentageTextX(percentageWidth), y, percentage, true);
    return;
  }

  int x = line.barX;
  renderer.drawText(UI_10_FONT_ID, x, y, "[", true);
  x += openWidth;

  const int filled = progress < 0 ? 0 : (std::clamp(progress, 0, 100) * line.segmentCount + 50) / 100;
  for (int i = 0; i < line.segmentCount; ++i) {
    const char* glyph = i < filled ? "#" : ".";
    const int glyphWidth = i < filled ? hashWidth : dotWidth;
    const int glyphX = x + i * line.cellWidth + std::max(0, (line.cellWidth - glyphWidth) / 2);
    renderer.drawText(UI_10_FONT_ID, glyphX, y, glyph, true);
  }

  x += line.cellWidth * line.segmentCount;
  renderer.drawText(UI_10_FONT_ID, x, y, "]", true);
  renderer.drawText(UI_10_FONT_ID, line.percentageTextX(percentageWidth), y, percentage, true);
}
}  // namespace

DosHomeLayout DosTheme::homeLayout(const GfxRenderer& renderer, int recentBookCount) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const dos_layout::FontMetrics fontMetrics = homeFontMetrics(renderer);
  if (recentBookCount < 0) recentBookCount = dos_layout::recentBookLimit(screenWidth, screenHeight, fontMetrics);
  const dos_layout::Geometry geometry = dos_layout::calculate(screenWidth, screenHeight, fontMetrics, recentBookCount);

  DosHomeLayout layout{};
  layout.header = Rect{geometry.header.x, geometry.header.y, geometry.header.width, geometry.header.height};
  layout.recent = Rect{geometry.recent.x, geometry.recent.y, geometry.recent.width, geometry.recent.height};
  layout.menu = Rect{geometry.menu.x, geometry.menu.y, geometry.menu.width, geometry.menu.height};
  layout.footer = Rect{geometry.footer.x, geometry.footer.y, geometry.footer.width, geometry.footer.height};
  layout.titleY = geometry.titleY;
  layout.memoryY = geometry.memoryY;
  layout.separatorY = geometry.separatorY;
  layout.contentX = geometry.contentX;
  layout.selectionX = geometry.selectionX;
  layout.selectionWidth = geometry.selectionWidth;
  layout.recentLabelY = geometry.recentLabelY;
  layout.recentRowsTop = geometry.recentRowsTop;
  layout.recentRowStep = geometry.recentRowStep;
  layout.recentProgressOffset = geometry.recentProgressOffset;
  layout.recentBookCount = geometry.recentBookCount;
  layout.syntaxErrorY = geometry.syntaxErrorY;
  layout.menuLabelY = geometry.menuLabelY;
  layout.menuRowsTop = geometry.menuRowsTop;
  layout.menuRowHeight = geometry.menuRowHeight;
  layout.menuRowsBottom = geometry.menuRowsBottom;
  layout.menuPreferredRowStep = geometry.menuPreferredRowStep;
  layout.sectionGap = geometry.sectionGap;
  layout.menuRowStep = geometry.menuRowStep;
  layout.promptY = geometry.promptY;
  return layout;
}

int DosTheme::recentBooksLimit(const GfxRenderer& renderer) {
  return dos_layout::recentBookLimit(renderer.getScreenWidth(), renderer.getScreenHeight(), homeFontMetrics(renderer));
}

int DosTheme::homeMenuRowStep(const GfxRenderer& renderer, const int /*itemCount*/, const int recentBookCount) {
  const DosHomeLayout layout = homeLayout(renderer, recentBookCount);
  return layout.menuRowStep;
}

int DosTheme::homeMenuRowHeight(const GfxRenderer& renderer, const int /*itemCount*/, const int recentBookCount) {
  const DosHomeLayout layout = homeLayout(renderer, recentBookCount);
  return layout.menuRowHeight;
}

void DosTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                          const bool backButton) const {
  if (title || backButton) {
    BaseTheme::drawHeader(renderer, rect, title, subtitle, backButton);
    return;
  }

  const std::string homeTitle = std::string("갈래목 ") + coreFirmwareVersion(CROSSPOINT_VERSION);
  const DosHomeLayout layout = homeLayout(renderer);
  drawDosHomeHeader(renderer, rect, homeTitle.c_str(), layout);
}

void DosTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount, const int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& /*rowIcon*/) const {
  DosHomeLayout layout = homeLayout(renderer, 0);
  const int recentLimit = recentBooksLimit(renderer);
  for (int count = 0; count <= recentLimit; ++count) {
    const DosHomeLayout candidate = homeLayout(renderer, count);
    if (candidate.menu.y == rect.y) {
      layout = candidate;
      break;
    }
  }
  const int rowWidth = std::max(0, renderer.getScreenWidth() - layout.contentX * 2);
  if (rowWidth <= 0 || rect.height <= 0) return;

  const std::string menuPrompt = "C:\\Xteink>Menu";
  const std::string prompt = "C:\\Xteink>_";
  renderer.drawText(UI_10_FONT_ID, layout.contentX, layout.menuLabelY, menuPrompt.c_str(), true);

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int rowHeight = layout.menuRowHeight;
  const int rowStep = layout.menuRowStep;
  for (int i = 0; i < buttonCount; ++i) {
    const int rowY = layout.menuRowsTop + i * rowStep;
    if (rowY + rowHeight > layout.promptY || rowY + rowHeight > rect.y + rect.height) break;

    const bool selected = selectedIndex == i;
    if (selected) {
      renderer.fillRectDither(layout.selectionX, rowY, layout.selectionWidth, rowHeight, Color::LightGray);
    }

    std::string label = selected ? "> " : "  ";
    label += buttonLabel(i);
    const std::string line = renderer.truncatedText(UI_12_FONT_ID, label.c_str(), rowWidth);
    const int textY = rowY + (rowHeight - lineHeight) / 2;
    renderer.drawText(UI_12_FONT_ID, layout.contentX, textY, line.c_str(), true);
  }

  renderer.drawText(UI_10_FONT_ID, layout.contentX, layout.promptY, prompt.c_str(), true);
}

void DosTheme::drawHomeRecentBooks(GfxRenderer& renderer, const Rect rect, const std::vector<RecentBook>& recentBooks,
                                   const int selectorIndex, const int* progress, const size_t progressCount) const {
  const DosHomeLayout layout = homeLayout(renderer, static_cast<int>(recentBooks.size()));
  const int screenWidth = renderer.getScreenWidth();
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rightEdge = screenWidth - layout.contentX;
  const int titleWidth = std::max(0, screenWidth - layout.contentX * 2);
  const std::string command = "C:\\Xteink>Recent";
  renderer.drawText(UI_10_FONT_ID, layout.contentX, layout.recentLabelY, command.c_str(), true);

  if (recentBooks.empty()) {
    const std::string emptyLabel = renderer.truncatedText(UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK), titleWidth);
    renderer.drawText(UI_12_FONT_ID, layout.contentX, layout.recentRowsTop, emptyLabel.c_str(), true);
  }

  for (int i = 0; i < layout.recentBookCount; ++i) {
    const int rowY = layout.recentRowsTop + i * layout.recentRowStep;
    const bool selected = selectorIndex == i;
    if (selected) {
      const int progressY = rowY + layout.recentProgressOffset + (titleLineHeight - smallLineHeight) / 2;
      const dos_layout::Box highlight =
          dos_layout::recentTitleSelection(layout.selectionX, layout.selectionWidth, rect.y, rect.y + rect.height, rowY,
                                           titleLineHeight, progressY, layout.sectionGap);
      if (highlight.height > 0) {
        renderer.fillRectDither(highlight.x, highlight.y, highlight.width, highlight.height, Color::LightGray);
      }
    }

    std::string title = selected ? "> " : "";
    title += recentBookTitle(recentBooks[i]);
    title = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), titleWidth);
    renderer.drawText(UI_12_FONT_ID, layout.contentX, rowY, title.c_str(), true);

    const int bookProgress = progress && static_cast<size_t>(i) < progressCount ? progress[i] : -1;
    drawProgressLine(renderer, rowY + layout.recentProgressOffset + (titleLineHeight - smallLineHeight) / 2, rightEdge,
                     layout.contentX, bookProgress);
  }

  renderer.drawText(UI_10_FONT_ID, layout.contentX, layout.syntaxErrorY, "Syntax error", true);
}

void DosTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const {
  if (gpio.hasTouch()) return;

  const DosHomeLayout layout = homeLayout(renderer);
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int left = layout.selectionX;
  const int width = layout.selectionWidth;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  if (width <= 0 || layout.footer.height <= 0) return;

  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || labels[i][0] == '\0') continue;
    const int cellLeft = left + width * i / 4;
    const int cellRight = left + width * (i + 1) / 4;
    const int cellWidth = cellRight - cellLeft;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, labels[i], cellWidth);
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str());
    const int textX = cellLeft + (cellWidth - textWidth) / 2;
    const int textY = layout.footer.y + (layout.footer.height - lineHeight) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, textY, label.c_str(), true);
  }
}
