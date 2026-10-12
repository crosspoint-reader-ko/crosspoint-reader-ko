#pragma once

#include <memory>

#include "EpdFontFamily.h"
#include "SdFont.h"

/**
 * SD Card font family - similar interface to EpdFontFamily but uses SdFont.
 * Supports regular, bold, italic, and bold-italic variants.
 */
class SdFontFamily {
 private:
  SdFont* regular;
  SdFont* bold;
  SdFont* italic;
  SdFont* boldItalic;
  bool ownsPointers;

  SdFont* getFont(EpdFontStyle style) const;

 public:
  // Constructor with raw pointers (does not take ownership)
  explicit SdFontFamily(SdFont* regular, SdFont* bold = nullptr, SdFont* italic = nullptr, SdFont* boldItalic = nullptr)
      : regular(regular), bold(bold), italic(italic), boldItalic(boldItalic), ownsPointers(false) {}

  // Constructor with file paths (creates and owns SdFont objects)
  explicit SdFontFamily(const char* regularPath, const char* boldPath = nullptr, const char* italicPath = nullptr,
                        const char* boldItalicPath = nullptr);

  ~SdFontFamily();

  // Disable copy
  SdFontFamily(const SdFontFamily&) = delete;
  SdFontFamily& operator=(const SdFontFamily&) = delete;

  // Enable move
  SdFontFamily(SdFontFamily&& other) noexcept;
  SdFontFamily& operator=(SdFontFamily&& other) noexcept;

  // Load all fonts in the family
  bool load();
  bool isLoaded() const;

  // EpdFontFamily-compatible interface
  void getTextDimensions(const char* string, int* w, int* h, EpdFontStyle style = REGULAR) const;
  bool hasPrintableChars(const char* string, EpdFontStyle style = REGULAR) const;

  // Get glyph (metadata only, no bitmap)
  const EpdGlyph* getGlyph(uint32_t cp, EpdFontStyle style = REGULAR) const;

  // Returns true only if this font actually contains a real glyph for cp.
  bool hasGlyph(uint32_t cp, EpdFontStyle style = REGULAR) const;

  // Get glyph bitmap data (loaded on demand from SD)
  const uint8_t* getGlyphBitmap(uint32_t cp, EpdFontStyle style = REGULAR) const;

  // Font metadata
  uint8_t getAdvanceY(EpdFontStyle style = REGULAR) const;
  int8_t getAscender(EpdFontStyle style = REGULAR) const;
  int8_t getDescender(EpdFontStyle style = REGULAR) const;
  bool is2Bit(EpdFontStyle style = REGULAR) const;

  // Check if bold variant is available
  bool hasBold() const { return bold != nullptr; }
};

/**
 * Unified font family that can hold either EpdFontFamily (flash) or SdFontFamily (SD card).
 * This allows GfxRenderer to work with both types transparently.
 */
class UnifiedFontFamily {
 public:
  enum class Type { FLASH, SD };

 private:
  Type type;
  const EpdFontFamily* flashFont;  // Non-owning pointer for flash fonts (they're global)
  SdFontFamily* sdFont;            // Owned pointer for SD fonts

  // Optional glyph-level fallback family (non-owning). When the primary family has no
  // real glyph for a codepoint, lookups delegate to this fallback. Used to let the UI
  // font (Hangul/Latin only) render Hanja/Kana from a user-selected SD "system font".
  const UnifiedFontFamily* glyphFallback = nullptr;

  // Own (non-fallback) lookups — operate strictly on this family's own glyphs.
  bool hasOwnGlyph(uint32_t cp, EpdFontStyle style) const;
  const EpdGlyph* getOwnGlyph(uint32_t cp, EpdFontStyle style) const;

 public:
  // Construct from flash font (EpdFontFamily) - stores pointer, does not copy
  explicit UnifiedFontFamily(const EpdFontFamily* font);

