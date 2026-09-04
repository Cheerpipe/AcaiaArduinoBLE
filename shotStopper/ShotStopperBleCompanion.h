#pragma once

#include "ShotStopperDomain.h"

#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

constexpr const char *BLE_COMPANION_LOCAL_NAME = "shotStopper";
constexpr const char *BLE_COMPANION_SERVICE_UUID =
    "00000000-0000-0000-0000-000000000FFE";
constexpr uint8_t BLE_COMPANION_PROTOCOL_VERSION = 2;
constexpr uint32_t BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS = 30000;
constexpr uint32_t BLE_COMPANION_REQUEST_TIMEOUT_MS = 10000;
// 0x0200 * 0.625 ms = 320 ms. Idle advertise does not need the ~100 ms default.
constexpr uint16_t BLE_COMPANION_ADV_INTERVAL = 0x0200;

enum class BleCompanionRequestType : uint8_t {
  SET_BREW_BY_WEIGHT,
  SET_GOAL_WEIGHT,
  SET_AUTO_TARE,
  SET_BBW_PROTECTION_SECONDS,
  SET_OPERATIONAL_WALL_SECONDS,
  SET_DRIP_DELAY_SECONDS,
  SAVE_WIFI,
  REBOOT,
  SET_AP_ENABLED
};

enum class BleCompanionRejectReason : uint8_t {
  NONE,
  SUPPORT_DISABLED,
  NOT_READY,
  INVALID_PAYLOAD,
  INVALID_VALUE,
  WIFI_STAGE_MISSING,
  QUEUE_FULL,
  PERSIST_FAILED,
  COMMAND_REJECTED,
  REQUEST_PENDING,
  ALLOCATION_FAILED
};

inline const char *bleCompanionRejectReasonName(
    BleCompanionRejectReason reason) {
  switch (reason) {
    case BleCompanionRejectReason::NONE: return "none";
    case BleCompanionRejectReason::SUPPORT_DISABLED: return "disabled";
    case BleCompanionRejectReason::NOT_READY: return "not_ready";
    case BleCompanionRejectReason::INVALID_PAYLOAD: return "invalid_payload";
    case BleCompanionRejectReason::INVALID_VALUE: return "invalid_value";
    case BleCompanionRejectReason::WIFI_STAGE_MISSING:
      return "wifi_stage_missing";
    case BleCompanionRejectReason::QUEUE_FULL: return "queue_full";
    case BleCompanionRejectReason::PERSIST_FAILED: return "persist_failed";
    case BleCompanionRejectReason::COMMAND_REJECTED:
      return "command_rejected";
    case BleCompanionRejectReason::REQUEST_PENDING: return "request_pending";
    case BleCompanionRejectReason::ALLOCATION_FAILED:
      return "allocation_failed";
  }
  return "unknown";
}

struct BleCompanionRequest {
  BleCompanionRequestType type =
      BleCompanionRequestType::SET_BREW_BY_WEIGHT;
  uint32_t sequence = 0;
  uint8_t value = 0;
  char ssid[WIFI_SSID_CAPACITY] = {};
  char password[WIFI_PASSWORD_CAPACITY] = {};
  bool openNetwork = false;
};

struct BleCompanionResult {
  uint32_t sequence = 0;
  bool accepted = false;
  BleCompanionRejectReason reason = BleCompanionRejectReason::NONE;
};

struct BleCompanionRuntimeSnapshot {
  // `enabled` is immutable for a boot: it says whether the GATT profile was
  // constructed. `configuredEnabled` is the persisted next-boot setting.
  bool enabled = false;
  bool configuredEnabled = false;
  bool configurationAllowed = false;
  bool brewByWeight = true;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  bool autoTare = true;
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  uint32_t dripDelayMs = DEFAULT_DRIP_DELAY_MS;
  bool scaleConnected = false;
  bool shotActive = false;
  bool apActive = false;
  char wifiSsid[WIFI_SSID_CAPACITY] = {};
  char wifiIp[16] = {};
};

struct BleCompanionStatusSnapshot {
  bool enabled = false;
  bool configuredEnabled = false;
  bool restartRequired = false;
  bool stackReady = false;
  bool advertising = false;
  bool connected = false;
  uint8_t protocolVersion = BLE_COMPANION_PROTOCOL_VERSION;
  bool apActive = false;
  uint32_t acceptedWrites = 0;
  uint32_t rejectedWrites = 0;
  BleCompanionRejectReason lastReject = BleCompanionRejectReason::NONE;
  int32_t lastRawError = 0;
  uint32_t advertisingStarts = 0;
  uint32_t advertisingFailures = 0;
  uint32_t phoneConnects = 0;
  uint32_t phoneDisconnects = 0;
};

inline bool bleCompanionSecondsToMs(uint8_t seconds, uint32_t &outMs) {
  outMs = static_cast<uint32_t>(seconds) * 1000U;
  return true;
}

inline uint8_t bleCompanionMsToSeconds(uint32_t milliseconds) {
  const uint32_t seconds = milliseconds / 1000U;
  return static_cast<uint8_t>(seconds > 255U ? 255U : seconds);
}

}  // namespace shotstopper

#if !defined(SHOT_STOPPER_HOST_TEST)
#include "ble/ShotStopperBleCompanionNimble.h"
#endif
