#define SHOT_STOPPER_HOST_TEST 1

#include "../ble/ShotStopperBleCompanionProtocol.h"
#include "../ble/ShotStopperBleRadioPolicy.h"

#include <stdio.h>
#include <string.h>

using namespace shotstopper;

namespace {
int checks = 0;
int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    ++checks;                                                                \
    if (!(condition)) {                                                      \
      ++failures;                                                            \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    }                                                                        \
  } while (0)

void checkFrozenProfile() {
  CHECK(strcmp(BLE_COMPANION_SERVICE_UUID,
               "00000000-0000-0000-0000-000000000FFE") == 0);
  CHECK(BLE_COMPANION_PROTOCOL_VERSION == 2);
  CHECK(static_cast<size_t>(BleCompanionCharacteristic::Count) == 17);
  for (size_t index = 0;
       index < static_cast<size_t>(BleCompanionCharacteristic::Count);
       ++index) {
    const auto *spec = bleCompanionCharacteristicSpec(
        static_cast<BleCompanionCharacteristic>(index));
    CHECK(spec != nullptr);
    CHECK(strlen(spec->uuid) == 36);
    CHECK(spec->uuid[32] == 'F' && spec->uuid[33] == 'F');
  }
  CHECK(BLE_COMPANION_CHARACTERISTICS[12].maxLength == 32);
  CHECK(BLE_COMPANION_CHARACTERISTICS[13].maxLength == 63);
  CHECK(BLE_COMPANION_CHARACTERISTICS[14].maxLength == 15);
  CHECK((BLE_COMPANION_CHARACTERISTICS[9].properties &
         BLE_COMPANION_NOTIFY) != 0);
  CHECK((BLE_COMPANION_CHARACTERISTICS[10].properties &
         BLE_COMPANION_NOTIFY) != 0);
  CHECK((BLE_COMPANION_CHARACTERISTICS[14].properties &
         BLE_COMPANION_NOTIFY) != 0);
  CHECK((BLE_COMPANION_CHARACTERISTICS[16].properties &
         BLE_COMPANION_NOTIFY) != 0);
}

void checkWritesAndStaging() {
  BleCompanionProtocolState state;
  const uint8_t goal[] = {42};
  auto result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WeightValue, goal, sizeof(goal), 1);
  CHECK(result.disposition == BleCompanionWriteDisposition::Enqueue);
  CHECK(result.request.type == BleCompanionRequestType::SET_GOAL_WEIGHT);
  CHECK(result.request.value == 42);
  CHECK(result.request.sequence == 1);

  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WeightValue, goal, 0, 2);
  CHECK(result.disposition == BleCompanionWriteDisposition::Rejected);
  CHECK(result.reject == BleCompanionRejectReason::INVALID_PAYLOAD);

  const uint8_t badReboot[] = {2};
  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::Reboot, badReboot, 1, 3);
  CHECK(result.disposition == BleCompanionWriteDisposition::Rejected);

  const uint8_t ssid[] = {'C', 'a', 'f', 'e'};
  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WifiSsid, ssid, sizeof(ssid), 4);
  CHECK(result.disposition == BleCompanionWriteDisposition::AcceptedNoop);
  CHECK(state.wifiStageValid);
  CHECK(strcmp(state.stagedSsid, "Cafe") == 0);

  const uint8_t password[] = {'s', 'e', 'c', 'r', 'e', 't'};
  result = processBleCompanionWrite(state,
                                    BleCompanionCharacteristic::WifiPassword,
                                    password, sizeof(password), 5);
  CHECK(result.disposition == BleCompanionWriteDisposition::Enqueue);
  CHECK(result.request.type == BleCompanionRequestType::SAVE_WIFI);
  CHECK(strcmp(result.request.ssid, "Cafe") == 0);
  CHECK(strcmp(result.request.password, "secret") == 0);
  CHECK(!result.request.openNetwork);
  CHECK(!state.wifiStageValid);

  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WifiSsid, ssid, sizeof(ssid), 5);
  uint8_t oversizedPassword[WIFI_PASSWORD_CAPACITY] = {};
  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WifiPassword, oversizedPassword,
      sizeof(oversizedPassword), 5);
  CHECK(result.disposition == BleCompanionWriteDisposition::Rejected);
  CHECK(!state.wifiStageValid);

  result = processBleCompanionWrite(state,
                                    BleCompanionCharacteristic::WifiPassword,
                                    nullptr, 0, 6);
  CHECK(result.reject == BleCompanionRejectReason::WIFI_STAGE_MISSING);

  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WifiSsid, ssid, sizeof(ssid), 7);
  CHECK(state.wifiStageValid);
  expireBleCompanionWifiStage(
      state, 7 + BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS - 1);
  CHECK(state.wifiStageValid);
  expireBleCompanionWifiStage(
      state, 7 + BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS);
  CHECK(!state.wifiStageValid);

  result = processBleCompanionWrite(
      state, BleCompanionCharacteristic::WifiSsid, ssid, sizeof(ssid),
      UINT32_MAX - 5);
  CHECK(state.wifiStageValid);
  expireBleCompanionWifiStage(
      state, static_cast<uint32_t>(UINT32_MAX - 5 +
                                   BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS));
  CHECK(!state.wifiStageValid);
}

