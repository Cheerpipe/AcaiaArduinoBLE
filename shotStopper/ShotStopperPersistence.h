#pragma once

#include "ShotStopperDomain.h"

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include "tests/persistence_host_stubs.h"
#else
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#endif

namespace shotstopper {

constexpr uint32_t PERSISTED_SETTINGS_MAGIC = 0x53544F50U;  // "STOP"
constexpr const char *SETTINGS_NAMESPACE = "shotstopper";
constexpr const char *SETTINGS_SLOT_A = "settingsA";
constexpr const char *SETTINGS_SLOT_B = "settingsB";
constexpr size_t AUTH_SALT_LENGTH = 16;
constexpr size_t AUTH_HASH_LENGTH = 32;
constexpr const char *DEFAULT_AP_PASSWORD = "Micra1234";

struct PersistedSettings {
  uint32_t magic = PERSISTED_SETTINGS_MAGIC;
  uint32_t schemaVersion = CONFIG_SCHEMA_VERSION;
  uint32_t structureSize = 0;
  uint32_t storageRevision = 0;
  RuntimeConfig runtime = {};
  bool staConfigured = false;
  bool staOpen = false;
  char staSsid[WIFI_SSID_CAPACITY] = {};
  char staPassword[WIFI_PASSWORD_CAPACITY] = {};
  char apPassword[WIFI_PASSWORD_CAPACITY] = {};
  uint8_t authSalt[AUTH_SALT_LENGTH] = {};
  uint8_t authHash[AUTH_HASH_LENGTH] = {};
  uint32_t checksum = 0;
};

struct RuntimeConfigV12 {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  bool timerOnly = false;
  bool canTareStartTimer = true;
  bool brewConfirmationBeep = true;
  bool paddleReturnReminderBeep = true;
  uint32_t paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  uint32_t paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  uint32_t retareStabilityMinDurationMs = DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  uint32_t confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct PersistedSettingsV12 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV12 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

inline bool calculatePasswordHash(const uint8_t salt[AUTH_SALT_LENGTH],
                                  const char *password,
                                  uint8_t output[AUTH_HASH_LENGTH]) {
  size_t passwordLength = 0;
  if (!boundedCString(password, WIFI_PASSWORD_CAPACITY, &passwordLength)) {
    return false;
  }

  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool success = mbedtls_sha256_starts(&context, 0) == 0 &&
                 mbedtls_sha256_update(&context, salt, AUTH_SALT_LENGTH) == 0 &&
                 mbedtls_sha256_update(
                     &context, reinterpret_cast<const uint8_t *>(password),
                     passwordLength) == 0 &&
                 mbedtls_sha256_finish(&context, output) == 0;
  mbedtls_sha256_free(&context);
  return success;
}

inline bool constantTimeEqual(const uint8_t *left, const uint8_t *right,
                              size_t length) {
  uint8_t difference = 0;
  for (size_t index = 0; index < length; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

inline bool refreshAuthentication(PersistedSettings &settings,
                                  const char *newPassword) {
  if (!validAccessPointPassword(newPassword)) {
    return false;
  }
  memset(settings.apPassword, 0, sizeof(settings.apPassword));
  strncpy(settings.apPassword, newPassword, sizeof(settings.apPassword) - 1);
  esp_fill_random(settings.authSalt, sizeof(settings.authSalt));
  return calculatePasswordHash(settings.authSalt, settings.apPassword,
                               settings.authHash);
}

inline bool initializeDefaultAuthentication(PersistedSettings &settings) {
  return refreshAuthentication(settings, DEFAULT_AP_PASSWORD);
}

inline bool verifyAdminPassword(const PersistedSettings &settings,
                                const char *candidate) {
  uint8_t candidateHash[AUTH_HASH_LENGTH] = {};
  return calculatePasswordHash(settings.authSalt, candidate, candidateHash) &&
         constantTimeEqual(settings.authHash, candidateHash,
                           sizeof(candidateHash));
}

inline uint32_t persistedSettingsChecksum(const PersistedSettings &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettings, checksum));
}

inline uint32_t persistedSettingsV12Checksum(
    const PersistedSettingsV12 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV12, checksum));
}

inline void normalizeRuntimeConfirmationDefaults(RuntimeConfig &runtime) {
  if (runtime.confirmationTimeoutMs < DEFAULT_CONFIRMATION_TIMEOUT_MS) {
    runtime.confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  }
  const uint32_t minimum = minimumConfirmationTimeoutMs(runtime);
  if (runtime.confirmationTimeoutMs < minimum) {
    runtime.confirmationTimeoutMs = minimum;
  }
}