  // Construct from SD font family (takes ownership)
  explicit UnifiedFontFamily(SdFontFamily* font);

  ~UnifiedFontFamily();

  // Disable copy
  UnifiedFontFamily(const UnifiedFontFamily&) = delete;
  UnifiedFontFamily& operator=(const UnifiedFontFamily&) = delete;

  // Enable move
  UnifiedFontFamily(UnifiedFontFamily&& other) noexcept;
  UnifiedFontFamily& operator=(UnifiedFontFamily&& other) noexcept;

  Type getType() const { return type; }
  bool isSdFont() const { return type == Type::SD; }

  // Glyph-level fallback wiring. The pointer is non-owning and must outlive this family.
  void setGlyphFallback(const UnifiedFontFamily* fallback) { glyphFallback = fallback; }
  const UnifiedFontFamily* getGlyphFallback() const { return glyphFallback; }

  // Resolve which family actually owns the glyph for cp (primary, else fallback).
  // Sets *outGlyph to the resolved glyph (may be the primary's replacement glyph, or
  // nullptr for SD-only families with no match). The returned family is never recursive
  // (it is a plain primary/fallback family), so the renderer can query its type/metrics
  // directly. Used by the text renderer for correct per-glyph bitmap + ascender + is2Bit.
  const UnifiedFontFamily* familyForGlyph(uint32_t cp, EpdFontStyle style, const EpdGlyph** outGlyph) const;

  // Unified interface
  void getTextDimensions(const char* string, int* w, int* h, EpdFontStyle style = REGULAR) const;
  bool hasPrintableChars(const char* string, EpdFontStyle style = REGULAR) const;
  const EpdGlyph* getGlyph(uint32_t cp, EpdFontStyle style = REGULAR) const;

  // For SD fonts: get bitmap data (for flash fonts, use getData()->bitmap[offset])
  const uint8_t* getGlyphBitmap(uint32_t cp, EpdFontStyle style = REGULAR) const;

  // Metadata (common interface)
  uint8_t getAdvanceY(EpdFontStyle style = REGULAR) const;
  int8_t getAscender(EpdFontStyle style = REGULAR) const;
  int8_t getDescender(EpdFontStyle style = REGULAR) const;
  bool is2Bit(EpdFontStyle style = REGULAR) const;

  // Flash font specific (returns nullptr for SD fonts)
  const EpdFontData* getFlashData(EpdFontStyle style = REGULAR) const;

  // Check if bold variant is available (for synthetic bold decision)
  bool hasBold() const;

  // Kerning: delegates to flash font; SD fonts have no kerning tables so returns 0.
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp, EpdFontStyle style = REGULAR) const;

  // Ligatures: delegates to flash font; SD fonts have no ligature tables so returns cp unchanged.
  uint32_t applyLigatures(uint32_t cp, const char*& text, EpdFontStyle style = REGULAR) const;

  // EpdFontData accessor for upstream compatibility (flash only; returns nullptr for SD fonts)
  const EpdFontData* getData(EpdFontStyle style = REGULAR) const { return getFlashData(style); }
};

// Owning slot in GfxRenderer's font map. A unique_ptr that also answers getGlyph(), because
// FreeInkUI reads glyph metrics straight from GfxRenderer::getFontMap() entries
// (`font->second.getGlyph(...)`) as if the map held families by value, as upstream's does.
class UnifiedFontSlot : public std::unique_ptr<UnifiedFontFamily> {
 public:
  using std::unique_ptr<UnifiedFontFamily>::unique_ptr;
  UnifiedFontSlot() = default;
  explicit UnifiedFontSlot(std::unique_ptr<UnifiedFontFamily>&& family)
      : std::unique_ptr<UnifiedFontFamily>(std::move(family)) {}
  const EpdGlyph* getGlyph(uint32_t cp, EpdFontStyle style = REGULAR) const {
    return get() ? get()->getGlyph(cp, style) : nullptr;
  }
};