void checkReadsAndNotifications() {
  BleCompanionRuntimeSnapshot snapshot;
  snapshot.brewByWeight = true;
  snapshot.goalWeightG = 37;
  snapshot.autoTare = false;
  snapshot.bbwProtectionMs = 7000;
  snapshot.operationalWallMs = 90000;
  snapshot.dripDelayMs = 2500;
  snapshot.scaleConnected = true;
  snapshot.shotActive = true;
  snapshot.apActive = true;
  copyCString(snapshot.wifiSsid, sizeof(snapshot.wifiSsid), "Cafe");
  copyCString(snapshot.wifiIp, sizeof(snapshot.wifiIp), "192.168.1.20");

  uint8_t value[64] = {};
  CHECK(readBleCompanionValue(BleCompanionCharacteristic::Enabled, snapshot,
                              value, sizeof(value)) == 1 &&
        value[0] == 1);
  CHECK(readBleCompanionValue(BleCompanionCharacteristic::WeightValue,
                              snapshot, value, sizeof(value)) == 1 &&
        value[0] == 37);
  CHECK(readBleCompanionValue(BleCompanionCharacteristic::MinShotDuration,
                              snapshot, value, sizeof(value)) == 1 &&
        value[0] == 7);
  CHECK(readBleCompanionValue(BleCompanionCharacteristic::WifiSsid, snapshot,
                              value, sizeof(value)) == 4);
  CHECK(memcmp(value, "Cafe", 4) == 0);
  CHECK(readBleCompanionValue(BleCompanionCharacteristic::WifiPassword,
                              snapshot, value, sizeof(value)) == 0);

  BleCompanionRuntimeSnapshot before = snapshot;
  CHECK(!bleCompanionNotificationChanged(
      BleCompanionCharacteristic::ScaleStatus, before, snapshot, false));
  snapshot.scaleConnected = false;
  CHECK(bleCompanionNotificationChanged(
      BleCompanionCharacteristic::ScaleStatus, before, snapshot, false));
  CHECK(bleCompanionNotificationChanged(
      BleCompanionCharacteristic::WifiIp, before, snapshot, true));
}

void checkRadioPolicy() {
  BleRadioPolicyInputs inputs;
  CHECK(!bleRadioPolicyPauseCompanionAdvertising(inputs));
  inputs.scaleConnecting = true;
  CHECK(bleRadioPolicyPauseCompanionAdvertising(inputs));
  inputs = {};
  inputs.scaleLinked = true;
  CHECK(bleRadioPolicyPauseCompanionAdvertising(inputs));
  inputs = {};
  inputs.machineCircuitClosed = true;
  CHECK(bleRadioPolicyPauseCompanionAdvertising(inputs));
  inputs = {};
  inputs.scaleHuntRfClear = true;
  CHECK(bleRadioPolicyPauseCompanionAdvertising(inputs));
}
}  // namespace

int main() {
  checkFrozenProfile();
  checkWritesAndStaging();
  checkReadsAndNotifications();
  checkRadioPolicy();
  printf("BLE Companion protocol tests: %d checks, %d failures\n", checks,
         failures);
  return failures == 0 ? 0 : 1;
}
