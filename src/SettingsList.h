#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "HomeButtonSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderLineSpacing.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "util/DictionaryRegistry.h"
#include "util/SlideSwitch.h"

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.key = "dictionaryName";  // web settings API; persisted by name, not by the generic loop
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

inline std::vector<StrId> buildLongPressMenuValues() {
  std::vector<StrId> values = {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                               StrId::STR_DICTIONARY};
  values.reserve(CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT);
  if (BoardConfig::hasHomeKey()) values.push_back(StrId::STR_READER_MENU);
  values.push_back(StrId::STR_SAVE_CLIPPING);
  values.push_back(StrId::STR_FOOTNOTES);
  return values;
}

inline uint8_t longPressMenuDisplayValue() {
  const uint8_t raw = SETTINGS.longPressMenuFunction;
  if (raw <= CrossPointSettings::LP_MENU_DICTIONARY) return raw;
  if (raw == CrossPointSettings::LP_MENU_READER_MENU) {
    return BoardConfig::hasHomeKey() ? CrossPointSettings::LP_MENU_READER_MENU : CrossPointSettings::LP_MENU_DISABLED;
  }
  if (raw < CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT) return BoardConfig::hasHomeKey() ? raw : raw - 1;
  return CrossPointSettings::LP_MENU_DISABLED;
}

inline void setLongPressMenuFromDisplayValue(const uint8_t displayValue) {
  if (displayValue <= CrossPointSettings::LP_MENU_DICTIONARY) {
    SETTINGS.longPressMenuFunction = displayValue;
  } else {
    SETTINGS.longPressMenuFunction = BoardConfig::hasHomeKey() ? displayValue : displayValue + 1;
  }
}

inline SettingInfo buildLongPressMenuSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_LONG_PRESS_MENU, buildLongPressMenuValues(), longPressMenuDisplayValue,
                                  setLongPressMenuFromDisplayValue, "longPressMenuFunction", StrId::STR_CAT_CONTROLS);
}

struct ShortPowerButtonOption {
  StrId label;
  uint8_t value;
};

// Menu order differs from the persisted enum order, so map by value.
inline std::vector<ShortPowerButtonOption> shortPowerButtonOptions() {
  std::vector<ShortPowerButtonOption> options = {{StrId::STR_IGNORE, CrossPointSettings::IGNORE},
                                                 {StrId::STR_SLEEP, CrossPointSettings::SLEEP},
                                                 {StrId::STR_PAGE_TURN, CrossPointSettings::PAGE_TURN},
                                                 {StrId::STR_FORCE_REFRESH, CrossPointSettings::FORCE_REFRESH},
                                                 {StrId::STR_FOOTNOTES, CrossPointSettings::FOOTNOTES}};
  options.reserve(CrossPointSettings::SHORT_PWRBTN_COUNT);
  if (BoardConfig::hasTouch()) {
    options.push_back({StrId::STR_CONFIRM, CrossPointSettings::PWR_CONFIRM});
  } else {
    options.push_back({StrId::STR_SAVE_CLIPPING, CrossPointSettings::CREATE_CLIPPING});
  }
  // Touch boards without a home key have no button for these shortcuts.
  if (BoardConfig::hasTouch() && !BoardConfig::hasHomeKey()) {
    options.push_back({StrId::STR_BOOKMARK_OPTION, CrossPointSettings::PWR_BOOKMARK});
    options.push_back({StrId::STR_KOSYNC, CrossPointSettings::PWR_SYNC});
    options.push_back({StrId::STR_READER_MENU, CrossPointSettings::PWR_READER_MENU});
  }
  return options;
}

inline std::vector<StrId> buildShortPowerButtonValues() {
  std::vector<StrId> values;
  for (const auto& option : shortPowerButtonOptions()) values.push_back(option.label);
  return values;
}

inline uint8_t shortPowerButtonDisplayValue() {
  const auto options = shortPowerButtonOptions();
  for (size_t i = 0; i < options.size(); i++) {
    if (options[i].value == SETTINGS.shortPwrBtn) return static_cast<uint8_t>(i);
  }
  return CrossPointSettings::IGNORE;
}

inline void setShortPowerButtonFromDisplayValue(const uint8_t displayValue) {
  const auto options = shortPowerButtonOptions();
  SETTINGS.shortPwrBtn = displayValue < options.size() ? options[displayValue].value : CrossPointSettings::IGNORE;
}

inline SettingInfo buildShortPowerButtonSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_SHORT_PWR_BTN, buildShortPowerButtonValues(), shortPowerButtonDisplayValue,
                                  setShortPowerButtonFromDisplayValue, "shortPwrBtn", StrId::STR_CAT_CONTROLS);
}

