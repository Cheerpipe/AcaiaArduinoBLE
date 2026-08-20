#pragma once

#include "ShotStopperDomain.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(SHOT_STOPPER_HOST_TEST)
#include <ArduinoBLE.h>
#endif

namespace shotstopper {

constexpr const char *BLE_COMPANION_LOCAL_NAME = "shotStopper";
constexpr const char *BLE_COMPANION_SERVICE_UUID =
    "00000000-0000-0000-0000-000000000FFE";
constexpr uint8_t BLE_COMPANION_PROTOCOL_VERSION = 2;
constexpr uint32_t BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS = 30000;
constexpr uint32_t BLE_COMPANION_REQUEST_TIMEOUT_MS = 10000;

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
  bool configuredEnabled = true;
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
  bool configuredEnabled = true;
  bool restartRequired = false;
  bool stackReady = false;
  bool advertising = false;
  bool connected = false;
  uint8_t protocolVersion = BLE_COMPANION_PROTOCOL_VERSION;
  bool apActive = false;
  uint32_t acceptedWrites = 0;
  uint32_t rejectedWrites = 0;
  BleCompanionRejectReason lastReject = BleCompanionRejectReason::NONE;
};

inline bool bleCompanionSecondsToMs(uint8_t seconds, uint32_t &outMs) {
  outMs = static_cast<uint32_t>(seconds) * 1000U;
  return true;
}

inline uint8_t bleCompanionMsToSeconds(uint32_t milliseconds) {
  const uint32_t seconds = milliseconds / 1000U;
  return static_cast<uint8_t>(seconds > 255U ? 255U : seconds);
}

#if !defined(SHOT_STOPPER_HOST_TEST)

inline bool readBleStringValue(BLEStringCharacteristic &characteristic,
                               char *output, size_t capacity,
                               size_t *outLength = nullptr) {
  const int length = characteristic.valueLength();
  if (length < 0 || static_cast<size_t>(length) >= capacity) {
    return false;
  }
  if (length == 0) {
    output[0] = '\0';
    if (outLength != nullptr) {
      *outLength = 0;
    }
    return true;
  }
  const int read =
      characteristic.readValue(reinterpret_cast<byte *>(output), length);
  if (read != length) {
    return false;
  }
  output[length] = '\0';
  if (outLength != nullptr) {
    *outLength = static_cast<size_t>(length);
  }
  return true;
}

inline void writeBleStringValue(BLEStringCharacteristic &characteristic,
                                const char *value) {
  characteristic.writeValue(value == nullptr ? String("") : String(value));
}

class ShotStopperBleCompanion {
 public:
  using EnqueueRequest = bool (*)(const BleCompanionRequest &request);

  ShotStopperBleCompanion()
      : service_(BLE_COMPANION_SERVICE_UUID),
        enabled_("00000000-0000-0000-0000-00000000FF10", BLERead | BLEWrite),
        weightValue_("00000000-0000-0000-0000-00000000FF11", BLERead | BLEWrite),
        reedSwitch_("00000000-0000-0000-0000-00000000FF12", BLERead | BLEWrite),
        momentary_("00000000-0000-0000-0000-00000000FF13", BLERead | BLEWrite),
        autoTare_("00000000-0000-0000-0000-00000000FF14", BLERead | BLEWrite),
        minShotDuration_("00000000-0000-0000-0000-00000000FF15", BLERead | BLEWrite),
        maxShotDuration_("00000000-0000-0000-0000-00000000FF16", BLERead | BLEWrite),
        dripDelay_("00000000-0000-0000-0000-00000000FF17", BLERead | BLEWrite),
        firmwareVersion_("00000000-0000-0000-0000-00000000FF18", BLERead),
        scaleStatus_("00000000-0000-0000-0000-00000000FF19", BLERead | BLENotify),
        shotStatus_("00000000-0000-0000-0000-00000000FF20", BLERead | BLENotify),
        otaRequested_("00000000-0000-0000-0000-00000000FF21", BLERead | BLEWrite),
        wifiSsid_("00000000-0000-0000-0000-00000000FF22", BLERead | BLEWrite,
                  WIFI_SSID_CAPACITY - 1),
        wifiPassword_("00000000-0000-0000-0000-00000000FF23", BLEWrite,
                      WIFI_PASSWORD_CAPACITY - 1),
        wifiIp_("00000000-0000-0000-0000-00000000FF24", BLERead | BLENotify, 15),
        reboot_("00000000-0000-0000-0000-00000000FF25", BLEWrite),
        enableAp_("00000000-0000-0000-0000-00000000FF26",
                  BLERead | BLEWrite | BLENotify) {}

