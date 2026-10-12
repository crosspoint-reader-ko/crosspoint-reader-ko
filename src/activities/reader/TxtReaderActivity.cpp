#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "TxtReaderMenuActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Progress file magic and version. No longer stores a page-offset index — only
// the current byte offset plus the layout settings used to produce it, so we
// can reset to the start if the user changed font/margin/etc. since last read.
constexpr uint32_t PROGRESS_MAGIC = 0x54585450;  // "TXTP"
constexpr uint8_t PROGRESS_VERSION = 2;          // v2 adds extraParagraphSpacing + paragraphIndent

// Auto page-turn options in pages-per-minute. Index 0 disables; the rest mirror
// the EPUB reader's choices so users see consistent values across formats.
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr size_t PAGE_TURN_RATES_COUNT = sizeof(PAGE_TURN_RATES) / sizeof(PAGE_TURN_RATES[0]);

// Long-press multi-page jump steps. Index 0 disables (long-press behaves like
// a normal page turn); other entries become the jump distance.
constexpr int PAGE_JUMP_STEPS[] = {0, 10, 20, 50, 100};
constexpr size_t PAGE_JUMP_STEPS_COUNT = sizeof(PAGE_JUMP_STEPS) / sizeof(PAGE_JUMP_STEPS[0]);

// Held duration above which a navigation button release counts as a "jump"
// instead of a single page turn. Matches the GO_HOME_MS feel so the user only
// has one threshold to learn.
constexpr unsigned long PAGE_JUMP_HOLD_MS = 1000;

int clampPercent(int percent) {
  if (percent < 0) return 0;
  if (percent > 100) return 100;
  return percent;
}

// Find UTF-8 character boundary at or before pos
size_t findUtf8Boundary(const std::string& str, size_t pos) {
  if (pos >= str.length()) return str.length();
  // Move back if we're in the middle of a UTF-8 sequence
  while (pos > 0 && (str[pos] & 0xC0) == 0x80) {
    pos--;
  }
  return pos;
}

// Binary search to find max characters that fit in width
// Returns the position (byte offset) where to break the string
size_t findBreakPosition(const GfxRenderer& renderer, int fontId, const std::string& line, int maxWidth) {
  if (line.empty()) return 0;

  // First check if the whole line fits
  int fullWidth = renderer.getTextWidth(fontId, line.c_str());
  if (fullWidth <= maxWidth) {
    return line.length();
  }

  // Minimum advance is one whole UTF-8 codepoint. Splitting mid-sequence here
  // produces invalid bytes at the next page's head and desynchronizes the
  // byte-offset navigation.
  size_t firstCharEnd = 1;
  while (firstCharEnd < line.length() && (static_cast<unsigned char>(line[firstCharEnd]) & 0xC0) == 0x80) {
    firstCharEnd++;
  }

  // Binary search for the break point
  size_t low = firstCharEnd;
  size_t high = line.length();
  size_t bestFit = firstCharEnd;

  while (low < high) {
    size_t mid = (low + high + 1) / 2;
    mid = findUtf8Boundary(line, mid);

    if (mid <= low) {
      // Can't make progress, exit
      break;
    }

    std::string substr = line.substr(0, mid);
    int width = renderer.getTextWidth(fontId, substr.c_str());

    if (width <= maxWidth) {
      bestFit = mid;
      low = mid;
    } else {
      high = mid - 1;
      if (high > 0) {
        high = findUtf8Boundary(line, high);
      }
    }
  }

  // Try to break at word boundary (space) if possible, unless character wrap is enabled
  if (!SETTINGS.characterWrap && bestFit > 0 && bestFit < line.length()) {
    size_t spacePos = line.rfind(' ', bestFit);
    if (spacePos != std::string::npos && spacePos > 0) {
      // Check if breaking at space still fits
      std::string atSpace = line.substr(0, spacePos);
      if (renderer.getTextWidth(fontId, atSpace.c_str()) <= maxWidth) {
        return spacePos;
      }
    }
  }

  return bestFit;  // At minimum, consume one whole UTF-8 codepoint
}
}  // namespace

bool TxtReaderActivity::loadBook() {
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "Failed to allocate TXT object");
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT");
    txt.reset();
    return false;
  }
  txt->setupCacheDir();
  return true;
}

void TxtReaderActivity::onEnter() {
  ReaderActivity::onEnter();

  // Begin per-book reading time accumulation (only when the book actually loaded;
  // ReaderActivity::onEnter() already finished this activity otherwise).
  if (txt) {
    readingTimer.start(txt->getCachePath());
  }
}

void TxtReaderActivity::onExit() {
  ReaderActivity::onExit();

  // Persist accumulated session time before tearing down the file.
  readingTimer.stop();

  backHistory.clear();
  currentPageLines.clear();
  currentPageLineEndsParagraph.clear();
  currentPageLineStartsParagraph.clear();
  txt.reset();
}

