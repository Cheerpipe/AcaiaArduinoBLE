#pragma once

#include "../ShotStopperBleCompanion.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

enum class BleCompanionCharacteristic : uint8_t {
  Enabled,
  WeightValue,
  ReedSwitch,
  Momentary,
  AutoTare,
  MinShotDuration,
  MaxShotDuration,
  DripDelay,
  FirmwareVersion,
  ScaleStatus,
  ShotStatus,
  OtaRequested,
  WifiSsid,
  WifiPassword,
  WifiIp,
  Reboot,
  EnableAp,
  Count
};

enum BleCompanionProperty : uint8_t {
  BLE_COMPANION_READ = 1U << 0,
  BLE_COMPANION_WRITE = 1U << 1,
  BLE_COMPANION_NOTIFY = 1U << 2,
};

struct BleCompanionCharacteristicSpec {
  BleCompanionCharacteristic id;
  const char *uuid;
  uint8_t properties;
  uint8_t maxLength;
};

inline constexpr BleCompanionCharacteristicSpec
    BLE_COMPANION_CHARACTERISTICS[] = {
        {BleCompanionCharacteristic::Enabled,
         "00000000-0000-0000-0000-00000000FF10",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::WeightValue,
         "00000000-0000-0000-0000-00000000FF11",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::ReedSwitch,
         "00000000-0000-0000-0000-00000000FF12",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::Momentary,
         "00000000-0000-0000-0000-00000000FF13",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::AutoTare,
         "00000000-0000-0000-0000-00000000FF14",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::MinShotDuration,
         "00000000-0000-0000-0000-00000000FF15",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::MaxShotDuration,
         "00000000-0000-0000-0000-00000000FF16",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::DripDelay,
         "00000000-0000-0000-0000-00000000FF17",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::FirmwareVersion,
         "00000000-0000-0000-0000-00000000FF18", BLE_COMPANION_READ, 1},
        {BleCompanionCharacteristic::ScaleStatus,
         "00000000-0000-0000-0000-00000000FF19",
         BLE_COMPANION_READ | BLE_COMPANION_NOTIFY, 1},
        {BleCompanionCharacteristic::ShotStatus,
         "00000000-0000-0000-0000-00000000FF20",
         BLE_COMPANION_READ | BLE_COMPANION_NOTIFY, 1},
        {BleCompanionCharacteristic::OtaRequested,
         "00000000-0000-0000-0000-00000000FF21",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::WifiSsid,
         "00000000-0000-0000-0000-00000000FF22",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE,
         static_cast<uint8_t>(WIFI_SSID_CAPACITY - 1)},
        {BleCompanionCharacteristic::WifiPassword,
         "00000000-0000-0000-0000-00000000FF23", BLE_COMPANION_WRITE,
         static_cast<uint8_t>(WIFI_PASSWORD_CAPACITY - 1)},
        {BleCompanionCharacteristic::WifiIp,
         "00000000-0000-0000-0000-00000000FF24",
         BLE_COMPANION_READ | BLE_COMPANION_NOTIFY, 15},
        {BleCompanionCharacteristic::Reboot,
         "00000000-0000-0000-0000-00000000FF25", BLE_COMPANION_WRITE, 1},
        {BleCompanionCharacteristic::EnableAp,
         "00000000-0000-0000-0000-00000000FF26",
         BLE_COMPANION_READ | BLE_COMPANION_WRITE | BLE_COMPANION_NOTIFY, 1},
};

static_assert(sizeof(BLE_COMPANION_CHARACTERISTICS) /
                      sizeof(BLE_COMPANION_CHARACTERISTICS[0]) ==
                  static_cast<size_t>(BleCompanionCharacteristic::Count),
              "Companion characteristic table must cover the frozen v2 profile");
static_assert(WIFI_PASSWORD_CAPACITY <= 64,
              "Companion callback scratch buffer assumes <= 64-byte password");

struct BleCompanionProtocolState {
  char stagedSsid[WIFI_SSID_CAPACITY] = {};
  bool wifiStageValid = false;
  uint32_t wifiStageAtMs = 0;
  uint32_t nextSequence = 1;
};

enum class BleCompanionWriteDisposition : uint8_t {
  Enqueue,
  AcceptedNoop,
  Rejected,
};

