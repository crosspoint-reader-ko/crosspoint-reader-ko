#include "ReaderOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

ReaderOptionsActivity::ReaderOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forTxtReader)
    : UiListActivity("ReaderOptions", renderer, mappedInput), forTxtReader(forTxtReader) {}

std::vector<SettingInfo> ReaderOptionsActivity::buildSettings(bool forTxtReader) {
  std::vector<SettingInfo> result;
  result.reserve(32);

  for (const auto& s : getSettingsList()) {
    if (s.type != SettingType::TOGGLE && s.type != SettingType::ENUM && s.type != SettingType::VALUE) {
      continue;
    }
    if (s.valuePtr == nullptr) {
      continue;
    }
    // The reader menu already exposes orientation, so omit it here to avoid
    // duplicate exposure with subtly different UX.
    if (s.nameId == StrId::STR_ORIENTATION) {
      continue;
    }
    // Drop EPUB-only settings when hosted by the TXT reader: TXT files have
    // no embedded images and no HTML/CSS styling, so these toggles do
    // nothing and would mislead the user.
    if (forTxtReader && (s.nameId == StrId::STR_IMAGES || s.nameId == StrId::STR_EMBEDDED_STYLE)) {
      continue;
    }
    if (s.category == StrId::STR_CAT_READER || s.category == StrId::STR_CAT_CONTROLS) {
      result.push_back(s);
    }
  }

  const auto& list = getSettingsList();
  const auto fading = std::find_if(list.begin(), list.end(), [](const SettingInfo& s) {
    return s.nameId == StrId::STR_SUNLIGHT_FADING_FIX && s.valuePtr != nullptr;
  });
  if (fading != list.end()) {
    result.push_back(*fading);
  }
  return result;
}

void ReaderOptionsActivity::onEnter() {
  settings = buildSettings(forTxtReader);
  rowItems.assign(settings.size(), fui::ListItem{});
  valueTexts.assign(settings.size(), std::string());
  for (size_t i = 0; i < settings.size(); i++) {
    rowItems[i].label = I18N.get(settings[i].nameId);
    rowItems[i].actionValue = static_cast<int16_t>(i);
    rowItems[i].toggle = settings[i].type == SettingType::TOGGLE;
  }
  UiListActivity::onEnter();
}

void ReaderOptionsActivity::onExit() {
  // Persist any changes before returning to the reader. The reader detects
  // render-affecting changes when its menu closes and re-lays out as needed.
  SETTINGS.saveToFile();
  UiListActivity::onExit();
}

void ReaderOptionsActivity::activateIndex(const int index) {
  nav.selected = index;
  toggleSetting(index);
  requestUpdate();
}

void ReaderOptionsActivity::toggleSetting(const int index) {
  if (index < 0 || index >= static_cast<int>(settings.size())) {
    return;
  }
  const auto& setting = settings[index];
  if (setting.valuePtr == nullptr) return;

  // Mirrors SettingsActivity's toggle/cycle semantics: the shared SettingInfo
  // entries must behave the same on both screens.
  if (setting.type == SettingType::TOGGLE) {
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const size_t count = setting.enumLabels().size();
    if (count == 0) return;
    SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>((currentValue + 1) % count);
  } else if (setting.type == SettingType::VALUE) {
    // int, not int8_t: VALUE settings such as wordSpacing (50..200 %) exceed the int8_t range.
    const int currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(currentValue + setting.valueRange.step);
    }
  }
}

void ReaderOptionsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Labels were set in onEnter(); only the live values are refreshed here.
  for (size_t i = 0; i < settings.size(); i++) {
    const auto& setting = settings[i];
    auto& item = rowItems[i];
    item.value = nullptr;
    if (setting.type == SettingType::TOGGLE) {
      item.toggleChecked = SETTINGS.*(setting.valuePtr) != 0;
    } else if (setting.type == SettingType::ENUM) {
      const uint8_t value = SETTINGS.*(setting.valuePtr);
      const auto labels = setting.enumLabels();
      if (value < labels.size()) item.value = I18N.get(labels[value]);
    } else if (setting.type == SettingType::VALUE) {
      valueTexts[i] = std::to_string(SETTINGS.*(setting.valuePtr));
      if (setting.nameId == StrId::STR_WORD_SPACING) valueTexts[i] += "%";
      item.value = valueTexts[i].c_str();
    }
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void ReaderOptionsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
