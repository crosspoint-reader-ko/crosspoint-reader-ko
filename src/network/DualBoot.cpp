#include "DualBoot.h"

#include <Logging.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "OtaBootSwitch.h"

namespace dual_boot {

namespace {

constexpr const char* kNamespace = "ota_names";
constexpr const char* kSelfName = "CrossPoint";

void slotKey(const esp_partition_t* part, char* key, size_t len) {
  snprintf(key, len, "ota_%d", static_cast<int>(part->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0));
}

}  // namespace

void registerSelf() {
  const esp_partition_t* self = esp_ota_get_running_partition();
  if (!self || self->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 || self->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_15) {
    return;
  }
  char key[8];
  slotKey(self, key, sizeof(key));
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  // Skip the write when the name is already there; NVS writes wear flash.
  if (prefs.getString(key, "") != kSelfName) {
    prefs.putString(key, kSelfName);
    LOG_INF("DUAL", "registered %s in %s", kSelfName, key);
  }
  prefs.end();
}

const esp_partition_t* otherApp() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (!running || !other || other == running) return nullptr;

  esp_app_desc_t mine;
  esp_app_desc_t theirs;
  if (esp_ota_get_partition_description(running, &mine) != ESP_OK) return nullptr;
  if (esp_ota_get_partition_description(other, &theirs) != ESP_OK) return nullptr;
  if (strncmp(mine.project_name, theirs.project_name, sizeof(mine.project_name)) == 0) return nullptr;
  return other;
}

std::string otherAppName() {
  const esp_partition_t* other = otherApp();
  if (!other) return "";
  char key[8];
  slotKey(other, key, sizeof(key));
  Preferences prefs;
  std::string name;
  if (prefs.begin(kNamespace, true)) {
    name = prefs.getString(key, "").c_str();
    prefs.end();
  }
  if (name.empty()) {
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) == ESP_OK) {
      name.assign(desc.project_name, strnlen(desc.project_name, sizeof(desc.project_name)));
    }
  }
  return name;
}

bool switchToOther() {
  const esp_partition_t* other = otherApp();
  if (!other) return false;
  if (!ota_boot::switchTo(other)) {
    LOG_ERR("DUAL", "otadata write failed");
    return false;
  }
  LOG_INF("DUAL", "switching to slot at 0x%x", static_cast<unsigned>(other->address));
  esp_restart();
  return true;
}

}  // namespace dual_boot