void TxtReaderActivity::openReaderMenu() {
  automaticPageTurnActive = false;
  const float progress = fileSize > 0 ? (currentOffset * 100.0f / fileSize) : 0.0f;
  const int bookProgressPercent = clampPercent(static_cast<int>(progress + 0.5f));
  // Snapshot orientation: Reader Options (sub-activity of the menu) writes
  // directly to SETTINGS.orientation, while menu.orientation is the inline
  // rotate-cycle value. Whichever differs from the snapshot is the user's
  // intent.
  const uint8_t menuOrientationSnapshot = SETTINGS.orientation;
  startActivityForResult(std::make_unique<TxtReaderMenuActivity>(
                             renderer, mappedInput, txt ? txt->getTitle() : std::string(), estimatedCurrentPage(),
                             estimatedTotalPages(), bookProgressPercent, SETTINGS.orientation, currentPageTurnOption,
                             currentPageJumpOption, readingTimer.totalSeconds()),
                         [this, menuOrientationSnapshot](const ActivityResult& result) {
                           const auto& menu = std::get<MenuResult>(result.data);
                           uint8_t finalOrientation = SETTINGS.orientation;
                           if (menu.orientation != menuOrientationSnapshot) {
                             finalOrientation = menu.orientation;
                           }
                           SETTINGS.orientation = menuOrientationSnapshot;
                           applyOrientation(finalOrientation);
                           toggleAutoPageTurn(menu.pageTurnOption);
                           currentPageJumpOption = menu.pageJumpOption;
                           skipNextButtonCheck = true;
                           readingTimer.notifyInput();
                           if (!result.isCancelled) {
                             onReaderMenuConfirm(static_cast<TxtReaderMenuActivity::MenuAction>(menu.action));
                           } else {
                             requestUpdate();
                           }
                         });
}

void TxtReaderActivity::loop() {
  if (!txt) {
    return;
  }

  rememberBookOnceRendered();

  // Skip one frame after a sub-activity returns so the release event that
  // closed it doesn't fall through into navigation here.
  if (skipNextButtonCheck) {
    skipNextButtonCheck = false;
    return;
  }

  // Accumulate reading time. See util/ReadingTimer.h for idle/save semantics.
  readingTimer.tick();

  clearEndOfBookOptionsIfNeeded();

  // Auto page turn handling — fire pageTurn(true) on the configured cadence
  // and let any user input cancel it.
  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu ||
        ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
      automaticPageTurnActive = false;
      readingTimer.notifyInput();
      requestUpdate();
      return;
    }
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }
    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      // Auto-turn counts as activity so 5-min idle pause doesn't suspend
      // unattended hands-free reading on the user's chosen cadence.
      readingTimer.notifyInput();
      if (pageTurn(true)) {
        requestUpdate();
      }
      return;
    }
  }

  // The end-of-book suggestion menu owns Confirm/Back/navigation while shown.
  if (handleEndOfBookMenu()) {
    readingTimer.notifyInput();
    return;
  }

  // Open the reader menu on Confirm release, the touch menu gesture, or the
  // home key's reader-menu action.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    readingTimer.notifyInput();
    openReaderMenu();
    return;
  }

  // Back navigation: ReaderUtils swaps short/long press targets per
  // SETTINGS.backShortToFileBrowser.
  if (handleBackNavigation()) {
    readingTimer.notifyInput();
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  // Long-press multi-page jump on navigation buttons. When enabled, switch to
  // release-based detection so we can distinguish short tap (single page) from
  // long hold (jumpPages step). When disabled, fall through to the default
  // press-based path below to preserve original snappy feel.
  if (currentPageJumpOption > 0 && currentPageJumpOption < PAGE_JUMP_STEPS_COUNT) {
    const bool swapFront = mappedInput.isNavDirectionSwapped();
    const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
    const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
    const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                              mappedInput.wasReleased(prevButton) || touch.prev;
    const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                              mappedInput.wasReleased(nextButton) || touch.next;
    if (prevReleased || nextReleased) {
      readingTimer.notifyInput();
      if (handleEndOfBookPageTurn(prevReleased, nextReleased)) {
        return;
      }
      const unsigned long held = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
      const int step = (held >= PAGE_JUMP_HOLD_MS) ? PAGE_JUMP_STEPS[currentPageJumpOption] : 1;
      const bool changed = step == 1 ? pageTurn(nextReleased) : jumpPages(nextReleased ? step : -step);
      if (changed) {
        requestUpdate();
      }
      return;
    }
    // Power button / home key are always release-driven, so honor their
    // configured page-turn role even while we're intercepting nav-button presses.
    if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
         mappedInput.wasReleased(MappedInputManager::Button::Power)) ||
        mappedInput.homeButtonAction() == HomeButtonAction::NextPage) {
      readingTimer.notifyInput();
      if (handleEndOfBookPageTurn(false, true)) {
        return;
      }
      if (pageTurn(true)) {
        requestUpdate();
      }
    }
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  readingTimer.notifyInput();
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) {
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  bool changed;
  if (prevTriggered) {
    changed = skip ? skipPages(-10) : pageTurn(false);
  } else {
    changed = skip ? skipPages(10) : pageTurn(true);
  }
  if (changed) {
    requestUpdate();
  }
}

