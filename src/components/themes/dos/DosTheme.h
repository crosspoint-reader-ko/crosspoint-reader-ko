#pragma once

#include "components/themes/BaseTheme.h"

namespace DosMetrics {
constexpr int MAX_RECENT_BOOKS = 4;
constexpr ThemeMetrics values = [] {
  ThemeMetrics metrics = BaseMetrics::values;
  metrics.topPadding = 12;
  metrics.headerSidePadding = 18;
  metrics.headerTitleAlign = 1;
  metrics.headerClockCentered = true;
  metrics.contentSidePadding = 12;
  metrics.homeRecentBooksCount = MAX_RECENT_BOOKS;
  metrics.homeContinueReadingInMenu = false;
  metrics.menuRowHeight = 36;
  metrics.menuSpacing = 0;
  return metrics;
}();
}  // namespace DosMetrics

struct DosHomeLayout {
  DosHomeLayout()
      : header(0, 0, 0, 0),
        recent(0, 0, 0, 0),
        menu(0, 0, 0, 0),
        footer(0, 0, 0, 0),
        titleY(0),
        memoryY(0),
        separatorY(0),
        contentX(0),
        selectionX(0),
        selectionWidth(0),
        recentLabelY(0),
        recentRowsTop(0),
        recentRowStep(0),
        recentProgressOffset(0),
        recentBookCount(0),
        syntaxErrorY(0),
        menuLabelY(0),
        menuRowsTop(0),
        menuRowHeight(0),
        menuRowStep(0),
        menuPreferredRowStep(0),
        menuRowsBottom(0),
        sectionGap(0),
        promptY(0) {}

  Rect header;
  Rect recent;
  Rect menu;
  Rect footer;
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
  int menuRowStep;
  int menuPreferredRowStep;
  int menuRowsBottom;
  int sectionGap;
  int promptY;
};

class DosTheme final : public BaseTheme {
 public:
  static DosHomeLayout homeLayout(const GfxRenderer& renderer, int recentBookCount = -1);
  static int recentBooksLimit(const GfxRenderer& renderer);
  static int homeMenuRowHeight(const GfxRenderer& renderer, int itemCount, int recentBookCount = -1);
  static int homeMenuRowStep(const GfxRenderer& renderer, int itemCount, int recentBookCount = -1);
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawHomeRecentBooks(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, const int* progress, size_t progressCount) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  bool usesHomeCoverImages() const override { return false; }
};
