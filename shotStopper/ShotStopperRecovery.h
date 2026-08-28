#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperPreferences.h"

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
  if (!lockFlashIo()) {
    return false;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, true)) {
    unlockFlashIo();
    return false;
  }
  const bool loaded =
      preferences.getBytesLength(RECOVERY_INTENT_KEY) == sizeof(intent) &&
      preferences.getBytes(RECOVERY_INTENT_KEY, &intent, sizeof(intent)) ==
          sizeof(intent) &&
      validRecoveryIntent(intent);
  preferences.end();
  unlockFlashIo();
  return loaded;
#else
  // Read-only Preferences.begin() ESP_LOGEs when the namespace is absent.
  nvs_handle_t handle = 0;
  if (nvs_open(RECOVERY_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    unlockFlashIo();
    return false;
  }
  size_t length = sizeof(intent);
  const esp_err_t err =
      nvs_get_blob(handle, RECOVERY_INTENT_KEY, &intent, &length);
  nvs_close(handle);
  unlockFlashIo();
  return err == ESP_OK && length == sizeof(intent) &&
         validRecoveryIntent(intent);
#endif
}

inline bool recoveryIntentRecordPresent() {
  if (!lockFlashIo()) {
    return false;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, true)) {
    unlockFlashIo();
    return false;
  }
  const bool present = preferences.getBytesLength(RECOVERY_INTENT_KEY) != 0;
  preferences.end();
  unlockFlashIo();
  return present;
#else
  nvs_handle_t handle = 0;
  if (nvs_open(RECOVERY_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    unlockFlashIo();
    return false;
  }
  size_t length = 0;
  const esp_err_t err =
      nvs_get_blob(handle, RECOVERY_INTENT_KEY, nullptr, &length);
  nvs_close(handle);
  unlockFlashIo();
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

  if (!lockFlashIo()) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, false)) {
    unlockFlashIo();
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
  unlockFlashIo();
  return saved;
}

inline bool clearRecoveryIntent() {
  if (!lockFlashIo()) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(RECOVERY_NAMESPACE, false)) {
    unlockFlashIo();
    return false;
  }
  const bool absent = preferences.getBytesLength(RECOVERY_INTENT_KEY) == 0;
  const bool removed = absent || preferences.remove(RECOVERY_INTENT_KEY);
  const bool verified =
      removed && preferences.getBytesLength(RECOVERY_INTENT_KEY) == 0;
  preferences.end();
  unlockFlashIo();
  return verified;
}

enum class PendingRecoveryKind : uint8_t {
  NONE = 0,
  VALID = 1,
  MALFORMED = 2,
};

inline PendingRecoveryKind inspectPendingRecovery(RecoveryIntent &intent) {
  if (loadRecoveryIntent(intent)) {
    return PendingRecoveryKind::VALID;
  }
  if (recoveryIntentRecordPresent()) {
    return PendingRecoveryKind::MALFORMED;
  }
  return PendingRecoveryKind::NONE;
}

inline bool recoveryIntentMatches(RecoveryOperation operation) {
  RecoveryIntent intent;
  return loadRecoveryIntent(intent) &&
         intent.operation == static_cast<uint8_t>(operation);
}

// Persist the latch only when missing or for a different operation. Rewriting
// a valid blob on a full NVS partition can tear it and brick boot recovery.
inline bool ensureRecoveryIntent(RecoveryOperation operation) {
  if (recoveryIntentMatches(operation)) {
    return true;
  }
  return saveRecoveryIntent(operation);
}

inline bool abandonRecoveryIntent() { return clearRecoveryIntent(); }

inline bool bootRecoveryShouldRestartAfterSuccess() {
  return !recoveryIntentRecordPresent();
}

}  // namespace shotstopper