inline void migrateRuntimeConfigV12ToV13(const RuntimeConfigV12 &legacy,
                                         RuntimeConfig &runtime) {
  runtime.revision = legacy.revision;
  runtime.goalWeightG = legacy.goalWeightG;
  runtime.weightOffsetG = legacy.weightOffsetG;
  runtime.autoTare = legacy.autoTare;
  runtime.timerOnly = legacy.timerOnly;
  runtime.canTareStartTimer = legacy.canTareStartTimer;
  runtime.brewConfirmationBeep = legacy.brewConfirmationBeep;
  runtime.paddleReturnReminderBeep = legacy.paddleReturnReminderBeep;
  runtime.paddleReturnReminderIntervalMs =
      legacy.paddleReturnReminderIntervalMs;
  runtime.paddleReturnReminderMaxDurationMs =
      legacy.paddleReturnReminderMaxDurationMs;
  runtime.rinseGestureMs = legacy.rinseGestureMs;
  runtime.rinseDurationMs = legacy.rinseDurationMs;
  runtime.autoRetare = legacy.autoRetare;
  runtime.retareWindowMs = legacy.retareWindowMs;
  runtime.minimumCupWeightG = legacy.minimumCupWeightG;
  runtime.retareStabilitySamples = legacy.retareStabilitySamples;
  runtime.retareStabilityToleranceG = legacy.retareStabilityToleranceG;
  runtime.retareStabilityMaxGapMs = legacy.retareStabilityMaxGapMs;
  runtime.retareStabilityMinDurationMs = legacy.retareStabilityMinDurationMs;
  runtime.confirmationTimeoutMs = legacy.confirmationTimeoutMs;
  runtime.operationalWallMs = legacy.operationalWallMs;
  runtime.timezoneOffsetMinutes = legacy.timezoneOffsetMinutes;
  runtime.ntpServerPreset = legacy.ntpServerPreset;
  memcpy(runtime.ntpServerCustom, legacy.ntpServerCustom,
         sizeof(runtime.ntpServerCustom));
  runtime.fastExtractionGuardEnabled = false;
  runtime.maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  runtime.minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
  normalizeRuntimeConfirmationDefaults(runtime);
}

inline bool validPersistedSettings(const PersistedSettings &settings) {
  if (settings.magic != PERSISTED_SETTINGS_MAGIC ||
      settings.schemaVersion != CONFIG_SCHEMA_VERSION ||
      settings.structureSize != sizeof(PersistedSettings) ||
      settings.checksum != persistedSettingsChecksum(settings) ||
      validateRuntimeConfig(settings.runtime) != ConfigValidationError::NONE ||
      !validAccessPointPassword(settings.apPassword)) {
    return false;
  }
  if (settings.staConfigured &&
      (!validWifiSsid(settings.staSsid) ||
       !validWifiPassword(settings.staPassword, settings.staOpen))) {
    return false;
  }
  uint8_t expectedHash[AUTH_HASH_LENGTH] = {};
  return calculatePasswordHash(settings.authSalt, settings.apPassword,
                               expectedHash) &&
         constantTimeEqual(settings.authHash, expectedHash,
                           sizeof(expectedHash));
}

inline void finalizePersistedSettings(PersistedSettings &settings) {
  settings.magic = PERSISTED_SETTINGS_MAGIC;
  settings.schemaVersion = CONFIG_SCHEMA_VERSION;
  settings.structureSize = sizeof(PersistedSettings);
  settings.checksum = 0;
  settings.checksum = persistedSettingsChecksum(settings);
}

inline bool readSettingsSlot(Preferences &preferences, const char *key,
                             PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettings)) {
    return false;
  }
  PersistedSettings candidate = {};
  if (preferences.getBytes(key, &candidate, sizeof(candidate)) !=
      sizeof(candidate)) {
    return false;
  }
  if (!validPersistedSettings(candidate)) {
    return false;
  }
  settings = candidate;
  return true;
}

inline bool readV12SettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV12)) {
    return false;
  }
  PersistedSettingsV12 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != PREVIOUS_CONFIG_SCHEMA_VERSION ||
      legacy.structureSize != sizeof(PersistedSettingsV12) ||
      legacy.checksum != persistedSettingsV12Checksum(legacy) ||
      !validAccessPointPassword(legacy.apPassword) ||
      (legacy.staConfigured != 0 &&
       (!validWifiSsid(legacy.staSsid) ||
        !validWifiPassword(legacy.staPassword, legacy.staOpen != 0)))) {
    return false;
  }

  uint8_t expectedHash[AUTH_HASH_LENGTH] = {};
  if (!calculatePasswordHash(legacy.authSalt, legacy.apPassword,
                             expectedHash) ||
      !constantTimeEqual(legacy.authHash, expectedHash,
                         sizeof(expectedHash))) {
    return false;
  }

  PersistedSettings migrated = {};
  migrated.storageRevision = legacy.storageRevision;
  migrateRuntimeConfigV12ToV13(legacy.runtime, migrated.runtime);
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readAnySettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings,
                                bool *legacyFormat = nullptr) {
  const size_t length = preferences.getBytesLength(key);
  const bool isLegacy = length == sizeof(PersistedSettingsV12);
  const bool valid =
      length == sizeof(PersistedSettings)
          ? readSettingsSlot(preferences, key, settings)
          : length == sizeof(PersistedSettingsV12)
                ? readV12SettingsSlot(preferences, key, settings)
                : false;
  if (legacyFormat != nullptr) {
    *legacyFormat = valid && isLegacy;
  }
  return valid;
}

