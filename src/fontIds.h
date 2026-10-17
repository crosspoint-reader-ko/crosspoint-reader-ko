// Font IDs for CrossPoint Korean version
#pragma once

// --- Korean build primary fonts ---
// UI font (Pretendard 10pt) - single font for all UI sizes
#define UI_FONT_ID (1983644244)
#define SMALL_FONT_ID UI_FONT_ID
#define UI_10_FONT_ID UI_FONT_ID
#define UI_12_FONT_ID UI_FONT_ID

// Korean EPUB reader font (Pretendard 14pt). SHA-256 of pretendard_14_regular.h, like build-font-ids.sh,
// so a font change also invalidates cached sections.
#define READER_14_FONT_ID (1493194985)

// Custom reader font loaded from SD card
#define CUSTOM_FONT_ID (-999999)

// User-selectable UI "system font" loaded from SD card.
// When set, it becomes the primary UI font (the whole UI renders with it) and Pretendard
// is kept as its glyph-level fallback for codepoints the SD font lacks. When unset, the UI
// uses Pretendard only.
#define SYSTEM_FONT_ID (-888888)

// Reserved id. The Korean system-font path keeps Pretendard registered under UI_FONT_ID and
// wires it as the SD system font's glyph fallback via setGlyphFallback(SYSTEM_FONT_ID, UI_FONT_ID),
// so this dedicated fallback slot is currently unused but kept reserved (and asserted non-zero).
#define UI_FALLBACK_FONT_ID (-777777)

// --- Upstream built-in reader font IDs (kept so upstream render/settings code compiles).
// The Korean build does not load these font objects; the reader uses Pretendard 14pt / a custom
// SD font instead (see CrossPointSettings::getReaderFontId and src/main.cpp font loading).
// UI_10/UI_12/SMALL are intentionally NOT redefined here — the Korean Pretendard IDs above win.
#define NOTOSERIF_12_FONT_ID (85340443)
#define NOTOSERIF_14_FONT_ID (-1367885987)
#define NOTOSERIF_16_FONT_ID (1428909134)
#define NOTOSERIF_18_FONT_ID (-501438527)
#define NOTOSANS_12_FONT_ID (2057568286)
#define NOTOSANS_14_FONT_ID (-1589315735)
#define NOTOSANS_16_FONT_ID (1669013660)
#define NOTOSANS_18_FONT_ID (37077304)

// Font ID 0 is reserved as the "not found" sentinel. Guard against any hash producing 0.
static_assert(NOTOSERIF_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSERIF_14_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSERIF_16_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSERIF_18_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_14_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_16_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(NOTOSANS_18_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_FALLBACK_FONT_ID != 0, "Font ID collision with sentinel");
