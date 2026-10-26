#include "AppsActivity.h"

#include <I18n.h>
#include <esp_ota_ops.h>

#include <cstring>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "network/DualBoot.h"

namespace fui = freeink::ui;

AppsActivity::AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Apps", renderer, mappedInput) {}

void AppsActivity::onEnter() {
  UiListActivity::onEnter();

  count = 0;
  if (const esp_partition_t* other = dual_boot::otherApp()) {
    snprintf(names[0], sizeof(names[0]), "%s", dual_boot::otherAppName().c_str());
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) == ESP_OK) {
      snprintf(versions[0], sizeof(versions[0]), "%s", desc.version);
    }
    fui::ListItem item;
    item.label = names[0];
    item.value = versions[0];
    item.actionValue = 0;
    rowItems[0] = item;
    count = 1;
  }
  nav.selected = 0;
}

const char* AppsActivity::headerTitle() const { return tr(STR_APPS); }

void AppsActivity::onBackButton() { activityManager.goHome(HomeMenuItem::APPS); }

void AppsActivity::activateIndex(const int index) {
  if (index < 0 || index >= count) return;
  app.clearTapFlash();
  dual_boot::switchToOther();  // restarts on success; stays here on failure
}

void AppsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
