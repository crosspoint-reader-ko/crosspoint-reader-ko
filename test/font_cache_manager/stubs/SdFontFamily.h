#pragma once

// Host-test stub of lib/EpdFont/SdFontFamily.h. FontCacheManager only needs the font type and
// the flash style data, so the SD (.epdfont) side is an empty marker type here.

#include <memory>

#include "EpdFontFamily.h"

using EpdFontStyle = EpdFontFamily::Style;

class SdFontFamily {};

class UnifiedFontFamily {
 public:
  enum class Type { FLASH, SD };

  explicit UnifiedFontFamily(const EpdFontFamily* font) : type(Type::FLASH), flashFont(font) {}
  explicit UnifiedFontFamily(SdFontFamily* font) : type(Type::SD), sdFont(font) {}

  Type getType() const { return type; }
  bool isSdFont() const { return type == Type::SD; }
  const EpdFontData* getFlashData(EpdFontStyle style = EpdFontFamily::REGULAR) const {
    return type == Type::FLASH && flashFont ? flashFont->getData(style) : nullptr;
  }

 private:
  Type type;
  const EpdFontFamily* flashFont = nullptr;
  std::unique_ptr<SdFontFamily> sdFont;
};

class UnifiedFontSlot : public std::unique_ptr<UnifiedFontFamily> {
 public:
  using std::unique_ptr<UnifiedFontFamily>::unique_ptr;
  UnifiedFontSlot() = default;
  explicit UnifiedFontSlot(std::unique_ptr<UnifiedFontFamily>&& family)
      : std::unique_ptr<UnifiedFontFamily>(std::move(family)) {}
};