inline std::vector<StrId> homeThemeValues() {
  std::vector<StrId> values = {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                               StrId::STR_THEME_ROUNDEDRAFF};
  if (UITheme::supportsCoverGrid()) values.push_back(StrId::STR_THEME_COVER_GRID);
  values.push_back(StrId::STR_THEME_DOS);
  return values;
}

inline SettingInfo buildHomeThemeSetting() {
  const bool supportsCoverGrid = UITheme::supportsCoverGrid();
  return SettingInfo::DynamicEnum(
      StrId::STR_UI_THEME, homeThemeValues(),
      [supportsCoverGrid]() -> uint8_t {
        return ui_theme_options::optionForTheme(SETTINGS.uiTheme, supportsCoverGrid);
      },
      [supportsCoverGrid](const uint8_t option) {
        if (option < ui_theme_options::optionCount(supportsCoverGrid)) {
          SETTINGS.uiTheme = ui_theme_options::themeForOption(option, supportsCoverGrid);
        }
      },
      "uiTheme", StrId::STR_CAT_DISPLAY);
}

using SettingVisitor = void (*)(void* context, SettingInfo& info);

// Visits synchronously in list order. Borrowed references are valid only during
// the callback; move from the descriptor to retain ownership. Its board conditions
// must be fixed before settings load, or load and save would see different keys.
void forEachBaseSetting(SettingVisitor visit, void* context);

template <typename Fn>
void forEachBaseSetting(Fn& fn) {
  forEachBaseSetting([](void* context, SettingInfo& info) { (*static_cast<Fn*>(context))(info); }, &fn);
}

// Collects the same descriptors into an owning list for UI and web callers.
std::vector<SettingInfo> getBaseSettingsList(size_t extraCapacity = 0);

// UI callers own the list for runtime filtering and dictionary options.
//
// The Korean build has no upstream font-family/font-size pickers (reader font selection routes
// through the FontSelection action) and gates the Focus Reading toggle to English.
inline std::vector<SettingInfo> getSettingsList(const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  auto v = getBaseSettingsList(dictionaries && !dictionaries->empty() ? 1 : 0);
  // Focus Reading is English-only: word-prefix bolding has no meaning for Korean (or other
  // non-space-delimited / non-Latin) text, so hide the toggle unless the UI language is English.
  // The reader also force-disables the effect for non-English content (see ReaderUtils).
  if (I18N.getLanguage() != Language::EN) {
    v.erase(
        std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FOCUS_READING; }),
        v.end());
  }
  // Frontlit and lightless variants of a board share one binary (EEGO A4);
  // presence is the I2C probe result from Frontlight.begin(). UI-only: the probe
  // has not run at load time, so the base list must keep the persisted key.
  if (!Frontlight.present()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_RESTORE_LIGHT_ON_WAKE; }),
            v.end());
  }
  // The slide switch's position owns its bound function (the light at boot
  // too). UI-only, so the keys stay persisted for other devices.
  v.erase(std::remove_if(v.begin(), v.end(),
                         [](const SettingInfo& s) {
                           return (s.nameId == StrId::STR_RESTORE_LIGHT_ON_WAKE && slideSwitchOwnsLight()) ||
                                  (s.nameId == StrId::STR_NIGHT_MODE &&
                                   slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_NIGHT_MODE)) ||
                                  (s.nameId == StrId::STR_TOUCH_READER_CONTROLS &&
                                   slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_TOUCH_CONTROLS));
                         }),
          v.end());
  // Tilt page turn needs an IMU with a gyroscope. UI-only, like the frontlight
  // entry: a failed probe on one boot must not drop the saved value.
  if (!halTiltSensor.isAvailable()) {
    v.erase(
        std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_TILT_PAGE_TURN; }),
        v.end());
  }
  // Clock settings need the RTC, as on the device (ClockSettingsActivity).
  if (!halClock.isAvailable()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             switch (s.nameId) {
                               case StrId::STR_CLOCK:
                               case StrId::STR_CLOCK_FORMAT:
                               case StrId::STR_TIMEZONE:
                               case StrId::STR_CLOCK_DST:
                               case StrId::STR_CLOCK_IN_HEADER:
                               case StrId::STR_CLOCK_SYNCED:
                                 return true;
                               default:
                                 return false;
                             }
                           }),
            v.end());
  }
  // Hidden on touch boards: the Home button actions (X4 Pro) or the short
  // power click (other touch boards) cover these shortcuts.
  if (BoardConfig::hasTouch()) {
    v.erase(
        std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_LONG_PRESS_MENU; }),
        v.end());
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}