struct BleCompanionWriteResult {
  BleCompanionWriteDisposition disposition =
      BleCompanionWriteDisposition::Rejected;
  BleCompanionRejectReason reject =
      BleCompanionRejectReason::INVALID_PAYLOAD;
  BleCompanionRequest request = {};
};

inline const BleCompanionCharacteristicSpec *bleCompanionCharacteristicSpec(
    BleCompanionCharacteristic id) {
  const size_t index = static_cast<size_t>(id);
  return index < static_cast<size_t>(BleCompanionCharacteristic::Count)
             ? &BLE_COMPANION_CHARACTERISTICS[index]
             : nullptr;
}

inline void clearBleCompanionWifiStage(BleCompanionProtocolState &state) {
  memset(state.stagedSsid, 0, sizeof(state.stagedSsid));
  state.wifiStageValid = false;
  state.wifiStageAtMs = 0;
}

inline void expireBleCompanionWifiStage(BleCompanionProtocolState &state,
                                        uint32_t nowMs) {
  if (state.wifiStageValid &&
      static_cast<uint32_t>(nowMs - state.wifiStageAtMs) >=
          BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS) {
    clearBleCompanionWifiStage(state);
  }
}

inline uint32_t nextBleCompanionSequence(BleCompanionProtocolState &state) {
  const uint32_t sequence = state.nextSequence++;
  if (state.nextSequence == 0) {
    state.nextSequence = 1;
  }
  return sequence == 0 ? nextBleCompanionSequence(state) : sequence;
}

inline BleCompanionWriteResult processBleCompanionWrite(
    BleCompanionProtocolState &state, BleCompanionCharacteristic id,
    const uint8_t *data, size_t length, uint32_t nowMs) {
  BleCompanionWriteResult result;
  const BleCompanionCharacteristicSpec *spec =
      bleCompanionCharacteristicSpec(id);
  if (spec == nullptr || (spec->properties & BLE_COMPANION_WRITE) == 0 ||
      length > spec->maxLength || (length > 0 && data == nullptr)) {
    if (id == BleCompanionCharacteristic::WifiPassword) {
      clearBleCompanionWifiStage(state);
    }
    return result;
  }

  if (id == BleCompanionCharacteristic::WifiSsid) {
    clearBleCompanionWifiStage(state);
    if (length == 0 || length >= WIFI_SSID_CAPACITY) {
      result.reject = BleCompanionRejectReason::INVALID_VALUE;
      return result;
    }
    memcpy(state.stagedSsid, data, length);
    state.stagedSsid[length] = '\0';
    state.wifiStageValid = true;
    state.wifiStageAtMs = nowMs;
    result.disposition = BleCompanionWriteDisposition::AcceptedNoop;
    result.reject = BleCompanionRejectReason::NONE;
    return result;
  }

  if (id == BleCompanionCharacteristic::WifiPassword) {
    if (!state.wifiStageValid) {
      result.reject = BleCompanionRejectReason::WIFI_STAGE_MISSING;
      return result;
    }
    result.request.type = BleCompanionRequestType::SAVE_WIFI;
    result.request.sequence = nextBleCompanionSequence(state);
    copyCString(result.request.ssid, sizeof(result.request.ssid),
                state.stagedSsid);
    result.request.openNetwork = length == 0;
    if (length > 0) {
      memcpy(result.request.password, data, length);
      result.request.password[length] = '\0';
    }
    clearBleCompanionWifiStage(state);
    result.disposition = BleCompanionWriteDisposition::Enqueue;
    result.reject = BleCompanionRejectReason::NONE;
    return result;
  }

  if (length != 1) {
    return result;
  }

  result.request.value = data[0];
  switch (id) {
    case BleCompanionCharacteristic::Enabled:
      result.request.type = BleCompanionRequestType::SET_BREW_BY_WEIGHT;
      break;
    case BleCompanionCharacteristic::WeightValue:
      result.request.type = BleCompanionRequestType::SET_GOAL_WEIGHT;
      break;
    case BleCompanionCharacteristic::AutoTare:
      result.request.type = BleCompanionRequestType::SET_AUTO_TARE;
      break;
    case BleCompanionCharacteristic::MinShotDuration:
      result.request.type =
          BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS;
      break;
    case BleCompanionCharacteristic::MaxShotDuration:
      result.request.type =
          BleCompanionRequestType::SET_OPERATIONAL_WALL_SECONDS;
      break;
    case BleCompanionCharacteristic::DripDelay:
      result.request.type = BleCompanionRequestType::SET_DRIP_DELAY_SECONDS;
      break;
    case BleCompanionCharacteristic::Reboot:
      if (data[0] != 1) {
        return result;
      }
      result.request.type = BleCompanionRequestType::REBOOT;
      break;
    case BleCompanionCharacteristic::EnableAp:
      result.request.type = BleCompanionRequestType::SET_AP_ENABLED;
      break;
    case BleCompanionCharacteristic::ReedSwitch:
    case BleCompanionCharacteristic::Momentary:
    case BleCompanionCharacteristic::OtaRequested:
      result.disposition = BleCompanionWriteDisposition::AcceptedNoop;
      result.reject = BleCompanionRejectReason::NONE;
      return result;
    default:
      return result;
  }

  result.request.sequence = nextBleCompanionSequence(state);
  result.disposition = BleCompanionWriteDisposition::Enqueue;
  result.reject = BleCompanionRejectReason::NONE;
  return result;
}

