#pragma once

#include <esp_partition.h>

#include <string>

// Dual-boot with another app (e.g. MicroSlate) flashed into the other OTA slot.
//
// Both apps keep a display name per slot in the shared NVS namespace
// "ota_names" (key "ota_<slot>"), so each can show the other by name.
// An app counts as "other" when its image header carries a different
// project_name than the running one. A CrossPoint update written to the
// other slot would erase that app, so the update paths refuse it.

namespace dual_boot {

// Record "CrossPoint" as the name of the running slot.
void registerSelf();

// The other OTA slot when it holds a different, valid app; nullptr otherwise.
const esp_partition_t* otherApp();

// Display name of otherApp(): its NVS name, else its project_name.
std::string otherAppName();

// Make the other app the boot target and restart. Returns false on failure.
bool switchToOther();

}  // namespace dual_boot
