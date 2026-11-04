#include "SdFontFamily.h"

#include <Logging.h>
#include <Utf8.h>

#include <algorithm>

// ============================================================================
// SdFontFamily Implementation
// ============================================================================

SdFontFamily::SdFontFamily(const char* regularPath, const char* boldPath, const char* italicPath,
                           const char* boldItalicPath)
    : regular(nullptr), bold(nullptr), italic(nullptr), boldItalic(nullptr), ownsPointers(true) {
  if (regularPath) {
    regular = new SdFont(regularPath);
  }
  if (boldPath) {
    bold = new SdFont(boldPath);
  }
  if (italicPath) {
    italic = new SdFont(italicPath);
  }
  if (boldItalicPath) {
    boldItalic = new SdFont(boldItalicPath);
  }
}

SdFontFamily::~SdFontFamily() {
  if (ownsPointers) {
    delete regular;
    delete bold;
    delete italic;
    delete boldItalic;
  }
}

SdFontFamily::SdFontFamily(SdFontFamily&& other) noexcept
    : regular(other.regular),
      bold(other.bold),
      italic(other.italic),
      boldItalic(other.boldItalic),
      ownsPointers(other.ownsPointers) {
  other.regular = nullptr;
  other.bold = nullptr;
  other.italic = nullptr;
  other.boldItalic = nullptr;
  other.ownsPointers = false;
}

SdFontFamily& SdFontFamily::operator=(SdFontFamily&& other) noexcept {
  if (this != &other) {
    if (ownsPointers) {
      delete regular;
      delete bold;
      delete italic;
      delete boldItalic;
    }

    regular = other.regular;
    bold = other.bold;
    italic = other.italic;
    boldItalic = other.boldItalic;
    ownsPointers = other.ownsPointers;

    other.regular = nullptr;
    other.bold = nullptr;
    other.italic = nullptr;
    other.boldItalic = nullptr;
    other.ownsPointers = false;
  }
  return *this;
}

bool SdFontFamily::load() {
  bool success = true;

  if (regular && !regular->load()) {
    LOG_ERR("SDF", "Failed to load regular font");
    success = false;
  }
  if (bold && !bold->load()) {
    LOG_ERR("SDF", "Failed to load bold font");
    // Bold is optional, don't fail completely
  }
  if (italic && !italic->load()) {
    LOG_ERR("SDF", "Failed to load italic font");
    // Italic is optional
  }
  if (boldItalic && !boldItalic->load()) {
    LOG_ERR("SDF", "Failed to load bold-italic font");
    // Bold-italic is optional
  }

  return success;
}

bool SdFontFamily::isLoaded() const { return regular && regular->isLoaded(); }

SdFont* SdFontFamily::getFont(EpdFontStyle style) const {
  if (style == BOLD && bold && bold->isLoaded()) {
    return bold;
  }
  if (style == ITALIC && italic && italic->isLoaded()) {
    return italic;
  }
  if (style == BOLD_ITALIC) {
    if (boldItalic && boldItalic->isLoaded()) {
      return boldItalic;
    }
    if (bold && bold->isLoaded()) {
      return bold;
    }
    if (italic && italic->isLoaded()) {
      return italic;
    }
  }

  return regular;
}

void SdFontFamily::getTextDimensions(const char* string, int* w, int* h, EpdFontStyle style) const {
  SdFont* font = getFont(style);
  if (font) {
    font->getTextDimensions(string, w, h);
  } else {
    *w = 0;
    *h = 0;
  }
}

bool SdFontFamily::hasPrintableChars(const char* string, EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->hasPrintableChars(string) : false;
}

const EpdGlyph* SdFontFamily::getGlyph(uint32_t cp, EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->getGlyph(cp) : nullptr;
}

bool SdFontFamily::hasGlyph(uint32_t cp, EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->hasGlyph(cp) : false;
}

const uint8_t* SdFontFamily::getGlyphBitmap(uint32_t cp, EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->getGlyphBitmap(cp) : nullptr;
}

uint8_t SdFontFamily::getAdvanceY(EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->getAdvanceY() : 0;
}

int8_t SdFontFamily::getAscender(EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->getAscender() : 0;
}

int8_t SdFontFamily::getDescender(EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->getDescender() : 0;
}

bool SdFontFamily::is2Bit(EpdFontStyle style) const {
  SdFont* font = getFont(style);
  return font ? font->is2Bit() : false;
}

// ============================================================================
// UnifiedFontFamily Implementation
// ============================================================================

UnifiedFontFamily::UnifiedFontFamily(const EpdFontFamily* font) : type(Type::FLASH), flashFont(font), sdFont(nullptr) {}

UnifiedFontFamily::UnifiedFontFamily(SdFontFamily* font) : type(Type::SD), flashFont(nullptr), sdFont(font) {}

UnifiedFontFamily::~UnifiedFontFamily() {
  // flashFont is not owned (points to global), don't delete
  delete sdFont;
}

UnifiedFontFamily::UnifiedFontFamily(UnifiedFontFamily&& other) noexcept
    : type(other.type), flashFont(other.flashFont), sdFont(other.sdFont), glyphFallback(other.glyphFallback) {
  other.flashFont = nullptr;
  other.sdFont = nullptr;
  other.glyphFallback = nullptr;
}

