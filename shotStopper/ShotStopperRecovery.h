#pragma once

#include "ShotStopperPersistence.h"

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <nvs.h>
#endif

namespace shotstopper {

constexpr uint32_t RECOVERY_INTENT_MAGIC = 0x52454356U;  // "RECV"
constexpr uint16_t RECOVERY_INTENT_VERSION = 1;
constexpr const char *RECOVERY_NAMESPACE = "recovery";
constexpr const char *RECOVERY_INTENT_KEY = "pending";

enum class RecoveryOperation : uint8_t {
  NONE = 0,
  NETWORK_ACCESS_RESET = 1,
  FACTORY_RESET = 2,
};

struct RecoveryIntent {
  uint32_t magic = RECOVERY_INTENT_MAGIC;
  uint16_t version = RECOVERY_INTENT_VERSION;
  uint16_t structureSize = sizeof(RecoveryIntent);
  uint8_t operation = static_cast<uint8_t>(RecoveryOperation::NONE);
  uint8_t reserved[3] = {};
  uint32_t checksum = 0;
};

inline uint32_t recoveryIntentChecksum(const RecoveryIntent &intent) {
  return crc32(reinterpret_cast<const uint8_t *>(&intent),
               offsetof(RecoveryIntent, checksum));
}

inline void finalizeRecoveryIntent(RecoveryIntent &intent) {
  intent.magic = RECOVERY_INTENT_MAGIC;
  intent.version = RECOVERY_INTENT_VERSION;
  intent.structureSize = sizeof(RecoveryIntent);
  intent.checksum = 0;
  intent.checksum = recoveryIntentChecksum(intent);
}

inline bool validRecoveryOperation(uint8_t operation) {
  return operation ==
             static_cast<uint8_t>(RecoveryOperation::NETWORK_ACCESS_RESET) ||
         operation == static_cast<uint8_t>(RecoveryOperation::FACTORY_RESET);
}

inline bool validRecoveryIntent(const RecoveryIntent &intent) {
  return intent.magic == RECOVERY_INTENT_MAGIC &&
         intent.version == RECOVERY_INTENT_VERSION &&
         intent.structureSize == sizeof(RecoveryIntent) &&
         validRecoveryOperation(intent.operation) &&
         intent.checksum == recoveryIntentChecksum(intent);
}

inline bool loadRecoveryIntent(RecoveryIntent &intent) {
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, true)) {
    return false;
  }
  const bool loaded =
      preferences.getBytesLength(RECOVERY_INTENT_KEY) == sizeof(intent) &&
      preferences.getBytes(RECOVERY_INTENT_KEY, &intent, sizeof(intent)) ==
          sizeof(intent) &&
      validRecoveryIntent(intent);
  preferences.end();
  return loaded;
#else
  // Read-only Preferences.begin() ESP_LOGEs when the namespace is absent.
  nvs_handle_t handle = 0;
  if (nvs_open(RECOVERY_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  size_t length = sizeof(intent);
  const esp_err_t err =
      nvs_get_blob(handle, RECOVERY_INTENT_KEY, &intent, &length);
  nvs_close(handle);
  return err == ESP_OK && length == sizeof(intent) &&
         validRecoveryIntent(intent);
#endif
}

inline bool recoveryIntentRecordPresent() {
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, true)) {
    return false;
  }
  const bool present = preferences.getBytesLength(RECOVERY_INTENT_KEY) != 0;
  preferences.end();
  return present;
#else
  nvs_handle_t handle = 0;
  if (nvs_open(RECOVERY_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  size_t length = 0;
  const esp_err_t err =
      nvs_get_blob(handle, RECOVERY_INTENT_KEY, nullptr, &length);
  nvs_close(handle);
  return err == ESP_ERR_NVS_INVALID_LENGTH || (err == ESP_OK && length != 0);
#endif
}

inline bool saveRecoveryIntent(RecoveryOperation operation) {
  if (operation == RecoveryOperation::NONE) {
    return false;
  }
  RecoveryIntent intent;
  intent.operation = static_cast<uint8_t>(operation);
  finalizeRecoveryIntent(intent);

  if (!lockSettingsNvs()) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  const bool written =
      preferences.putBytes(RECOVERY_INTENT_KEY, &intent, sizeof(intent)) ==
      sizeof(intent);
  RecoveryIntent verified;
  const bool saved =
      written &&
      preferences.getBytesLength(RECOVERY_INTENT_KEY) == sizeof(verified) &&
      preferences.getBytes(RECOVERY_INTENT_KEY, &verified,
                           sizeof(verified)) == sizeof(verified) &&
      validRecoveryIntent(verified) &&
      verified.operation == intent.operation;
  preferences.end();
  unlockSettingsNvs();
  return saved;
}

inline bool clearRecoveryIntent() {
  if (!lockSettingsNvs()) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  const bool absent = preferences.getBytesLength(RECOVERY_INTENT_KEY) == 0;
  const bool removed = absent || preferences.remove(RECOVERY_INTENT_KEY);
  const bool verified =
      removed && preferences.getBytesLength(RECOVERY_INTENT_KEY) == 0;
  preferences.end();
  unlockSettingsNvs();
  return verified;
}

inline bool resetPersistedNetworkAccess(PersistedSettings &settings) {
  PersistedSettings candidate;
  if (!loadPersistedSettings(candidate) &&
      !initializeDefaultSettings(candidate)) {
    return false;
  }
  clearStaNetwork(candidate);
  if (!initializeDefaultAccessPointPassword(candidate) ||
      !savePersistedSettings(candidate)) {
    return false;
  }
  PersistedSettings verified;
  if (!loadPersistedSettings(verified) || verified.staConfigured ||
      verified.lkgValid ||
      verified.staIpMode != static_cast<uint8_t>(StaIpMode::DHCP) ||
      !passwordIsFactoryDefault(verified)) {
    return false;
  }
  settings = verified;
  return true;
}

}  // namespace shotstopper
