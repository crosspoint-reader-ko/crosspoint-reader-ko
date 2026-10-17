#pragma once

#include <cstddef>
#include <cstdint>

// Reader font size is stored as an actual point size (see CrossPointSettings::
// fontPointSize), not an abstract Small/Medium/Large slot.
//
// The Korean build does not use upstream's SD-card font registry to enumerate
// installed point sizes: its reader font is Pretendard 14pt or a single-size SD
// .epdfont picked in FontSelectionActivity. Only the built-in set below is
// selectable, so the registry-aware readerFontPointSizes() upstream ships is
// not carried here.

// The built-in Noto Serif / Noto Sans families are compiled in at exactly these
// point sizes (see the global font objects in main.cpp).
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12, 14, 16, 18};

// Closest entry in `sizes` (ascending, `count` > 0) to `pt`; ties resolve to the
// smaller size. Takes a raw range rather than a vector because getReaderFontId()
// runs inside the page render loop and must not allocate.
uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt);