inline size_t readBleCompanionValue(BleCompanionCharacteristic id,
                                    const BleCompanionRuntimeSnapshot &snapshot,
                                    uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity == 0) {
    return 0;
  }
  switch (id) {
    case BleCompanionCharacteristic::Enabled:
      output[0] = snapshot.brewByWeight ? 1 : 0;
      return 1;
    case BleCompanionCharacteristic::WeightValue:
      output[0] = snapshot.goalWeightG;
      return 1;
    case BleCompanionCharacteristic::ReedSwitch:
    case BleCompanionCharacteristic::Momentary:
    case BleCompanionCharacteristic::OtaRequested:
      output[0] = 0;
      return 1;
    case BleCompanionCharacteristic::AutoTare:
      output[0] = snapshot.autoTare ? 1 : 0;
      return 1;
    case BleCompanionCharacteristic::MinShotDuration:
      output[0] = bleCompanionMsToSeconds(snapshot.bbwProtectionMs);
      return 1;
    case BleCompanionCharacteristic::MaxShotDuration:
      output[0] = bleCompanionMsToSeconds(snapshot.operationalWallMs);
      return 1;
    case BleCompanionCharacteristic::DripDelay:
      output[0] = bleCompanionMsToSeconds(snapshot.dripDelayMs);
      return 1;
    case BleCompanionCharacteristic::FirmwareVersion:
      output[0] = BLE_COMPANION_PROTOCOL_VERSION;
      return 1;
    case BleCompanionCharacteristic::ScaleStatus:
      output[0] = snapshot.scaleConnected ? 1 : 0;
      return 1;
    case BleCompanionCharacteristic::ShotStatus:
      output[0] = snapshot.shotActive ? 1 : 0;
      return 1;
    case BleCompanionCharacteristic::WifiSsid: {
      const size_t length = strnlen(snapshot.wifiSsid, sizeof(snapshot.wifiSsid));
      if (length > capacity) return 0;
      memcpy(output, snapshot.wifiSsid, length);
      return length;
    }
    case BleCompanionCharacteristic::WifiIp: {
      const size_t length = strnlen(snapshot.wifiIp, sizeof(snapshot.wifiIp));
      if (length > capacity) return 0;
      memcpy(output, snapshot.wifiIp, length);
      return length;
    }
    case BleCompanionCharacteristic::EnableAp:
      output[0] = snapshot.apActive ? 1 : 0;
      return 1;
    case BleCompanionCharacteristic::WifiPassword:
    case BleCompanionCharacteristic::Reboot:
    case BleCompanionCharacteristic::Count:
      return 0;
  }
  return 0;
}

inline bool bleCompanionNotificationChanged(
    BleCompanionCharacteristic id, const BleCompanionRuntimeSnapshot &before,
    const BleCompanionRuntimeSnapshot &after, bool force) {
  if (force) return true;
  switch (id) {
    case BleCompanionCharacteristic::ScaleStatus:
      return before.scaleConnected != after.scaleConnected;
    case BleCompanionCharacteristic::ShotStatus:
      return before.shotActive != after.shotActive;
    case BleCompanionCharacteristic::WifiIp:
      return strcmp(before.wifiIp, after.wifiIp) != 0;
    case BleCompanionCharacteristic::EnableAp:
      return before.apActive != after.apActive;
    default:
      return false;
  }
}

}  // namespace shotstopper
