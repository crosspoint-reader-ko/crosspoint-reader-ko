#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Settings → Cloud. Shows the server URL and pairing state, runs the pairing
// exchange with a 6-char code from the dashboard, and unpairs. The tunnel
// itself only runs inside File Transfer.
class CloudPairActivity final : public UiListActivity {
 public:
  explicit CloudPairActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onExit() override;

  static constexpr int MAX_ITEMS = 3;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void editServerUrl();
  void enterPairingCode();
  void executePairing(const std::string& code);
  void doForget();

  // Transient result of the last action (pairing…, paired, failed), shown as
  // the value of the pairing row until the next action.
  std::string statusBanner;

  // Persisted across activity entries (not reboots: codes expire in 10
  // minutes) so a failed attempt can be retried without retyping.
  static std::string lastEnteredCode;

  std::string rowValues_[MAX_ITEMS];
  freeink::ui::ListItem rowItems_[MAX_ITEMS]{};
};