bool TxtReaderActivity::pageTurn(const bool isForward) {
  // Ignore paging until initializeReader has established the layout.
  if (!initialized || atEndOfBook) {
    return false;
  }

  if (!isForward) {
    if (currentOffset == 0) {
      return false;  // already at start
    }
    if (!backHistory.empty()) {
      currentOffset = backHistory.back();
      backHistory.pop_back();
    } else {
      currentOffset = findBackwardPageStart(currentOffset);
    }
    lastPageTurnTime = millis();
    return true;
  }

  if (currentEndOffset >= fileSize) {
    // Past the last page: hand over to the shared end-of-book screen.
    automaticPageTurnActive = false;
    atEndOfBook = true;
    return true;
  }
  // Push the page we're leaving so Back can return to it, capped so we
  // don't grow forever on very long reads.
  if (backHistory.size() >= MAX_BACK_HISTORY) {
    backHistory.erase(backHistory.begin(), backHistory.begin() + (MAX_BACK_HISTORY / 4));
  }
  backHistory.push_back(currentOffset);
  currentOffset = currentEndOffset;
  lastPageTurnTime = millis();
  return true;
}

bool TxtReaderActivity::skipPages(const int amount) {
  if (!initialized || atEndOfBook) {
    return false;
  }
  return jumpPages(amount);
}

bool TxtReaderActivity::isAtEndOfBook() const { return initialized && atEndOfBook; }

void TxtReaderActivity::onReturnFromEndOfBook() {
  // currentOffset still points at the last page; just leave the end screen.
  atEndOfBook = false;
}

bool TxtReaderActivity::jumpPages(const int deltaPages) {
  if (deltaPages == 0 || fileSize == 0) {
    return false;
  }
  // Use the running per-page byte estimate so we can skip without paginating.
  // Without an estimate (very first frame) bail out instead of guessing wildly.
  const size_t perPage = estBytesPerPage > 0 ? estBytesPerPage : 0;
  if (perPage == 0) {
    return pageTurn(deltaPages > 0);
  }

  size_t target;
  if (deltaPages > 0) {
    const size_t deltaBytes = perPage * static_cast<size_t>(deltaPages);
    target = (currentOffset + deltaBytes >= fileSize) ? fileSize - 1 : currentOffset + deltaBytes;
  } else {
    const size_t deltaBytes = perPage * static_cast<size_t>(-deltaPages);
    target = (currentOffset > deltaBytes) ? currentOffset - deltaBytes : 0;
  }
  target = snapToLineStart(target);

  if (target == currentOffset) {
    return false;
  }
  // Only a forward jump lets Back return to the pre-jump page. On a backward
  // jump, pushing the pre-jump offset would make the next PageBack jump
  // forward — the opposite of what the user just asked for. Wipe history so
  // Back keeps moving backward page-by-page.
  if (deltaPages > 0) {
    if (backHistory.size() >= MAX_BACK_HISTORY) {
      backHistory.erase(backHistory.begin(), backHistory.begin() + (MAX_BACK_HISTORY / 4));
    }
    backHistory.push_back(currentOffset);
  } else {
    backHistory.clear();
  }
  currentOffset = target;
  currentEndOffset = target;
  lastPageTurnTime = millis();
  return true;
}

void TxtReaderActivity::jumpToPercent(int percent) {
  if (fileSize == 0) {
    return;
  }
  percent = clampPercent(percent);

  // Overflow-safe (fileSize/100)*percent + (fileSize%100)*percent/100.
  size_t target =
      (fileSize / 100) * static_cast<size_t>(percent) + (fileSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    target = fileSize - 1;
  }
  target = snapToLineStart(target);

  // Reset history so backward navigation after a jump uses backward-scan,
  // matching the EPUB jump behavior of an effective fresh location.
  backHistory.clear();
  atEndOfBook = false;
  currentOffset = target;
  currentEndOffset = target;
  lastPageTurnTime = millis();
  requestUpdate();
}

void TxtReaderActivity::applyOrientation(const uint8_t orientation) {
  if (SETTINGS.orientation == orientation) {
    return;
  }
  SETTINGS.orientation = orientation;
  SETTINGS.saveToFile();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  // Force the layout to recompute viewport + lines per page on next render.
  initialized = false;
  currentOffset = snapToLineStart(currentOffset);
  currentEndOffset = currentOffset;
  backHistory.clear();
  estBytesPerPage = 0;
  requestUpdate();
}

void TxtReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  currentPageTurnOption = selectedPageTurnOption;
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= PAGE_TURN_RATES_COUNT) {
    automaticPageTurnActive = false;
    return;
  }
  lastPageTurnTime = millis();
  pageTurnDuration = (1UL * 60 * 1000) / static_cast<unsigned long>(PAGE_TURN_RATES[selectedPageTurnOption]);
  automaticPageTurnActive = true;
  requestUpdate();
}

void TxtReaderActivity::onReaderMenuConfirm(const TxtReaderMenuActivity::MenuAction action) {
  switch (action) {
    case TxtReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      const float progress = fileSize > 0 ? (currentOffset * 100.0f / fileSize) : 0.0f;
      const int initialPercent = clampPercent(static_cast<int>(progress + 0.5f));
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            skipNextButtonCheck = true;
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            } else {
              requestUpdate();
            }
          });
      break;
    }
    case TxtReaderMenuActivity::MenuAction::SCREENSHOT: {
      pendingScreenshot = true;
      requestUpdate();
      break;
    }
    case TxtReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case TxtReaderMenuActivity::MenuAction::RESET_READING_TIMER: {
      // Two-step destructive action: prompt before zeroing the cumulative time.
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_READING_TIMER),
                                                                    tr(STR_RESET_READING_TIMER_PROMPT)),
                             [this](const ActivityResult& result) {
                               skipNextButtonCheck = true;
                               readingTimer.notifyInput();
                               if (!result.isCancelled) {
                                 readingTimer.reset();
                               }
                               requestUpdate();
                             });
      break;
    }
    case TxtReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
    case TxtReaderMenuActivity::MenuAction::PAGE_JUMP_STEP:
    case TxtReaderMenuActivity::MenuAction::ROTATE_SCREEN:
      // Inline-cycle options are applied via menu.orientation /
      // menu.pageTurnOption / menu.pageJumpOption already consumed above.
      requestUpdate();
      break;
    case TxtReaderMenuActivity::MenuAction::READER_OPTIONS:
      // Reader Options is a sub-activity of the menu, never dispatched here.
      // Layout changes are auto-detected by recomputeLayout() on next render;
      // orientation is reconciled in the menu's result handler in openReaderMenu().
      break;
  }
}

