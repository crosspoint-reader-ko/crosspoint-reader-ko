#pragma once

// Copy of freeink-sdk 084fdfb libs/network/BleKeyboardHost/include/BleRemoteProfiles.h.
// The pinned SDK predates it; delete this file when the pin moves past 084fdfb.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace freeink {

// Logical keys for known non-keyboard reports. Apps choose their actions; raw
// report identities stay unchanged so saved per-button bindings remain valid.
enum class RemoteKey : uint8_t { Right = 0, Left = 1, PageDown = 2, PageUp = 3, Count = 4 };

struct RemoteButtonBinding {
  uint32_t code;  // RawButtonEvent::code()
  RemoteKey key;
  bool anyReport = false;
};

struct RemoteButtonProfile {
  const char* namePrefix;
  uint8_t count;
  RemoteButtonBinding buttons[4];
};

inline constexpr RemoteButtonProfile kRemoteButtonProfiles[] = {
    // Free3's third button reports byte 1 on tap, byte 0 on hold. Its page
    // buttons keep using the generic HID decode. Report IDs vary by model.
    {"Free3", 2, {{0x000102, RemoteKey::PageDown, true}, {0x000008, RemoteKey::PageUp, true}}},
    // BOOX Keypage has two observed code sets, both on report 3.
    {"BOOX Keypage",
     4,
     {{0x030002, RemoteKey::Right},
      {0x030001, RemoteKey::Left},
      {0x030004, RemoteKey::Right},
      {0x030008, RemoteKey::Left}}},
};

// Index into kRemoteButtonProfiles, or -1 when generic HID decoding applies.
inline int remoteButtonProfileIndex(const char* name) {
  if (name == nullptr) return -1;
  for (size_t i = 0; i < sizeof(kRemoteButtonProfiles) / sizeof(kRemoteButtonProfiles[0]); ++i) {
    const char* prefix = kRemoteButtonProfiles[i].namePrefix;
    if (strncmp(name, prefix, strlen(prefix)) == 0) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace freeink
