#pragma once
#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;

  const EpdGlyph* getGlyph(uint32_t cp) const;

  /// Returns true only if this font actually contains a real glyph for \p cp.
  /// Unlike getGlyph(), this never falls back to the U+FFFD replacement glyph,
  /// so it can be used to decide whether a glyph-level fallback font is needed.
  bool hasGlyph(uint32_t cp) const;
  /// Like getGlyph(), but supplies metrics for missing solid block symbols.
  /// If the returned pointer is &solidFallback, draw a filled rectangle;
  /// it has no bitmap. The caller owns the fallback storage.
  const EpdGlyph* getGlyphMetrics(uint32_t cp, EpdGlyph& solidFallback) const;

  /// Upstream name for hasGlyph(): interval coverage only, no replacement fallback. The Korean
  /// build has no SD coverage handler, so the two are the same.
  bool hasCodepoint(uint32_t cp) const { return hasGlyph(cp); }

  /// Returns the kerning adjustment (4.4 fixed-point in pixels) between two codepoints.
  /// Returns 0 if no kerning data exists for the pair.
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp) const;

  /// Returns the ligature codepoint for a pair, or 0 if no ligature exists.
  uint32_t getLigature(uint32_t leftCp, uint32_t rightCp) const;

  /// Greedily applies ligature substitutions starting from cp, consuming
  /// as many following codepoints from text as possible. Returns the
  /// (possibly substituted) codepoint; advances text past consumed chars.
  uint32_t applyLigatures(uint32_t cp, const char*& text) const;
};

namespace syntheticGlyph {
// Metrics for a solid symbol the font lacks, sized from the font's 'M' (may be null) and
// ascender. Fills and returns &solidFallback, which has no bitmap: draw a filled rectangle.
const EpdGlyph* solidMetrics(uint32_t cp, const EpdGlyph* emGlyph, int fontAscender, EpdGlyph& solidFallback);
}  // namespace syntheticGlyph