  bool begin(EnqueueRequest enqueueRequest) {
    enqueueRequest_ = enqueueRequest;
    status_.enabled = true;
    BLE.setLocalName(BLE_COMPANION_LOCAL_NAME);
    BLE.setDeviceName(BLE_COMPANION_LOCAL_NAME);
    BLE.setAdvertisedService(service_);
    service_.addCharacteristic(enabled_);
    service_.addCharacteristic(weightValue_);
    service_.addCharacteristic(reedSwitch_);
    service_.addCharacteristic(momentary_);
    service_.addCharacteristic(autoTare_);
    service_.addCharacteristic(minShotDuration_);
    service_.addCharacteristic(maxShotDuration_);
    service_.addCharacteristic(dripDelay_);
    service_.addCharacteristic(firmwareVersion_);
    service_.addCharacteristic(scaleStatus_);
    service_.addCharacteristic(shotStatus_);
    service_.addCharacteristic(otaRequested_);
    service_.addCharacteristic(wifiSsid_);
    service_.addCharacteristic(wifiPassword_);
    service_.addCharacteristic(wifiIp_);
    service_.addCharacteristic(reboot_);
    service_.addCharacteristic(enableAp_);
    BLE.addService(service_);

    reedSwitch_.writeValue(static_cast<byte>(0));
    momentary_.writeValue(static_cast<byte>(0));
    otaRequested_.writeValue(static_cast<byte>(0));
    firmwareVersion_.writeValue(BLE_COMPANION_PROTOCOL_VERSION);
    status_.stackReady = true;
    status_.advertising = BLE.advertise() != 0;
    return status_.advertising;
  }

  void service(const BleCompanionRuntimeSnapshot &snapshot, uint32_t nowMs) {
    if (!status_.stackReady) {
      return;
    }
    status_.apActive = snapshot.apActive;

    BLEDevice central = BLE.central();
    const bool connectedNow = static_cast<bool>(central) && central.connected();
    if (!connectedNow && status_.connected) {
      clearWifiStage();
    }
    status_.connected = connectedNow;
    if (wifiStageValid_ &&
        static_cast<uint32_t>(nowMs - wifiStageAtMs_) >=
            BLE_COMPANION_WIFI_STAGE_TIMEOUT_MS) {
      clearWifiStage();
    }

    expirePending(nowMs);
    processWrites(nowMs);
    sync(snapshot);
  }

  void noteResult(const BleCompanionResult &result) {
    clearPending(result.sequence);
    if (result.accepted) {
      ++status_.acceptedWrites;
      return;
    }
    ++status_.rejectedWrites;
    status_.lastReject = result.reason;
  }

  const BleCompanionStatusSnapshot &status() const { return status_; }

 private:
  bool enqueue(BleCompanionRequest &request) {
    const size_t pendingIndex = static_cast<size_t>(request.type);
    if (pendingIndex >= BLE_COMPANION_REQUEST_TYPE_COUNT ||
        pendingSequence_[pendingIndex] != 0) {
      rejectLocal(BleCompanionRejectReason::REQUEST_PENDING);
      return false;
    }
    request.sequence = nextSequence_++;
    if (nextSequence_ == 0) {
      nextSequence_ = 1;
    }
    if (enqueueRequest_ != nullptr && enqueueRequest_(request)) {
      pendingSequence_[pendingIndex] = request.sequence;
      pendingAtMs_[pendingIndex] = millis();
      return true;
    }
    BleCompanionResult result;
    result.sequence = request.sequence;
    result.reason = BleCompanionRejectReason::QUEUE_FULL;
    noteResult(result);
    return false;
  }

  void enqueueByte(BleCompanionRequestType type, uint8_t value) {
    BleCompanionRequest request;
    request.type = type;
    request.value = value;
    enqueue(request);
  }