void TxtReaderActivity::recomputeLayout() {
  // Store current settings for progress validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;
  cachedCharacterWrap = SETTINGS.characterWrap;
  cachedExtraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  cachedParagraphIndent = SETTINGS.paragraphIndent;
  cachedLineCompression = SETTINGS.getReaderLineCompression();

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId) * cachedLineCompression;

  // Indent ≈ 1em (one full font height), spacing ≈ half a line. These match
  // the visual feel of EPUB text-indent/paragraph-margin defaults.
  paragraphIndentPx = cachedParagraphIndent ? renderer.getLineHeight(cachedFontId) : 0;
  paragraphSpacingPx = cachedExtraParagraphSpacing ? lineHeight / 2 : 0;

  // Conservative upper bound — actual lines per page is determined by
  // accumulated y in loadPageAtOffset.
  maxLinesPerPage = viewportHeight / lineHeight;
  if (maxLinesPerPage < 1) maxLinesPerPage = 1;

  fileSize = txt->getFileSize();
  LOG_DBG("TRS", "Viewport: %dx%d, max lines/page: %d, indent=%d, spacing=%d, file: %zu bytes", viewportWidth,
          viewportHeight, maxLinesPerPage, paragraphIndentPx, paragraphSpacingPx, fileSize);
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  recomputeLayout();

  // Load saved offset only on first init for this file. Subsequent
  // settings-driven re-layouts must NOT call loadProgress — its validation
  // would either accept the saved offset (cache hit) or reset to 0 on a
  // mismatch, losing the user's reading position.
  if (!progressLoaded) {
    loadProgress();
    progressLoaded = true;
  }

  initialized = true;
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, bool firstLineIsParagraphStart,
                                         std::vector<std::string>& outLines, std::vector<bool>* outEndsParagraph,
                                         std::vector<bool>* outStartsParagraph, size_t& nextOffset) {
  outLines.clear();
  if (outEndsParagraph) outEndsParagraph->clear();
  if (outStartsParagraph) outStartsParagraph->clear();
  const size_t totalSize = fileSize ? fileSize : txt->getFileSize();

  if (offset >= totalSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, totalSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // SD-card (.epdfont) fonts stream glyph metrics on demand via SdFont's own glyph cache;
  // no separate advance-table prewarm is needed here.

  const int lineHeight = renderer.getLineHeight(cachedFontId) * cachedLineCompression;
  // Track accumulated y to enforce height-based pagination. Extra paragraph
  // spacing is added BEFORE the leading line of each new paragraph (except
  // the first line of the page) so partial-line wraps stay tight.
  int accumulatedY = 0;
  bool isFirstSourceLineOnPage = true;

  size_t pos = 0;

  // Helper: try to add a wrapped segment. Returns true if it fit, false if
  // adding it would overflow the viewport (caller must stop).
  auto tryAddLine = [&](const std::string& seg, bool endsParagraph, bool startsParagraph,
                        bool needsExtraSpacing) -> bool {
    int linePixelHeight = lineHeight + (needsExtraSpacing ? paragraphSpacingPx : 0);
    if (accumulatedY + linePixelHeight > viewportHeight && !outLines.empty()) {
      return false;
    }
    outLines.push_back(seg);
    if (outEndsParagraph) outEndsParagraph->push_back(endsParagraph);
    if (outStartsParagraph) outStartsParagraph->push_back(startsParagraph);
    accumulatedY += linePixelHeight;
    return true;
  };

  while (pos < chunkSize && static_cast<int>(outLines.size()) < maxLinesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= totalSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    // This source line starts a new paragraph if it's not the leading line
    // of the page (those are continuations of the previous source line in
    // the byte stream — caller tells us whether the leading source line is
    // a fresh paragraph via firstLineIsParagraphStart).
    const bool sourceLineStartsParagraph = isFirstSourceLineOnPage ? firstLineIsParagraphStart : true;
    // Extra spacing precedes a paragraph that is NOT the page's first.
    const bool needsExtraSpacingBefore = cachedExtraParagraphSpacing && sourceLineStartsParagraph && !outLines.empty();

    // First wrapped segment of this source line gets paragraph indent if
    // this source line begins a paragraph.
    const int firstSegmentIndent = (cachedParagraphIndent && sourceLineStartsParagraph) ? paragraphIndentPx : 0;

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;
    bool isFirstSegmentOfSourceLine = true;

    if (line.empty()) {
      // Blank source line — emit one empty visual line to preserve paragraph whitespace.
      tryAddLine("", true, isFirstSegmentOfSourceLine && sourceLineStartsParagraph, needsExtraSpacingBefore);
    } else {
      bool extraSpacingApplied = false;
      // Word wrap if needed - use binary search for performance with SD fonts.
      // The size guard is a defensive belt-and-braces check; tryAddLine() also
      // gates on capacity and will break out when full.
      // cppcheck-suppress knownConditionTrueFalse
      while (!line.empty() && static_cast<int>(outLines.size()) < maxLinesPerPage) {
        const int effectiveWidth = isFirstSegmentOfSourceLine ? viewportWidth - firstSegmentIndent : viewportWidth;
        // Use binary search to find break position (much faster than linear search)
        size_t breakPos = findBreakPosition(renderer, cachedFontId, line, effectiveWidth);

        const bool needsSpacing = needsExtraSpacingBefore && isFirstSegmentOfSourceLine && !extraSpacingApplied;

        if (breakPos >= line.length()) {
          // Whole line fits character-wise — this is the last segment of a
          // source line, so it marks the end of a paragraph (the next line in
          // the source starts a new paragraph).
          if (!tryAddLine(line, true, isFirstSegmentOfSourceLine && sourceLineStartsParagraph, needsSpacing)) {
            // Failed at viewport check. Break out so the partial-consumption
            // path below advances pos by lineBytePos (any prior wrapped
            // segments of this source line that we already added) — using
            // goto here would skip that and the next page would re-render
            // those segments.
            break;
          }
          if (needsSpacing) extraSpacingApplied = true;
          lineBytePos = displayLen;
          line.clear();
          break;
        }

        if (breakPos == 0) {
          breakPos = 1;  // Ensure progress
        }

        if (!tryAddLine(line.substr(0, breakPos), false, isFirstSegmentOfSourceLine && sourceLineStartsParagraph,
                        needsSpacing)) {
          // Same rationale as above: prior wrapped segments may already be in
          // outLines, so we must advance pos by lineBytePos rather than
          // jumping over the partial-consumption update.
          break;
        }
        if (needsSpacing) extraSpacingApplied = true;
        isFirstSegmentOfSourceLine = false;

        // Skip space at break point
        size_t skipChars = breakPos;
        if (breakPos < line.length() && line[breakPos] == ' ') {
          skipChars++;
        }
        lineBytePos += skipChars;
        line = line.substr(skipChars);
      }
    }

    // Determine how much of the source buffer we consumed
    if (line.empty()) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
    isFirstSourceLineOnPage = false;
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > totalSize) {
    nextOffset = totalSize;
  }

  free(buffer);

  return !outLines.empty();
}

bool TxtReaderActivity::isOffsetAtLineStart(size_t offset) const {
  if (offset == 0) return true;
  uint8_t prev = 0;
  if (!txt->readContent(&prev, offset - 1, 1)) {
    // Read failure: assume paragraph start so indent/spacing applies (visual
    // glitch is less harmful than missing the indent on every page).
    return true;
  }
  return prev == '\n';
}

size_t TxtReaderActivity::snapToLineStart(size_t offset) const {
  if (offset == 0 || offset >= fileSize) return offset;

  // Look back for the nearest '\n' + 1. Read at most one chunk worth.
  const size_t scanLen = std::min(static_cast<size_t>(CHUNK_SIZE), offset);
  const size_t scanStart = offset - scanLen;

  auto* buf = static_cast<uint8_t*>(malloc(scanLen));
  if (!buf) return offset;
  if (!txt->readContent(buf, scanStart, scanLen)) {
    free(buf);
    return offset;
  }

  size_t snapped = scanStart;
  for (size_t i = scanLen; i > 0; i--) {
    if (buf[i - 1] == '\n') {
      snapped = scanStart + i;  // position right after '\n'
      break;
    }
  }
  free(buf);
  return snapped;
}

size_t TxtReaderActivity::findBackwardPageStart(size_t endOffset) const {
  // No history cache to fall back on — reconstruct the previous page by
  // walking forward from a guessed earlier position until we reach endOffset.
  // Estimate the guess window from bytes-per-page seen so far so long books
  // don't need a full scan.
  const size_t perPage = estBytesPerPage > 0 ? estBytesPerPage : 2048;
  const size_t windowBytes = perPage * 2;
  size_t scanStart = endOffset > windowBytes ? endOffset - windowBytes : 0;
  scanStart = snapToLineStart(scanStart);

  std::vector<std::string> lines;
  size_t cursor = scanStart;
  size_t lastStart = scanStart;

  // Walk forward page-by-page. The last page-start at or before endOffset
  // is our answer.
  while (cursor < endOffset) {
    size_t next = cursor;
    // The leading source line is a paragraph start only when cursor actually
    // sits at a line boundary. Only the first cursor (snapToLineStart above) is
    // guaranteed to be one; every subsequent cursor is the previous synthetic
    // page's end, which usually lands mid-paragraph. Forward rendering decides
    // this via isOffsetAtLineStart() (see render()), so mirror it exactly here —
    // otherwise the reconstructed pages get different paragraph spacing/indent,
    // their height-based boundaries drift from the forward pages, and Back lands
    // on a misaligned offset (issue #17 "text shifted to a different part").
    const bool firstLineIsParagraphStart = isOffsetAtLineStart(cursor);
    if (!const_cast<TxtReaderActivity*>(this)->loadPageAtOffset(cursor, firstLineIsParagraphStart, lines, nullptr,
                                                                nullptr, next)) {
      break;
    }
    if (next <= cursor) break;
    if (next >= endOffset) {
      lastStart = cursor;
      break;
    }
    lastStart = cursor;
    cursor = next;
  }
  return lastStart;
}

int TxtReaderActivity::estimatedTotalPages() const {
  if (fileSize == 0 || estBytesPerPage == 0) return 1;
  int n = static_cast<int>((fileSize + estBytesPerPage - 1) / estBytesPerPage);
  return n > 0 ? n : 1;
}

int TxtReaderActivity::estimatedCurrentPage() const {
  if (estBytesPerPage == 0) return 1;
  return static_cast<int>(currentOffset / estBytesPerPage) + 1;
}

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  // Check if font or settings changed since initialization
  if (initialized) {
    const int currentFontId = SETTINGS.getReaderFontId();
    const int currentMargin = SETTINGS.screenMargin;
    const uint8_t currentAlignment = SETTINGS.paragraphAlignment;
    const uint8_t currentCharacterWrap = SETTINGS.characterWrap;
    const uint8_t currentExtraSpacing = SETTINGS.extraParagraphSpacing;
    const uint8_t currentParagraphIndent = SETTINGS.paragraphIndent;
    const float currentLineCompression = SETTINGS.getReaderLineCompression();

    if (currentFontId != cachedFontId || currentMargin != cachedScreenMargin ||
        currentAlignment != cachedParagraphAlignment || currentCharacterWrap != cachedCharacterWrap ||
        currentExtraSpacing != cachedExtraParagraphSpacing || currentParagraphIndent != cachedParagraphIndent ||
        currentLineCompression != cachedLineCompression) {
      LOG_DBG("TRS", "Settings changed, recomputing layout (font: %d->%d)", cachedFontId, currentFontId);
      // Keep currentOffset (the user's reading position), but snap to the
      // previous line boundary so the new layout doesn't render a partial
      // line at the top. Critically: do NOT call loadProgress — that would
      // reset the offset to 0 on a settings mismatch.
      recomputeLayout();
      currentOffset = snapToLineStart(currentOffset);
      currentEndOffset = currentOffset;
      backHistory.clear();
      estBytesPerPage = 0;
    }
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (fileSize == 0) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  // Recover from an out-of-range offset (e.g. settings change shrinking the
  // effective reachable position). snapToLineStart(fileSize) returns fileSize
  // itself and would leave loadPageAtOffset with nothing to read, so fall back
  // to the last actual page start. The fileSize == 0 case was handled above.
  if (currentOffset >= fileSize) {
    currentOffset = findBackwardPageStart(fileSize);
    currentEndOffset = currentOffset;
  }

  LOG_DBG("TRS", "Rendering page at offset %zu", currentOffset);

  // Load current page content and remember where the next page starts.
  size_t nextOffset = currentOffset;
  currentPageLines.clear();
  currentPageLineEndsParagraph.clear();
  currentPageLineStartsParagraph.clear();
  const bool pageStartsAtLineBegin = isOffsetAtLineStart(currentOffset);
  if (!loadPageAtOffset(currentOffset, pageStartsAtLineBegin, currentPageLines, &currentPageLineEndsParagraph,
                        &currentPageLineStartsParagraph, nextOffset)) {
    LOG_ERR("TRS", "Failed to load page at offset %zu", currentOffset);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  currentEndOffset = nextOffset;

  // Seed the page-count estimate from the first page we render in this
  // session so the status bar's N/M display is usable immediately.
  if (estBytesPerPage == 0 && nextOffset > currentOffset) {
    estBytesPerPage = nextOffset - currentOffset;
  }

  LOG_DBG("TRS", "Page loaded, %d lines. Rendering...", currentPageLines.size());

  renderer.clearScreen();
  renderPage();
  markPageRendered();

  LOG_DBG("TRS", "Render complete, saving progress...");

  // Save progress
  saveProgress();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void TxtReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId) * cachedLineCompression;
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    const size_t lineCount = currentPageLines.size();
    for (size_t i = 0; i < lineCount; i++) {
      const std::string& line = currentPageLines[i];
      const bool endsParagraph = i < currentPageLineEndsParagraph.size() ? currentPageLineEndsParagraph[i] : true;
      const bool startsParagraph =
          i < currentPageLineStartsParagraph.size() ? currentPageLineStartsParagraph[i] : false;
      const bool needsSpacingBefore = cachedExtraParagraphSpacing && startsParagraph && i > 0;
      if (needsSpacingBefore) {
        y += paragraphSpacingPx;
      }

      if (!line.empty()) {
        // Indent only applies to the first wrapped segment of a paragraph.
        const int indent = (cachedParagraphIndent && startsParagraph) ? paragraphIndentPx : 0;
        const int effectiveContentWidth = contentWidth - indent;
        int x = cachedOrientedMarginLeft + indent;
        // RTL detection: if a line starts with RTL text, switch to right-aligned
        // rendering so Arabic/Hebrew text doesn't appear left-justified by default.
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        // A line is "soft-wrapped" (safe to justify by widening gaps) when it
        // didn't consume a full source line AND it isn't the last line on the
        // page (the last line may actually continue on the next page even if
        // we didn't track it as wrapped).
        const bool isLastLineOnPage = (i + 1 == lineCount);
        const bool canJustify = !endsParagraph && !isLastLineOnPage;
        // Pre-compute text width once for use in alignment calculations.
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

        // Apply text alignment
        int8_t letterSpacing = 0;
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set to left margin (+ indent)
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            // Center within the indented content box so wrapped continuations
            // don't drift left of the paragraph's first line.
            x = cachedOrientedMarginLeft + indent + (effectiveContentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            if (canJustify) {
              const int extra = effectiveContentWidth - textWidth;
              // Count UTF-8 characters (lead bytes) to distribute justification gaps.
              int utf8Chars = 0;
              for (const char* p = line.c_str(); *p; ++p) {
                if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) ++utf8Chars;
              }
              const int gaps = utf8Chars - 1;
              if (extra > 0 && gaps > 0) {
                // drawText's letterSpacing is an int8_t; clamp to ±127. For
                // typical reading layouts the per-gap extra is single digits,
                // so the clamp only matters for pathological short lines.
                const int perGap = extra / gaps;
                letterSpacing = static_cast<int8_t>(std::min(perGap, 127));
              }
            }
            break;
        }

        if (letterSpacing > 0) {
          renderer.drawText(cachedFontId, x, y, line.c_str(), letterSpacing);
        } else {
          renderer.drawText(cachedFontId, x, y, line.c_str());
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();      // scan pass — text accumulated, no drawing
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  // scope destructor clears font cache via FontCacheManager
}

void TxtReaderActivity::renderStatusBar() const {
  const float progress = fileSize > 0 ? (currentOffset * 100.0f / fileSize) : 0.0f;
  std::string title;
  if (automaticPageTurnActive && pageTurnDuration > 0) {
    // Mirror the EPUB reader: while auto-turn is on, override the title with
    // the current pages-per-minute rate so the user can verify it.
    title = std::string(tr(STR_AUTO_TURN_ENABLED)) + std::to_string(60UL * 1000UL / pageTurnDuration);
  } else if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, estimatedCurrentPage(), estimatedTotalPages(), title);
}

void TxtReaderActivity::saveProgress() const {
  // Same byte layout as the former serialization::writePod() stream, assembled in
  // a small stack buffer so it can go through ProgressFile::writeAtomic() (no torn
  // progress.bin on power loss).
  uint8_t data[64];
  size_t len = 0;
  const auto put = [&data, &len](const void* value, const size_t size) {
    memcpy(data + len, value, size);
    len += size;
  };
  const uint32_t magic = PROGRESS_MAGIC;
  const uint8_t version = PROGRESS_VERSION;
  const auto savedFileSize = static_cast<uint32_t>(txt->getFileSize());
  const auto savedWidth = static_cast<int32_t>(viewportWidth);
  const auto savedMaxLines = static_cast<int32_t>(maxLinesPerPage);
  const auto savedFontId = static_cast<int32_t>(cachedFontId);
  const auto savedMargin = static_cast<int32_t>(cachedScreenMargin);
  const auto savedOffset = static_cast<uint64_t>(currentOffset);
  put(&magic, sizeof(magic));
  put(&version, sizeof(version));
  put(&savedFileSize, sizeof(savedFileSize));
  put(&savedWidth, sizeof(savedWidth));
  put(&savedMaxLines, sizeof(savedMaxLines));
  put(&savedFontId, sizeof(savedFontId));
  put(&savedMargin, sizeof(savedMargin));
  put(&cachedParagraphAlignment, sizeof(cachedParagraphAlignment));
  put(&cachedCharacterWrap, sizeof(cachedCharacterWrap));
  put(&cachedExtraParagraphSpacing, sizeof(cachedExtraParagraphSpacing));
  put(&cachedParagraphIndent, sizeof(cachedParagraphIndent));
  put(&cachedLineCompression, sizeof(cachedLineCompression));
  put(&savedOffset, sizeof(savedOffset));
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, len)) {
    LOG_ERR("TRS", "Failed to save progress: offset %zu", currentOffset);
  }
}