UnifiedFontFamily& UnifiedFontFamily::operator=(UnifiedFontFamily&& other) noexcept {
  if (this != &other) {
    // flashFont is not owned (points to global), don't delete
    delete sdFont;

    type = other.type;
    flashFont = other.flashFont;
    sdFont = other.sdFont;
    glyphFallback = other.glyphFallback;

    other.flashFont = nullptr;
    other.sdFont = nullptr;
    other.glyphFallback = nullptr;
  }
  return *this;
}

void UnifiedFontFamily::getTextDimensions(const char* string, int* w, int* h, EpdFontStyle style) const {
  // Fast path: with no glyph-level fallback configured, behaviour is identical to before —
  // delegate straight to the underlying family so all existing layout is byte-for-byte unchanged.
  if (!glyphFallback) {
    if (type == Type::FLASH && flashFont) {
      flashFont->getTextDimensions(string, w, h, style);
    } else if (sdFont) {
      sdFont->getTextDimensions(string, w, h, style);
    } else {
      *w = 0;
      *h = 0;
    }
    return;
  }

  // Fallback-aware bounds: mirror EpdFont::getTextBounds exactly (same ligatures, kerning,
  // combining-mark handling and 12.4 fixed-point snapping) but resolve each glyph through
  // familyForGlyph so fallback glyphs contribute their real metrics. This keeps getTextWidth()
  // in agreement with drawText() for mixed Hangul/Hanja strings.
  *w = 0;
  *h = 0;
  if (!string || *string == '\0') {
    return;
  }

  int minX = 0, minY = 0, maxX = 0, maxY = 0;
  int lastBaseX = 0, lastBaseLeft = 0, lastBaseWidth = 0, lastBaseTop = 0;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap
  const char* cursor = string;
  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    const bool isCombining = utf8IsCombiningMark(cp);
    if (!isCombining) {
      cp = applyLigatures(cp, cursor, style);  // ligatures come from the primary flash font
    }

    const EpdGlyph* glyph = nullptr;
    familyForGlyph(cp, style, &glyph);
    if (!glyph) {
      if (!isCombining) {
        lastBaseX += fp4::toPixel(prevAdvanceFP);  // flush pending advance before resetting
        prevCp = 0;
        prevAdvanceFP = 0;
        lastBaseLeft = 0;
        lastBaseWidth = 0;
        lastBaseTop = 0;
      }
      continue;
    }

    // Mirror GfxRenderer's combining-mark placement so measurement matches rendering: anchorFor
    // pins position-sensitive marks (niqqud) and leaves the rest centered/raised.
    const auto anchor = isCombining ? combiningMark::anchorFor(cp) : combiningMark::Anchor::CenterRaised;
    const int raiseBy = isCombining ? combiningMark::raiseAboveBase(anchor, glyph->top, glyph->height, lastBaseTop) : 0;

    if (!isCombining && prevCp != 0) {
      const auto kernFP = getKerning(prevCp, cp, style);  // 4.4 fixed-point kern (primary only)
      lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);
    }

    const int glyphBaseX = isCombining ? combiningMark::anchorOver(anchor, lastBaseX, lastBaseLeft, lastBaseWidth,
                                                                   glyph->left, glyph->width)
                                       : lastBaseX;
    const int glyphBaseY = 0 - raiseBy;

    minX = std::min(minX, glyphBaseX + glyph->left);
    maxX = std::max(maxX, glyphBaseX + glyph->left + glyph->width);
    minY = std::min(minY, glyphBaseY + glyph->top - glyph->height);
    maxY = std::max(maxY, glyphBaseY + glyph->top);

    if (!isCombining) {
      lastBaseLeft = glyph->left;
      lastBaseWidth = glyph->width;
      lastBaseTop = glyph->top;
      prevAdvanceFP = glyph->advanceX;  // 12.4 fixed-point
      prevCp = cp;
    }
  }

  *w = maxX - minX;
  *h = maxY - minY;
}

bool UnifiedFontFamily::hasPrintableChars(const char* string, EpdFontStyle style) const {
  const bool primaryHas = (type == Type::FLASH && flashFont) ? flashFont->hasPrintableChars(string, style)
                          : (sdFont)                         ? sdFont->hasPrintableChars(string, style)
                                                             : false;
  if (primaryHas) {
    return true;
  }
  // A string consisting only of glyphs absent from the primary (e.g. an all-Hanja
  // title under the Hangul/Latin UI font) is still printable via the fallback.
  if (glyphFallback && string) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(string);
    uint32_t cp;
    while ((cp = utf8NextCodepoint(&p))) {
      if (glyphFallback->hasOwnGlyph(cp, style)) {
        return true;
      }
    }
  }
  return false;
}

bool UnifiedFontFamily::hasOwnGlyph(uint32_t cp, EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    return flashFont->hasGlyph(cp, style);
  } else if (sdFont) {
    return sdFont->hasGlyph(cp, style);
  }
  return false;
}

