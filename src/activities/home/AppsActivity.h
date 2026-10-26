#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Home > Apps: other firmware installed next to CrossPoint (dual boot).
// Choosing one makes it the boot target and restarts into it.
class AppsActivity final : public UiListActivity {
 public:
  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  static constexpr int kMaxApps = 1;  // two OTA slots: CrossPoint and one other app

  int listCount() const override { return count; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  int count = 0;
  char names[kMaxApps][48]{};
  char versions[kMaxApps][32]{};
  freeink::ui::ListItem rowItems[kMaxApps]{};
};
