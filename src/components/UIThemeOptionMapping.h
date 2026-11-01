#pragma once

#include <cstdint>

// Persisted theme IDs stay independent from the options visible on devices
// that do not have PSRAM for Cover Grid.
namespace ui_theme_options {

enum Theme : uint8_t { CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3, COVER_GRID = 4, DOS = 5 };

constexpr uint8_t optionCount(const bool supportsCoverGrid) { return supportsCoverGrid ? 6 : 5; }

constexpr uint8_t themeForOption(const uint8_t option, const bool supportsCoverGrid) {
  if (option <= ROUNDEDRAFF) return option;
  if (supportsCoverGrid && option == 4) return COVER_GRID;
  if (option == (supportsCoverGrid ? 5 : 4)) return DOS;
  return LYRA;
}

constexpr uint8_t optionForTheme(const uint8_t theme, const bool supportsCoverGrid) {
  if (theme <= ROUNDEDRAFF) return theme;
  if (theme == COVER_GRID) return supportsCoverGrid ? 4 : LYRA;
  if (theme == DOS) return supportsCoverGrid ? 5 : 4;
  return LYRA;
}

constexpr bool isAvailableTheme(const uint8_t theme, const bool supportsCoverGrid) {
  return theme <= DOS && (theme != COVER_GRID || supportsCoverGrid);
}

}  // namespace ui_theme_options
