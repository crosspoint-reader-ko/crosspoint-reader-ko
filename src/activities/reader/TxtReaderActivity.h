#pragma once

#include <Txt.h>

#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"
#include "TxtReaderMenuActivity.h"
#include "util/ReadingTimer.h"

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;

  // Offset-based navigation: no whole-file pagination. We track the byte
  // offset of the page currently on screen and the offset where the next
  // page starts (computed as a side-effect of rendering). Previous-page
  // navigation uses a bounded history stack; going deeper than the stack
  // falls back to a coarse backward scan.
  size_t currentOffset = 0;     // byte offset of current page start
  size_t currentEndOffset = 0;  // byte offset where current page ends (= next page start)
  std::vector<size_t> backHistory;
  static constexpr size_t MAX_BACK_HISTORY = 256;

  // Page-count estimate: sampled from the first rendered page so the status
  // bar can show N/M + % without scanning the whole file. Refined lazily.
  size_t estBytesPerPage = 0;

  size_t fileSize = 0;

  // End-of-book sentinel for the ReaderActivity end-of-book screen. Set when a
  // forward turn is requested from the last page; currentOffset keeps pointing
  // at that last page so returning from the end screen needs no re-layout.
  bool atEndOfBook = false;

  std::vector<std::string> currentPageLines;
  // Parallel to currentPageLines: true if this line ended a source-file line
  // (paragraph break) and therefore should render left-aligned even in
  // justified mode. False for soft-wrapped continuations which can be spread
  // to fill the viewport.
  std::vector<bool> currentPageLineEndsParagraph;
  // Parallel to currentPageLines: true if this line is the first wrapped
  // segment of a source-file line (paragraph start). Used by paragraphIndent
  // and to suppress double extra-spacing on the page's leading line.
  std::vector<bool> currentPageLineStartsParagraph;
  int maxLinesPerPage = 0;  // Upper bound; actual lines per page is height-driven.
  int viewportWidth = 0;
  int viewportHeight = 0;
  bool initialized = false;
  // True once loadProgress() has run for this open file. Settings changes
  // recompute the layout but must NOT call loadProgress again — that would
  // reset currentOffset to 0 if the new settings don't match the saved ones.
  bool progressLoaded = false;

  // Cached settings for invalidating saved progress on layout changes
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  uint8_t cachedCharacterWrap = 0;
  uint8_t cachedExtraParagraphSpacing = 0;
  uint8_t cachedParagraphIndent = 0;
  float cachedLineCompression = 1.0f;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;
  int paragraphIndentPx = 0;
  int paragraphSpacingPx = 0;

  // Reader-menu-driven options (ephemeral; not persisted to global settings)
  bool automaticPageTurnActive = false;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  uint8_t currentPageTurnOption = 0;  // index into the menu's pageTurnLabels
  uint8_t currentPageJumpOption = 0;  // index into the menu's pageJumpLabels (0 = off)
  bool pendingScreenshot = false;
  // Suppress one frame of input after returning from a sub-activity so the
  // release event that closed the menu doesn't immediately retrigger here.
  bool skipNextButtonCheck = false;

  // Per-book reading-time accumulator. See util/ReadingTimer.h.
  ReadingTimer readingTimer;

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  void recomputeLayout();
  // Returns true if the byte at offset starts a source line (i.e. previous
  // byte is '\n' or offset == 0). Used to decide whether the page's leading
  // line is the start of a new paragraph for indent/spacing purposes.
  bool isOffsetAtLineStart(size_t offset) const;
  bool loadPageAtOffset(size_t offset, bool firstLineIsParagraphStart, std::vector<std::string>& outLines,
                        std::vector<bool>* outEndsParagraph, std::vector<bool>* outStartsParagraph, size_t& nextOffset);
  size_t snapToLineStart(size_t offset) const;
  size_t findBackwardPageStart(size_t endOffset) const;
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  void renderBook() override;

  int estimatedTotalPages() const;
  int estimatedCurrentPage() const;

  // Menu-driven actions
  void openReaderMenu();
  void onReaderMenuConfirm(TxtReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  // Multi-page navigation. Positive delta moves forward, negative backward.
  // Uses the running bytes-per-page estimate to skip without re-paginating.
  bool jumpPages(int deltaPages);
  // Translate a 0-100 percent into an absolute byte offset and reset state to
  // render that position on the next frame.
  void jumpToPercent(int percent);

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~TxtReaderActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // Prevent auto-sleep while auto page-turn is running so long unattended
  // reads don't get cut off by the global inactivity timer.
  bool preventAutoSleep() override { return automaticPageTurnActive; }

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