const EpdGlyph* UnifiedFontFamily::getOwnGlyph(uint32_t cp, EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    return flashFont->getGlyph(cp, style);
  } else if (sdFont) {
    return sdFont->getGlyph(cp, style);
  }
  return nullptr;
}

const UnifiedFontFamily* UnifiedFontFamily::familyForGlyph(uint32_t cp, EpdFontStyle style,
                                                           const EpdGlyph** outGlyph) const {
  // Fast path (the common case, incl. the reader body font): no fallback configured, so a
  // single glyph lookup is all that is needed — identical cost to the pre-fallback renderer.
  if (!glyphFallback) {
    *outGlyph = getOwnGlyph(cp, style);
    return this;
  }
  if (hasOwnGlyph(cp, style)) {
    *outGlyph = getOwnGlyph(cp, style);
    return this;
  }
  if (glyphFallback->hasOwnGlyph(cp, style)) {
    *outGlyph = glyphFallback->getOwnGlyph(cp, style);
    return glyphFallback;
  }
  // Neither has it: fall back to the primary's own lookup (flash replacement glyph
  // or nullptr for SD), preserving the pre-fallback rendering behavior.
  *outGlyph = getOwnGlyph(cp, style);
  return this;
}

const UnifiedFontFamily* UnifiedFontFamily::familyForGlyphMetrics(uint32_t cp, EpdFontStyle style,
                                                                  EpdGlyph& solidFallback,
                                                                  const EpdGlyph** outGlyph) const {
  if (syntheticGlyph::isSolid(cp) && !hasCodepoint(cp, style)) {
    const EpdGlyph* em = hasOwnGlyph('M', style) ? getOwnGlyph('M', style) : nullptr;
    *outGlyph = syntheticGlyph::solidMetrics(cp, em, getAscender(style), solidFallback);
    return this;
  }
  return familyForGlyph(cp, style, outGlyph);
}

const EpdGlyph* UnifiedFontFamily::getGlyph(uint32_t cp, EpdFontStyle style) const {
  // Fast path: no fallback → single lookup, byte-for-byte identical to the original behavior.
  if (!glyphFallback) {
    return getOwnGlyph(cp, style);
  }
  if (hasOwnGlyph(cp, style)) {
    return getOwnGlyph(cp, style);
  }
  if (glyphFallback->hasOwnGlyph(cp, style)) {
    return glyphFallback->getOwnGlyph(cp, style);
  }
  return getOwnGlyph(cp, style);
}

const uint8_t* UnifiedFontFamily::getGlyphBitmap(uint32_t cp, EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    // For flash fonts, get bitmap from the data structure
    const EpdFontData* data = flashFont->getData(style);
    const EpdGlyph* glyph = flashFont->getGlyph(cp, style);
    if (data && glyph) {
      return &data->bitmap[glyph->dataOffset];
    }
    return nullptr;
  } else if (sdFont) {
    return sdFont->getGlyphBitmap(cp, style);
  }
  return nullptr;
}

uint8_t UnifiedFontFamily::getAdvanceY(EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    const EpdFontData* data = flashFont->getData(style);
    return data ? data->advanceY : 0;
  } else if (sdFont) {
    return sdFont->getAdvanceY(style);
  }
  return 0;
}

int8_t UnifiedFontFamily::getAscender(EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    const EpdFontData* data = flashFont->getData(style);
    return data ? data->ascender : 0;
  } else if (sdFont) {
    return sdFont->getAscender(style);
  }
  return 0;
}

int8_t UnifiedFontFamily::getDescender(EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    const EpdFontData* data = flashFont->getData(style);
    return data ? data->descender : 0;
  } else if (sdFont) {
    return sdFont->getDescender(style);
  }
  return 0;
}

bool UnifiedFontFamily::is2Bit(EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    const EpdFontData* data = flashFont->getData(style);
    return data ? data->is2Bit : false;
  } else if (sdFont) {
    return sdFont->is2Bit(style);
  }
  return false;
}

const EpdFontData* UnifiedFontFamily::getFlashData(EpdFontStyle style) const {
  if (type == Type::FLASH && flashFont) {
    return flashFont->getData(style);
  }
  return nullptr;
}

bool UnifiedFontFamily::hasBold() const {
  if (type == Type::FLASH && flashFont) {
    return flashFont->hasBold();
  } else if (sdFont) {
    return sdFont->hasBold();
  }
  return false;
}

int8_t UnifiedFontFamily::getKerning(uint32_t leftCp, uint32_t rightCp, EpdFontStyle style) const {
  // Only flash fonts carry kerning tables; SD fonts return 0 (no kerning).
  if (type == Type::FLASH && flashFont) {
    return flashFont->getKerning(leftCp, rightCp, style);
  }
  return 0;
}

uint32_t UnifiedFontFamily::applyLigatures(uint32_t cp, const char*& text, EpdFontStyle style) const {
  // Only flash fonts carry ligature tables; SD fonts pass codepoint through unchanged.
  if (type == Type::FLASH && flashFont) {
    return flashFont->applyLigatures(cp, text, style);
  }
  return cp;
}