void TxtReaderActivity::loadProgress() {
  currentOffset = 0;
  currentEndOffset = 0;

  HalFile f;
  if (!Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    return;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != PROGRESS_MAGIC) return;

  uint8_t version;
  serialization::readPod(f, version);
  if (version != PROGRESS_VERSION) return;

  uint32_t savedFileSize;
  serialization::readPod(f, savedFileSize);
  if (savedFileSize != fileSize) return;  // file changed — start over

  // Read remaining layout fields purely for forward-compat / debugging. We
  // intentionally do NOT validate them against current settings: byte offset
  // is layout-independent (any layout change just re-paginates around the
  // saved position), and validating would lose the user's reading position
  // every time they tweak a font/margin/spacing setting.
  int32_t savedWidth, savedMaxLines, savedFontId, savedMargin;
  serialization::readPod(f, savedWidth);
  serialization::readPod(f, savedMaxLines);
  serialization::readPod(f, savedFontId);
  serialization::readPod(f, savedMargin);
  uint8_t savedAlignment, savedWrap, savedExtraSpacing, savedParagraphIndent;
  serialization::readPod(f, savedAlignment);
  serialization::readPod(f, savedWrap);
  serialization::readPod(f, savedExtraSpacing);
  serialization::readPod(f, savedParagraphIndent);
  float savedCompression;
  serialization::readPod(f, savedCompression);
  (void)savedWidth;
  (void)savedMaxLines;
  (void)savedFontId;
  (void)savedMargin;
  (void)savedAlignment;
  (void)savedWrap;
  (void)savedExtraSpacing;
  (void)savedParagraphIndent;
  (void)savedCompression;

  // savedOffset is the one field with no value-comparison after the read
  // (earlier fields are guarded by their own validation, so a truncated read
  // there harmlessly mismatches and returns). Read it via file.read() and
  // verify bytesRead so a truncated progress.bin doesn't leave savedOffset as
  // stack garbage and silently jump the user to a random byte position.
  uint64_t savedOffset;
  if (f.read(reinterpret_cast<uint8_t*>(&savedOffset), sizeof(savedOffset)) != sizeof(savedOffset)) {
    return;
  }
  if (savedOffset < fileSize) {
    // Snap to a line boundary so the new layout doesn't render a partial
    // wrap segment at the top of the page.
    currentOffset = snapToLineStart(static_cast<size_t>(savedOffset));
    LOG_DBG("TRS", "Loaded progress: offset %zu / %zu (%.0f%%)", currentOffset, fileSize,
            fileSize ? currentOffset * 100.0f / fileSize : 0.0f);
  }
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = estimatedCurrentPage();
  info.totalPages = estimatedTotalPages();
  info.progressPercent = fileSize > 0 ? static_cast<int>(currentOffset * 100.0f / fileSize + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
