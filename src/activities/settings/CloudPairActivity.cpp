#include "CloudPairActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/CloudClient.h"

namespace fui = freeink::ui;

std::string CloudPairActivity::lastEnteredCode;

namespace {
std::string truncatedMid(const std::string& s, size_t maxLen) {
  if (s.size() <= maxLen) return s;
  if (maxLen <= 3) return std::string(maxLen, '.');
  const size_t keep = (maxLen - 1) / 2;
  return s.substr(0, keep) + "…" + s.substr(s.size() - (maxLen - 1 - keep));
}
}  // namespace

CloudPairActivity::CloudPairActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("CloudPair", renderer, mappedInput) {
  for (int i = 0; i < MAX_ITEMS; i++) rowItems_[i].actionValue = static_cast<int16_t>(i);
}

void CloudPairActivity::onExit() {
  UiListActivity::onExit();
  // Pairing may have brought WiFi up through WifiSelectionActivity. Like the
  // other settings rows that do, release the driver's heap with a restart.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToSettings();
  }
}

int CloudPairActivity::listCount() const { return CloudClient::getInstance().isPaired() ? 3 : 2; }

const char* CloudPairActivity::headerTitle() const { return tr(STR_CLOUD_PAIRING); }

void CloudPairActivity::activateIndex(const int index) {
  app.clearTapFlash();
  switch (index) {
    case 0:
      editServerUrl();
      break;
    case 1:
      // Once paired this row only reports state; re-pairing needs Forget
      // first so a working token is not replaced by accident.
      if (!CloudClient::getInstance().isPaired()) enterPairingCode();
      break;
    case 2:
      doForget();
      break;
    default:
      break;
  }
}

void CloudPairActivity::editServerUrl() {
  const auto& current = CloudClient::getInstance().getServerUrl();
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CLOUD_ENTER_URL),
                                                                 current, 127, InputType::Url),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto& kb = std::get<KeyboardResult>(result.data);
                           if (kb.text.empty()) return;
                           statusBanner = CloudClient::getInstance().setServerUrl(kb.text)
                                              ? std::string()
                                              : std::string(tr(STR_CLOUD_PAIRING_FAILED));
                           requestUpdate();
                         });
}

void CloudPairActivity::enterPairingCode() {
  if (CloudClient::getInstance().getServerUrl().empty()) {
    statusBanner = tr(STR_CLOUD_URL_NEEDED);
    requestUpdate();
    return;
  }
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CLOUD_ENTER_CODE),
                                                                 lastEnteredCode, 6, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto& kb = std::get<KeyboardResult>(result.data);
                           if (kb.text.empty()) return;
                           // The server stores codes uppercase.
                           std::string code = kb.text;
                           for (auto& c : code) {
                             if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                           }
                           lastEnteredCode = code;

                           if (WiFi.status() == WL_CONNECTED) {
                             executePairing(code);
                             return;
                           }
                           // Detour through WiFi setup, then pair with the same code.
                           statusBanner = tr(STR_CLOUD_WIFI_NEEDED);
                           requestUpdate();
                           startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true),
                                                  [this, code](const ActivityResult&) {
                                                    if (WiFi.status() != WL_CONNECTED) {
                                                      statusBanner = tr(STR_CLOUD_WIFI_NEEDED);
                                                      requestUpdate();
                                                      return;
                                                    }
                                                    executePairing(code);
                                                  });
                         });
}

void CloudPairActivity::executePairing(const std::string& code) {
  statusBanner = tr(STR_CLOUD_PAIRING_IN_PROGRESS);
  requestUpdate(true);  // paint "pairing…" before the blocking HTTP call

  // The last MAC bytes keep names distinct when one owner pairs several readers.
  const auto& hwId = CloudClient::getInstance().getHardwareId();
  const std::string suffix = hwId.size() >= 4 ? hwId.substr(hwId.size() - 4) : hwId;
  const bool ok = CloudClient::getInstance().pairWithCode(code, "CrossPoint-" + suffix);
  statusBanner = ok ? tr(STR_CLOUD_PAIRING_SUCCESS) : tr(STR_CLOUD_PAIRING_FAILED);
  if (ok) moveSelectionTo(0);
  requestUpdate();
}

void CloudPairActivity::doForget() {
  if (CloudClient::getInstance().forgetDevice()) {
    statusBanner.clear();
    moveSelectionTo(0);
    requestUpdate();
  }
}

void CloudPairActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  auto& cc = CloudClient::getInstance();
  const bool paired = cc.isPaired();
  const int count = listCount();

  rowItems_[0].label = I18N.get(StrId::STR_CLOUD_SERVER_URL);
  rowValues_[0] = cc.getServerUrl().empty() ? std::string(tr(STR_NOT_SET)) : truncatedMid(cc.getServerUrl(), 32);

  rowItems_[1].label = I18N.get(paired ? StrId::STR_CLOUD_STATUS : StrId::STR_CLOUD_PAIR_DEVICE);
  if (!statusBanner.empty()) {
    rowValues_[1] = statusBanner;
  } else {
    rowValues_[1] = paired ? tr(STR_CLOUD_PAIRED) : tr(STR_CLOUD_NOT_PAIRED);
  }

  rowItems_[2].label = I18N.get(StrId::STR_CLOUD_FORGET_DEVICE);
  rowValues_[2] = truncatedMid(cc.getDeviceId(), 24);

  for (int i = 0; i < count; i++) {
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
