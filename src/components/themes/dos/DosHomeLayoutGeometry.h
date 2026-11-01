#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// Pure geometry shared by DosTheme and the host coordinate regression test.
// All sizes are logical pixels and all line heights come from the active UI
// font, including a selected SD-card system font.
namespace dos_layout {

struct Box {
  int x;
  int y;
  int width;
  int height;
};

struct FontMetrics {
  int titleLineHeight;
  int sectionLineHeight;
  int bodyLineHeight;
  int progressLineHeight;
  int hintLineHeight;
  int statusStripHeight;
  int topPadding;
};

struct Geometry {
  Box header;
  Box recent;
  Box menu;
  Box footer;
  int titleY;
  int memoryY;
  int separatorY;
  int contentX;
  int selectionX;
  int selectionWidth;
  int recentLabelY;
  int recentRowsTop;
  int recentRowStep;
  int recentProgressOffset;
  int recentBookCount;
  int syntaxErrorY;
  int menuLabelY;
  int menuRowsTop;
  int menuRowHeight;
  int menuPreferredRowStep;
  int menuRowStep;
  int menuRowsBottom;
  int promptY;
  int sectionGap;
  bool contentFits;
};

struct ProgressLine {
  int barX;
  int barWidth;
  int cellWidth;
  int segmentCount;
  int percentageColumnX;
  int percentageColumnWidth;
  int gap;

