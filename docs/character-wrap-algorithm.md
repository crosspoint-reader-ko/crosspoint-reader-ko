# Character Wrap Algorithm for Korean Text

## Overview

This document describes the character-level line wrapping algorithm for Korean text with justified alignment. The algorithm ensures consistent word spacing across all lines while allowing character-level word breaks when necessary.

## Problem

With traditional justified text alignment, when a word doesn't fit on a line and wraps to the next, the remaining words on the current line get excessive spacing between them. This is particularly problematic for Korean text where words can be long.

### Example of the Problem

```
일반적인 양쪽정렬:
[단어1          단어2          단어3]  <- 공백이 너무 큼
[긴단어가다음줄로넘어감]
```

## Solution

The character wrap algorithm maintains consistent word spacing (1.0x - 1.5x of normal space width) by:
1. Filling lines greedily with words
2. Splitting words at character boundaries when spacing would exceed the maximum limit

### Target Spacing Range

- **Minimum Spacing**: 1.0x of normal space width (`spaceWidth`)
- **Maximum Spacing**: 1.5x of normal space width (`spaceWidth + spaceWidth/2`)

## Token input (upstream 1.5.0 and later)

`ParsedText::addWord()` splits any CJK-bearing word into **one token per character** and flags
every piece after the first with `wordNoSpaceBefore` (see `cjkCharacterBreakByteOffsets`). A
Hangul word therefore reaches this algorithm as N glued tokens, not one word.

`layoutCharacterWrap()` must consult `wordNoSpaceBefore` / `wordContinues` and treat only
space-delimited boundaries as gaps:

- **Glued token** (`wordNoSpaceBefore` or `wordContinues` set): drawn flush against the previous
  token, contributes no gap, and is not stretched by justification.
- **Real word boundary**: gets `minSpacing`, and is one of the gaps the justifier stretches.

Charging every token a gap instead spaces out each syllable and — once justification distributes
the spare space across those gaps — renders the whole paragraph at uniform letter spacing.
`realGapCount` (not the token count) is therefore the gap count in every spacing formula below.

Because a line of purely glued tokens has no stretchable gap, Phase 1 also needs an explicit
width guard (`newTotalWidth <= pageWidth`) to stop filling; the spacing test alone never trips.

### Which gap count the fill phases use

Phases 1 and 2 decide whether *another* token still fits, so every formula there budgets against
the gaps the line will have **after** that token lands — `fillGapCount(wantsGap)`, i.e.
`realGapCount + 1` at a real word boundary and `realGapCount` for a glued token. Phase 3 is the
only place that uses `realGapCount` on its own, because by then the line is final.

1.2/1.3-ko spelled the same quantity as `lineWordsVec.size()`: on a space-delimited line every
token carried a gap, so the token count *was* the post-add gap count. Substituting the plain
`realGapCount` there measures the line against a gap that does not exist yet, which reads a line
already sitting near 1.75x space width as over `maxSpacing`; Phase 2 then drags one more character
in and justification collapses the word gaps back to ~1.0x. That is a visibly tighter page — about
three characters per page — even though nothing about the width measurement changed.

## Algorithm

### Phase 1: Greedy Word Collection

Collect words that fit on the current line while maintaining `spacing >= minSpacing`:

```
for each word in input:
    calculate newSpacing if we add this word

    if first word on line:
        add word (or partial if too long)
    else if newSpacing >= minSpacing:
        add word
    else:
        # Word doesn't fit entirely
        calculate maxPartialWidth to maintain minSpacing
        add as many characters as possible within maxPartialWidth
        break (line is full)
```

**Key formula for partial word width**:
```
maxPartialWidth = pageWidth - totalWordWidth - gapCount * minSpacing
```

### Phase 2: Fill Excess Space

If spacing exceeds `maxSpacing`, add characters from the next word:

```
while spacing > maxSpacing and words remain:
    calculate maxPartialWidth to keep spacing within range
    add characters from next word up to maxPartialWidth
    update totalWordWidth
    recalculate spacing
```

**Spacing constraint formulas**:
```
# To keep spacing <= maxSpacing:
partialWidth >= pageWidth - totalWordWidth - gapCount * maxSpacing

# To keep spacing >= minSpacing:
partialWidth <= pageWidth - totalWordWidth - gapCount * minSpacing
```

### Phase 3: Calculate Justified Positions

Distribute spare space evenly across word gaps. Unlike Phases 1 and 2, this uses `realGapCount`
directly — the line is final, so there is no pending token to budget a gap for:

```
gapCount = realGapCount
spareSpace = pageWidth - totalWordWidth
baseSpacing = spareSpace / gapCount
extraPixels = spareSpace % gapCount  # Distribute to first N gaps

xpos = 0
for i, word in enumerate(words):
    positions[i] = xpos
    if i < wordCount - 1:
        gap = baseSpacing + (1 if i < extraPixels else 0)
        xpos += wordWidths[i] + gap
```

This ensures the last word's right edge aligns exactly with `pageWidth`.

### Last Line Handling

The last line uses left alignment with minimum spacing (standard typographic convention).

## UTF-8 Character Splitting

Korean characters are encoded as 3-byte UTF-8 sequences. The algorithm handles multi-byte characters correctly:

```cpp
static std::vector<std::string> splitUtf8Chars(const std::string& str) {
    std::vector<std::string> chars;
    const char* p = str.c_str();
    while (*p) {
        int charLen = 1;
        const unsigned char c = static_cast<unsigned char>(*p);
        if ((c & 0xF8) == 0xF0) charLen = 4;      // 4-byte UTF-8
        else if ((c & 0xF0) == 0xE0) charLen = 3;  // 3-byte UTF-8 (Korean)
        else if ((c & 0xE0) == 0xC0) charLen = 2;  // 2-byte UTF-8
        chars.push_back(std::string(p, charLen));
        p += charLen;
    }
    return chars;
}
```

## Visual Result

```
Character wrap enabled:
[단어1 단어2 단어3 긴단어의앞]  <- 균등한 공백 (1.0x-1.5x)
[부분 단어4 단어5 단어6 단어]  <- 균등한 공백 (1.0x-1.5x)
[7의끝부분]                    <- 마지막 줄 왼쪽 정렬
```

## Configuration

The feature is controlled by the `characterWrap` setting in `CrossPointSettings`:

```cpp
// Settings
uint8_t characterWrap = 0;  // 0 = disabled, 1 = enabled
```

UI toggle available in Settings: "문자 단위 줄바꿈"

## Files Modified

- `ParsedText.cpp` - Core algorithm implementation (`layoutCharacterWrap`)
- `ParsedText.h` - Method declaration
- `CrossPointSettings.h/cpp` - Setting storage
- `SettingsActivity.cpp` - UI toggle
- `Section.cpp/h` - Cache version bump
- `ChapterHtmlSlimParser.cpp/h` - Pass setting to parser
- `EpubReaderActivity.cpp` - Pass setting to Section
- `src/activities/reader/TxtReaderActivity.cpp` - TXT reader line breaking
- `lib/Txt/TxtToHtml.cpp` - Markdown to HTML for the EPUB reader
