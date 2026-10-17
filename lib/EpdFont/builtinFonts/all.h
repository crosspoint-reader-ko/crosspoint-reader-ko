#pragma once

// Korean build ships only Pretendard (14pt reader, 10pt UI) to stay within the
// 6.25 MB OTA size budget. Upstream's NotoSerif/NotoSans/Ubuntu built-ins are intentionally
// NOT included here; src/main.cpp loads only the fonts below. The NOTOSERIF_*/NOTOSANS_*
// font IDs in fontIds.h remain defined so upstream code compiles, but their font data is absent.

// UI fonts (Pretendard 10pt) - Regular only, synthetic bold used when needed
#include <builtinFonts/pretendard_10_regular.h>

// Korean EPUB reader font (Pretendard 14pt, full Hangul, no Hanja) - Regular only, synthetic bold used when
// needed. KoPub Batang ships as a release .epdfont instead.
#include <builtinFonts/pretendard_14_regular.h>