  int percentageTextX(const int measuredTextWidth) const {
    return percentageColumnX + percentageColumnWidth - std::max(0, measuredTextWidth);
  }
};

constexpr int MAX_RECENT_BOOKS = 4;
constexpr int MAX_MENU_ITEMS = 5;  // Four core actions plus optional OPDS.

inline int positive(const int value) { return std::max(1, value); }

inline int fitMenuRowStep(const int menuRowHeight, const int preferredMenuStep, const int menuRowsAvailable) {
  const int rowHeight = positive(menuRowHeight);
  const int preferredStep = std::max(rowHeight, preferredMenuStep);
  const int fittedStep = std::max(rowHeight, std::max(0, menuRowsAvailable) / MAX_MENU_ITEMS);
  return std::max(rowHeight, std::min(preferredStep, fittedStep));
}

inline int asciiRuleCharacterCount(const int screenWidth, const int sideMargin, const int glyphWidth,
                                   const int maxCharacters) {
  const int availableWidth = std::max(0, positive(screenWidth) - 2 * std::max(0, sideMargin));
  return std::min(std::max(0, maxCharacters), availableWidth / positive(glyphWidth));
}

inline void formatFreeHeapLabel(char* output, const size_t outputSize, const uint32_t freeHeapBytes) {
  if (!output || outputSize == 0) return;
  std::snprintf(output, outputSize, "%uK bytes free", static_cast<unsigned>(freeHeapBytes / 1024));
}

inline Geometry calculate(const int screenWidth, const int screenHeight, const FontMetrics& font,
                          const int requestedRecentBookCount) {
  const int width = positive(screenWidth);
  const int height = positive(screenHeight);
  const int titleLine = positive(font.titleLineHeight);
  const int sectionLine = positive(font.sectionLineHeight);
  const int bodyLine = positive(font.bodyLineHeight);
  const int progressLine = positive(font.progressLineHeight);
  const int hintLine = positive(font.hintLineHeight);
  int smallGap = std::max(2, sectionLine / 4);
  int sectionGap = std::max(4, sectionLine / 2);
  int menuRowPadding = std::max(1, bodyLine / 10);
  int recentBlockGap = std::max(2, std::min(bodyLine / 4, progressLine / 3));
  const int recentLineGap = std::max(1, progressLine / 5);
  const int recentBookCount = std::clamp(requestedRecentBookCount, 0, MAX_RECENT_BOOKS);

  // The header starts at the existing DOS top inset, preserving the status
  // group's battery origin. Its preferred height retains the portrait band;
  // shorter logical orientations use a font/status-derived minimum instead.
  int bottomInset = std::max(1, height / 160);
  const int top = std::max(positive(font.topPadding), bottomInset);
  int preferredHeaderHeight = std::min(84, std::max(1, height / 9));
  int footerPadding = std::max(2, sectionLine / 4);

  auto stackedHeaderHeight = [&]() {
    return positive(font.statusStripHeight) + 4 * smallGap + titleLine + 2 * sectionLine;
  };
  auto headerHeight = [&]() { return std::max(preferredHeaderHeight, stackedHeaderHeight()); };

  // Recent books and Syntax error are packed at fixed font-derived line
  // spacing. Menu rows use one fixed cadence for all four or five actions.
  // When larger SD system fonts leave less room, reduce decorative gaps first.
  auto requiredHeight = [&]() {
    const int recentStep = bodyLine + recentLineGap + progressLine + recentBlockGap;
    // Choose shared gaps and header coordinates against the full four-book
    // layout so title/battery geometry does not shift with the current list size.
    const int recentBottomOffset = (MAX_RECENT_BOOKS - 1) * recentStep + bodyLine + recentLineGap + progressLine;
    const int recentMinimum =
        sectionGap + sectionLine + sectionGap + recentBottomOffset + smallGap + sectionLine + smallGap;
    const int rowHeight = bodyLine + menuRowPadding * 2;
    const int menuMinimum =
        sectionLine + sectionGap + MAX_MENU_ITEMS * rowHeight + (MAX_MENU_ITEMS - 1) * smallGap + 2 * sectionGap;
    return top + headerHeight() + recentMinimum + menuMinimum + sectionLine + hintLine + 2 * footerPadding +
           bottomInset;
  };
  while (requiredHeight() > height && sectionGap > 1) --sectionGap;
  while (requiredHeight() > height && smallGap > 1) --smallGap;
  while (requiredHeight() > height && recentBlockGap > 1) --recentBlockGap;
  while (requiredHeight() > height && menuRowPadding > 0) --menuRowPadding;
  while (requiredHeight() > height && preferredHeaderHeight > stackedHeaderHeight()) --preferredHeaderHeight;
  while (requiredHeight() > height && footerPadding > 1) --footerPadding;
  while (requiredHeight() > height && bottomInset > 1) --bottomInset;

  const int finalHeaderHeight = headerHeight();
  const int headerBottom = std::min(height, top + finalHeaderHeight);
  const int footerHeight = hintLine + footerPadding * 2;
  const int footerY = std::max(headerBottom, height - bottomInset - footerHeight);
  const int promptY = std::max(headerBottom, footerY - sectionGap - sectionLine);
  const int menuRowsBottom = std::max(headerBottom, promptY - sectionGap);

  const int recentProgressOffset = bodyLine + recentLineGap;
  const int minimumRecentRowStep = recentProgressOffset + progressLine + recentBlockGap;
  const int menuRowHeight = bodyLine + menuRowPadding * 2;

  // Keep book blocks tightly packed. The decorative gap after Syntax error is
  // small and the menu remains top-aligned; any unused height stays below it.
  const int recentRowStep = minimumRecentRowStep;

  const int contentX = std::max(1, width * 60 / 1000);
  const int selectionX = std::max(0, width * 36 / 1000);
  const int selectionWidth = std::max(0, width * 920 / 1000);
  const int recentLabelY = headerBottom + sectionGap;
  const int titleY = top + positive(font.statusStripHeight) + smallGap;
  const int memoryY = titleY + titleLine + smallGap;
  const int separatorY = memoryY + sectionLine + smallGap;
  const int recentRowsTop = recentLabelY + sectionLine + sectionGap;
  const int recentRenderedBottom = recentBookCount == 0 ? recentRowsTop + bodyLine
                                                        : recentRowsTop + (recentBookCount - 1) * recentRowStep +
                                                              recentProgressOffset + progressLine;
  const int syntaxErrorY = recentRenderedBottom + smallGap;
  const int menuLabelY = syntaxErrorY + sectionLine + smallGap;
  const int recentBottom = menuLabelY;
  const int menuRowsTop = menuLabelY + sectionLine + sectionGap;
  const int preferredMenuStep = menuRowHeight + smallGap;
  const int menuRowsAvailable = std::max(0, menuRowsBottom - menuRowsTop);
  const int menuRowStep = fitMenuRowStep(menuRowHeight, preferredMenuStep, menuRowsAvailable);
  const int recentContentBottom = syntaxErrorY + sectionLine;
  const int menuRenderedBottom = menuRowsTop + (MAX_MENU_ITEMS - 1) * menuRowStep + menuRowHeight;

  Geometry result{};
  result.header = {0, top, width, std::max(0, headerBottom - top)};
  result.recent = {0, headerBottom, width, std::max(0, recentBottom - headerBottom)};
  result.menu = {0, recentBottom, width, std::max(0, footerY - recentBottom)};
  result.footer = {0, footerY, width, std::max(0, height - footerY)};
  result.titleY = titleY;
  result.memoryY = memoryY;
  result.separatorY = separatorY;
  result.contentX = std::min(contentX, std::max(0, width / 3));
  result.selectionX = selectionX;
  result.selectionWidth = std::min(selectionWidth, std::max(0, width - selectionX));
  result.recentLabelY = recentLabelY;
  result.recentRowsTop = recentRowsTop;
  result.recentRowStep = recentRowStep;
  result.recentProgressOffset = recentProgressOffset;
  result.recentBookCount = recentBookCount;
  result.syntaxErrorY = syntaxErrorY;
  result.menuLabelY = menuLabelY;
  result.menuRowsTop = menuRowsTop;
  result.menuRowHeight = menuRowHeight;
  result.menuPreferredRowStep = preferredMenuStep;
  result.menuRowStep = menuRowStep;
  result.menuRowsBottom = menuRowsBottom;
  result.promptY = promptY;
  result.sectionGap = sectionGap;
  result.contentFits =
      recentContentBottom <= menuLabelY && menuRenderedBottom <= menuRowsBottom && menuRowStep >= menuRowHeight;
  return result;
}

inline int recentBookLimit(const int screenWidth, const int screenHeight, const FontMetrics& font) {
  for (int recentCount = MAX_RECENT_BOOKS; recentCount >= 0; --recentCount) {
    if (calculate(screenWidth, screenHeight, font, recentCount).contentFits) return recentCount;
  }
  return 0;
}

inline int menuRowStep(const Geometry& layout, const int /*itemCount*/) { return layout.menuRowStep; }

inline int menuRowStep(const int menuRowHeight, const int preferredMenuStep, const int menuRowsAvailable) {
  return fitMenuRowStep(menuRowHeight, preferredMenuStep, menuRowsAvailable);
}

inline int menuRowHeight(const Geometry& layout, const int /*itemCount*/) { return layout.menuRowHeight; }

inline Box recentTitleSelection(const int selectionX, const int selectionWidth, const int recentTop,
                                const int recentBottom, const int rowY, const int titleLineHeight, const int progressY,
                                const int sectionGap) {
  const int padding = std::max(1, sectionGap / 2);
  const int top = std::max(recentTop, rowY - padding);
  const int bottom = std::min({recentBottom, rowY + positive(titleLineHeight) + padding, progressY});
  return {selectionX, top, selectionWidth, std::max(0, bottom - top)};
}

inline ProgressLine progressLine(const int screenWidth, const int rightEdge, const int leftLimit, const int hashWidth,
                                 const int dotWidth, const int openWidth, const int closeWidth,
                                 const int percentageColumnWidth, const int requestedGap, const int maxSegments = 20) {
  const int width = positive(screenWidth);
  const int left = std::clamp(leftLimit, 0, width);
  const int right = std::clamp(rightEdge, left, width);
  const int cellWidth = std::max(1, std::max(hashWidth, dotWidth));
  const int percentWidth = std::min(std::max(0, percentageColumnWidth), right - left);
  const int percentX = right - percentWidth;
  const int gap = std::min(std::max(0, requestedGap), std::max(0, percentX - left));
  const int widthForBar = std::max(0, percentX - left - gap);
  const int fitSegments = std::max(0, (widthForBar - std::max(0, openWidth) - std::max(0, closeWidth)) / cellWidth);
  const int segments = std::min(std::max(0, maxSegments), fitSegments);
  const int barWidth = std::max(0, openWidth) + cellWidth * segments + std::max(0, closeWidth);
  return {percentX - gap - barWidth, barWidth, cellWidth, segments, percentX, percentWidth, gap};
}

}  // namespace dos_layout
