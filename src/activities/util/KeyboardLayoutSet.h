#pragma once

#include <FreeInkUI.h>
#include <I18n.h>

#include <cstdint>

namespace keyboard_layouts {

struct LayoutInfo {
  freeink::ui::KeyboardLayoutId id;
  // UI language whose speakers start on this layout. The Korean build only compiles
  // english.yaml and korean.yaml, so the generated Language enum has no FR/DE/ES/...
  // members; those rows use Language::_COUNT, which never matches a UI language.
  Language language;
  // Row label in Settings > Keyboard layouts (I18N.getLanguageName() can't name
  // languages that aren't compiled in).
  const char* name;
};

// Table position is the persisted bit assignment. Keep existing rows in place
// and append new layouts so SDK enum changes cannot reinterpret saved masks.
inline constexpr LayoutInfo ALL[] = {
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::EN, "English"},
    {freeink::ui::KeyboardLayoutId::AzertyFr, Language::_COUNT, "French"},
    {freeink::ui::KeyboardLayoutId::QwertzDe, Language::_COUNT, "German"},
    {freeink::ui::KeyboardLayoutId::SpanishEs, Language::_COUNT, "Spanish"},
    {freeink::ui::KeyboardLayoutId::CyrillicRu, Language::_COUNT, "Russian"},
    {freeink::ui::KeyboardLayoutId::CyrillicUk, Language::_COUNT, "Ukrainian"},
    {freeink::ui::KeyboardLayoutId::CyrillicBe, Language::_COUNT, "Belarusian"},
    {freeink::ui::KeyboardLayoutId::CyrillicKk, Language::_COUNT, "Kazakh"},
    {freeink::ui::KeyboardLayoutId::HebrewIl, Language::_COUNT, "Hebrew"},
    {freeink::ui::KeyboardLayoutId::ArabicAr, Language::_COUNT, "Arabic"},
};
inline constexpr uint8_t COUNT = sizeof(ALL) / sizeof(ALL[0]);
static_assert(COUNT <= 16, "keyboard layout mask is uint16_t");

inline constexpr uint16_t bitAt(const uint8_t i) { return static_cast<uint16_t>(1u << i); }
// Symbol layers have no Latin letters, so credentials and URLs require at
// least one of these layouts to remain enabled.
inline constexpr uint16_t LATIN_BITS = bitAt(0) | bitAt(1) | bitAt(2) | bitAt(3);

uint16_t enabled();
freeink::ui::KeyboardLayoutId startingLayout();
freeink::ui::KeyboardLayoutId next(freeink::ui::KeyboardLayoutId current);

}  // namespace keyboard_layouts