  void processWrites(uint32_t nowMs) {
    bool anyWrite = false;
    if (enabled_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_BREW_BY_WEIGHT,
                  enabled_.value());
    }
    if (weightValue_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_GOAL_WEIGHT,
                  weightValue_.value());
    }
    if (reedSwitch_.written()) {
      anyWrite = true;
      reedSwitch_.writeValue(static_cast<byte>(0));
    }
    if (momentary_.written()) {
      anyWrite = true;
      momentary_.writeValue(static_cast<byte>(0));
    }
    if (autoTare_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_AUTO_TARE, autoTare_.value());
    }
    if (minShotDuration_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS,
                  minShotDuration_.value());
    }
    if (maxShotDuration_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_OPERATIONAL_WALL_SECONDS,
                  maxShotDuration_.value());
    }
    if (dripDelay_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_DRIP_DELAY_SECONDS,
                  dripDelay_.value());
    }
    if (otaRequested_.written()) {
      anyWrite = true;
      otaRequested_.writeValue(static_cast<byte>(0));
    }
    if (wifiSsid_.written()) {
      anyWrite = true;
      size_t ssidLength = 0;
      memset(wifiStageSsid_, 0, sizeof(wifiStageSsid_));
      if (!readBleStringValue(wifiSsid_, wifiStageSsid_, sizeof(wifiStageSsid_),
                              &ssidLength) ||
          ssidLength == 0) {
        rejectLocal(BleCompanionRejectReason::INVALID_VALUE);
        clearWifiStage();
      } else {
        wifiStageValid_ = true;
        wifiStageAtMs_ = nowMs;
      }
    }
    if (wifiPassword_.written()) {
      anyWrite = true;
      if (!wifiStageValid_) {
        rejectLocal(BleCompanionRejectReason::WIFI_STAGE_MISSING);
      } else {
        char password[WIFI_PASSWORD_CAPACITY] = {};
        size_t passwordLength = 0;
        if (!readBleStringValue(wifiPassword_, password, sizeof(password),
                                &passwordLength) ||
            passwordLength >= WIFI_PASSWORD_CAPACITY) {
          rejectLocal(BleCompanionRejectReason::INVALID_VALUE);
        } else {
          BleCompanionRequest request;
          request.type = BleCompanionRequestType::SAVE_WIFI;
          strncpy(request.ssid, wifiStageSsid_, sizeof(request.ssid) - 1);
          request.openNetwork = passwordLength == 0;
          if (!request.openNetwork) {
            memcpy(request.password, password, passwordLength);
          }
          enqueue(request);
        }
        clearWifiStage();
      }
      writeBleStringValue(wifiPassword_, "");
    }
    if (reboot_.written()) {
      anyWrite = true;
      if (reboot_.value() == 1) {
        enqueueByte(BleCompanionRequestType::REBOOT, 1);
      } else {
        rejectLocal(BleCompanionRejectReason::INVALID_PAYLOAD);
      }
    }
    if (enableAp_.written()) {
      anyWrite = true;
      enqueueByte(BleCompanionRequestType::SET_AP_ENABLED,
                  enableAp_.value());
    }
    forceSync_ = forceSync_ || anyWrite;
  }

  void sync(const BleCompanionRuntimeSnapshot &snapshot) {
    if (forceSync_ || snapshot.brewByWeight != synced_.brewByWeight) {
      enabled_.writeValue(static_cast<byte>(snapshot.brewByWeight ? 1 : 0));
    }
    if (forceSync_ || snapshot.goalWeightG != synced_.goalWeightG) {
      weightValue_.writeValue(snapshot.goalWeightG);
    }
    if (forceSync_) {
      reedSwitch_.writeValue(static_cast<byte>(0));
      momentary_.writeValue(static_cast<byte>(0));
      firmwareVersion_.writeValue(BLE_COMPANION_PROTOCOL_VERSION);
      otaRequested_.writeValue(static_cast<byte>(0));
    }
    if (forceSync_ || snapshot.autoTare != synced_.autoTare) {
      autoTare_.writeValue(static_cast<byte>(snapshot.autoTare ? 1 : 0));
    }
    if (forceSync_ || snapshot.bbwProtectionMs != synced_.bbwProtectionMs) {
      minShotDuration_.writeValue(
          bleCompanionMsToSeconds(snapshot.bbwProtectionMs));
    }
    if (forceSync_ || snapshot.operationalWallMs != synced_.operationalWallMs) {
      maxShotDuration_.writeValue(
          bleCompanionMsToSeconds(snapshot.operationalWallMs));
    }
    if (forceSync_ || snapshot.dripDelayMs != synced_.dripDelayMs) {
      dripDelay_.writeValue(bleCompanionMsToSeconds(snapshot.dripDelayMs));
    }
    if (forceSync_ || snapshot.scaleConnected != synced_.scaleConnected) {
      scaleStatus_.writeValue(
          static_cast<byte>(snapshot.scaleConnected ? 1 : 0));
    }
    if (forceSync_ || snapshot.shotActive != synced_.shotActive) {
      shotStatus_.writeValue(static_cast<byte>(snapshot.shotActive ? 1 : 0));
    }
    if (forceSync_ || strcmp(snapshot.wifiSsid, synced_.wifiSsid) != 0) {
      writeBleStringValue(wifiSsid_, snapshot.wifiSsid);
    }
    if (forceSync_ || strcmp(snapshot.wifiIp, synced_.wifiIp) != 0) {
      writeBleStringValue(wifiIp_, snapshot.wifiIp);
    }
    if (forceSync_ || snapshot.apActive != synced_.apActive) {
      enableAp_.writeValue(static_cast<byte>(snapshot.apActive ? 1 : 0));
    }
    synced_ = snapshot;
    forceSync_ = false;
  }

  void rejectLocal(BleCompanionRejectReason reason) {
    BleCompanionResult result;
    result.reason = reason;
    noteResult(result);
  }

  void clearPending(uint32_t sequence) {
    if (sequence == 0) return;
    for (size_t index = 0; index < BLE_COMPANION_REQUEST_TYPE_COUNT; ++index) {
      if (pendingSequence_[index] == sequence) {
        pendingSequence_[index] = 0;
        pendingAtMs_[index] = 0;
        return;
      }
    }
  }

  void expirePending(uint32_t nowMs) {
    for (size_t index = 0; index < BLE_COMPANION_REQUEST_TYPE_COUNT; ++index) {
      if (pendingSequence_[index] != 0 &&
          static_cast<uint32_t>(nowMs - pendingAtMs_[index]) >=
              BLE_COMPANION_REQUEST_TIMEOUT_MS) {
        pendingSequence_[index] = 0;
        pendingAtMs_[index] = 0;
        rejectLocal(BleCompanionRejectReason::COMMAND_REJECTED);
      }
    }
  }

  void clearWifiStage() {
    memset(wifiStageSsid_, 0, sizeof(wifiStageSsid_));
    wifiStageValid_ = false;
    wifiStageAtMs_ = 0;
  }

  BLEService service_;
  BLEByteCharacteristic enabled_;
  BLEByteCharacteristic weightValue_;
  BLEByteCharacteristic reedSwitch_;
  BLEByteCharacteristic momentary_;
  BLEByteCharacteristic autoTare_;
  BLEByteCharacteristic minShotDuration_;
  BLEByteCharacteristic maxShotDuration_;
  BLEByteCharacteristic dripDelay_;
  BLEByteCharacteristic firmwareVersion_;
  BLEByteCharacteristic scaleStatus_;
  BLEByteCharacteristic shotStatus_;
  BLEByteCharacteristic otaRequested_;
  BLEStringCharacteristic wifiSsid_;
  BLEStringCharacteristic wifiPassword_;
  BLEStringCharacteristic wifiIp_;
  BLEByteCharacteristic reboot_;
  BLEByteCharacteristic enableAp_;
  EnqueueRequest enqueueRequest_ = nullptr;
  BleCompanionStatusSnapshot status_ = {};
  uint32_t nextSequence_ = 1;
  char wifiStageSsid_[WIFI_SSID_CAPACITY] = {};
  bool wifiStageValid_ = false;
  uint32_t wifiStageAtMs_ = 0;
  BleCompanionRuntimeSnapshot synced_ = {};
  bool forceSync_ = true;
  static constexpr size_t BLE_COMPANION_REQUEST_TYPE_COUNT = 9;
  uint32_t pendingSequence_[BLE_COMPANION_REQUEST_TYPE_COUNT] = {};
  uint32_t pendingAtMs_[BLE_COMPANION_REQUEST_TYPE_COUNT] = {};
};

#endif

}  // namespace shotstopper