inline bool loadPersistedSettings(PersistedSettings &settings,
                                  bool *legacyMigrated = nullptr) {
  if (legacyMigrated != nullptr) {
    *legacyMigrated = false;
  }
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    return false;
  }
  PersistedSettings first = {};
  PersistedSettings second = {};
  bool firstLegacy = false;
  bool secondLegacy = false;
  const bool firstValid = readAnySettingsSlot(
      preferences, SETTINGS_SLOT_A, first, &firstLegacy);
  const bool secondValid = readAnySettingsSlot(
      preferences, SETTINGS_SLOT_B, second, &secondLegacy);
  preferences.end();

  if (!firstValid && !secondValid) {
    return false;
  }
  if (!firstValid) {
    settings = second;
    if (legacyMigrated != nullptr) {
      *legacyMigrated = secondLegacy;
    }
  } else if (!secondValid) {
    settings = first;
    if (legacyMigrated != nullptr) {
      *legacyMigrated = firstLegacy;
    }
  } else {
    const int32_t revisionDelta =
        static_cast<int32_t>(second.storageRevision - first.storageRevision);
    if (revisionDelta > 0 ||
        (revisionDelta == 0 && secondLegacy && !firstLegacy)) {
      settings = second;
      if (legacyMigrated != nullptr) {
        *legacyMigrated = secondLegacy;
      }
    } else {
      settings = first;
      if (legacyMigrated != nullptr) {
        *legacyMigrated = firstLegacy;
      }
    }
  }
  return true;
}

inline bool savePersistedSettings(PersistedSettings &settings) {
  PersistedSettings candidate = settings;
  PersistedSettings current = {};
  if (loadPersistedSettings(current)) {
    candidate.storageRevision = current.storageRevision;
  }
  ++candidate.storageRevision;
  if (candidate.storageRevision == 0) {
    candidate.storageRevision = 1;
  }
  finalizePersistedSettings(candidate);

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    return false;
  }
  const char *target =
      (candidate.storageRevision & 1U) ? SETTINGS_SLOT_A : SETTINGS_SLOT_B;
  const bool written =
      preferences.putBytes(target, &candidate, sizeof(candidate)) ==
      sizeof(candidate);
  PersistedSettings verified = {};
  const bool saved = written &&
                     readSettingsSlot(preferences, target, verified) &&
                     verified.storageRevision == candidate.storageRevision &&
                     memcmp(&verified, &candidate, sizeof(candidate)) == 0;
  preferences.end();
  if (saved) {
    settings = candidate;
  }
  return saved;
}

inline bool initializeDefaultSettings(PersistedSettings &settings) {
  settings = PersistedSettings{};
  if (!initializeDefaultAuthentication(settings)) {
    return false;
  }
  finalizePersistedSettings(settings);
  return true;
}

inline bool resetPersistedSettingsToFactory(PersistedSettings &settings) {
  PersistedSettings first = {};
  if (!initializeDefaultSettings(first)) {
    return false;
  }
  first.storageRevision = 1;
  finalizePersistedSettings(first);
  PersistedSettings second = first;
  second.storageRevision = 2;
  finalizePersistedSettings(second);

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    return false;
  }
  if (!preferences.clear()) {
    preferences.end();
    return false;
  }
  const bool firstSaved =
      preferences.putBytes(SETTINGS_SLOT_A, &first, sizeof(first)) ==
      sizeof(first);
  const bool secondSaved =
      preferences.putBytes(SETTINGS_SLOT_B, &second, sizeof(second)) ==
      sizeof(second);
  PersistedSettings verifiedFirst = {};
  PersistedSettings verifiedSecond = {};
  const bool firstVerified =
      firstSaved && readSettingsSlot(preferences, SETTINGS_SLOT_A,
                                     verifiedFirst);
  const bool secondVerified =
      secondSaved && readSettingsSlot(preferences, SETTINGS_SLOT_B,
                                      verifiedSecond);
  preferences.end();

  if (!firstVerified && !secondVerified) {
    return false;
  }
  settings = secondVerified ? verifiedSecond : verifiedFirst;
  return true;
}

}  // namespace shotstopper
