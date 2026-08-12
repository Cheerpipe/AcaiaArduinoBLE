#pragma once

#include "ShotStopperDomain.h"

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include "tests/persistence_host_stubs.h"
#else
#include <EEPROM.h>
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
constexpr size_t LEGACY_WEIGHT_EEPROM_ADDRESS = 0;
constexpr size_t LEGACY_OFFSET_EEPROM_ADDRESS = 1;
constexpr uint8_t ERASED_EEPROM_VALUE = 0xFF;

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

// Exact on-flash layout used by schemas 2 and 3. Keep byte-sized flags here:
// schema 2 used the schema-3 brew beep byte as padding, which is not a valid
// C++ bool representation in every possible legacy record.
struct PersistedRuntimeConfigV3 {
  uint32_t revision;
  uint8_t goalWeightG;
  uint8_t alignmentBeforeWeight[3];
  float weightOffsetG;
  uint8_t autoTare;
  uint8_t timerOnly;
  uint8_t canTareStartTimer;
  uint8_t brewConfirmationBeep;
  uint32_t rinseGestureMs;
  uint32_t rinseDurationMs;
  uint32_t brewConfirmMs;
  uint32_t minAutoStopMs;
  uint32_t operationalWallMs;
};

struct PersistedSettingsV3 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  PersistedRuntimeConfigV3 runtime;
  uint8_t staConfigured;
  uint8_t staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint8_t alignmentBeforeChecksum[1];
  uint32_t checksum;
};

static_assert(sizeof(PersistedRuntimeConfigV3) == 36,
              "Unexpected schema-3 runtime layout");
static_assert(sizeof(PersistedSettingsV3) == 268,
              "Unexpected schema-3 settings layout");
static_assert(sizeof(PersistedSettingsV3) < sizeof(PersistedSettings),
              "Schema-4 migration requires a larger current record");

struct PersistedRuntimeConfigV4 {
  uint32_t revision;
  uint8_t goalWeightG;
  float weightOffsetG;
  uint8_t autoTare;
  uint8_t timerOnly;
  uint8_t canTareStartTimer;
  uint8_t brewConfirmationBeep;
  uint8_t paddleReturnReminderBeep;
  uint32_t rinseGestureMs;
  uint32_t rinseDurationMs;
  uint32_t brewConfirmMs;
  uint32_t minAutoStopMs;
  uint32_t operationalWallMs;
};

struct PersistedSettingsV4 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  PersistedRuntimeConfigV4 runtime;
  uint8_t staConfigured;
  uint8_t staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

static_assert(sizeof(PersistedSettingsV4) < sizeof(PersistedSettings),
              "Schema-5 migration requires a larger current record");

struct PersistedRuntimeConfigV5 {
  uint32_t revision;
  uint8_t goalWeightG;
  float weightOffsetG;
  uint8_t autoTare;
  uint8_t timerOnly;
  uint8_t canTareStartTimer;
  uint8_t brewConfirmationBeep;
  uint8_t paddleReturnReminderBeep;
  uint32_t paddleReturnReminderIntervalMs;
  uint32_t rinseGestureMs;
  uint32_t rinseDurationMs;
  uint32_t brewConfirmMs;
  uint32_t minAutoStopMs;
  uint32_t operationalWallMs;
};

struct PersistedSettingsV5 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  PersistedRuntimeConfigV5 runtime;
  uint8_t staConfigured;
  uint8_t staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

static_assert(sizeof(PersistedSettingsV5) < sizeof(PersistedSettings),
              "Schema-6 migration requires a larger current record");

struct PersistedRuntimeConfigV6 {
  uint32_t revision;
  uint8_t goalWeightG;
  float weightOffsetG;
  uint8_t autoTare;
  uint8_t timerOnly;
  uint8_t canTareStartTimer;
  uint8_t brewConfirmationBeep;
  uint8_t paddleReturnReminderBeep;
  uint32_t paddleReturnReminderIntervalMs;
  uint32_t paddleReturnReminderMaxDurationMs;
  uint32_t rinseGestureMs;
  uint32_t rinseDurationMs;
  uint32_t brewConfirmMs;
  uint32_t minAutoStopMs;
  uint32_t operationalWallMs;
};

struct PersistedSettingsV6 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  PersistedRuntimeConfigV6 runtime;
  uint8_t staConfigured;
  uint8_t staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

struct RuntimeConfigV7 {
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
  uint32_t brewConfirmMs = 3000;
  uint32_t minAutoStopMs = 5000;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
};

struct PersistedSettingsV7 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV7 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

struct RuntimeConfigV8 {
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
  uint32_t brewConfirmMs = 3000;
  uint32_t minAutoStopMs = 5000;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct PersistedSettingsV8 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV8 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

struct RuntimeConfigV9 {
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
  uint32_t brewConfirmMs = 3000;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  bool shotConfirmationEnabled = true;
  uint32_t confirmationTimeoutMs = 7000;
  uint32_t minAutoStopMs = 5000;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct PersistedSettingsV9 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV9 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

struct RuntimeConfigV10 {
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
  uint32_t brewConfirmMs = 3000;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  bool shotConfirmationEnabled = true;
  uint32_t confirmationTimeoutMs = 7000;
  uint32_t minAutoStopMs = 5000;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct PersistedSettingsV10 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV10 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

struct RuntimeConfigV11 {
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
  uint32_t brewConfirmMs = 3000;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  uint32_t retareStabilityMinDurationMs = DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  bool shotConfirmationEnabled = true;
  uint32_t confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  uint32_t minAutoStopMs = 5000;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct PersistedSettingsV11 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV11 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

static_assert(sizeof(PersistedSettingsV6) < sizeof(PersistedSettingsV7),
              "Schema-7 migration requires a larger v7 record");
static_assert(sizeof(PersistedSettingsV7) < sizeof(PersistedSettingsV8),
              "Schema-8 migration requires a larger v8 record");
static_assert(sizeof(PersistedSettingsV8) < sizeof(PersistedSettingsV9),
              "Schema-9 migration requires a larger v9 record");
static_assert(sizeof(PersistedSettingsV9) < sizeof(PersistedSettingsV10),
              "Schema-10 migration requires a larger v10 record");
static_assert(sizeof(PersistedSettingsV10) < sizeof(PersistedSettingsV11),
              "Schema-11 migration requires a larger v11 record");
static_assert(sizeof(PersistedSettingsV11) >= sizeof(PersistedSettings),
              "Schema-12 record must fit within the v11 on-flash size");

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

inline uint32_t persistedSettingsV3Checksum(
    const PersistedSettingsV3 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV3, checksum));
}

inline uint32_t persistedSettingsV6Checksum(
    const PersistedSettingsV6 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV6, checksum));
}

inline uint32_t persistedSettingsV7Checksum(
    const PersistedSettingsV7 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV7, checksum));
}

inline uint32_t persistedSettingsV8Checksum(
    const PersistedSettingsV8 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV8, checksum));
}

inline uint32_t persistedSettingsV9Checksum(
    const PersistedSettingsV9 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV9, checksum));
}

inline uint32_t persistedSettingsV10Checksum(
    const PersistedSettingsV10 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV10, checksum));
}

inline uint32_t persistedSettingsV11Checksum(
    const PersistedSettingsV11 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV11, checksum));
}

inline void migrateRuntimeConfigV11ToCurrent(const RuntimeConfigV11 &legacy,
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
  if (runtime.confirmationTimeoutMs < DEFAULT_CONFIRMATION_TIMEOUT_MS) {
    runtime.confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  }
  const uint32_t minimumConfirmation = minimumConfirmationTimeoutMs(runtime);
  if (runtime.confirmationTimeoutMs < minimumConfirmation) {
    runtime.confirmationTimeoutMs = minimumConfirmation;
  }
  runtime.operationalWallMs = legacy.operationalWallMs;
  runtime.timezoneOffsetMinutes = legacy.timezoneOffsetMinutes;
  runtime.ntpServerPreset = legacy.ntpServerPreset;
  memcpy(runtime.ntpServerCustom, legacy.ntpServerCustom,
         sizeof(runtime.ntpServerCustom));
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

inline bool readV7SettingsSlot(Preferences &preferences, const char *key,
                               PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV7)) {
    return false;
  }
  PersistedSettingsV7 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != 7U ||
      legacy.structureSize != sizeof(PersistedSettingsV7) ||
      legacy.checksum != persistedSettingsV7Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly;
  migrated.runtime.canTareStartTimer = legacy.runtime.canTareStartTimer;
  migrated.runtime.brewConfirmationBeep = legacy.runtime.brewConfirmationBeep;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep;
  migrated.runtime.paddleReturnReminderIntervalMs =
      legacy.runtime.paddleReturnReminderIntervalMs;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      legacy.runtime.paddleReturnReminderMaxDurationMs;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.runtime.timezoneOffsetMinutes =
      legacy.runtime.timezoneOffsetMinutes;
  migrated.runtime.ntpServerPreset =
      static_cast<uint8_t>(NtpServerPreset::POOL);
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readV8SettingsSlot(Preferences &preferences, const char *key,
                               PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV8)) {
    return false;
  }
  PersistedSettingsV8 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != 8U ||
      legacy.structureSize != sizeof(PersistedSettingsV8) ||
      legacy.checksum != persistedSettingsV8Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly;
  migrated.runtime.canTareStartTimer = legacy.runtime.canTareStartTimer;
  migrated.runtime.brewConfirmationBeep = legacy.runtime.brewConfirmationBeep;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep;
  migrated.runtime.paddleReturnReminderIntervalMs =
      legacy.runtime.paddleReturnReminderIntervalMs;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      legacy.runtime.paddleReturnReminderMaxDurationMs;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.runtime.timezoneOffsetMinutes =
      legacy.runtime.timezoneOffsetMinutes;
  migrated.runtime.ntpServerPreset = legacy.runtime.ntpServerPreset;
  memcpy(migrated.runtime.ntpServerCustom, legacy.runtime.ntpServerCustom,
         sizeof(migrated.runtime.ntpServerCustom));
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readV9SettingsSlot(Preferences &preferences, const char *key,
                               PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV9)) {
    return false;
  }
  PersistedSettingsV9 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != 9U ||
      legacy.structureSize != sizeof(PersistedSettingsV9) ||
      legacy.checksum != persistedSettingsV9Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly;
  migrated.runtime.canTareStartTimer = legacy.runtime.canTareStartTimer;
  migrated.runtime.brewConfirmationBeep = legacy.runtime.brewConfirmationBeep;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep;
  migrated.runtime.paddleReturnReminderIntervalMs =
      legacy.runtime.paddleReturnReminderIntervalMs;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      legacy.runtime.paddleReturnReminderMaxDurationMs;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.autoRetare = legacy.runtime.autoRetare;
  migrated.runtime.retareWindowMs = legacy.runtime.retareWindowMs;
  migrated.runtime.minimumCupWeightG = legacy.runtime.minimumCupWeightG;
  migrated.runtime.confirmationTimeoutMs =
      legacy.runtime.confirmationTimeoutMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.runtime.timezoneOffsetMinutes =
      legacy.runtime.timezoneOffsetMinutes;
  migrated.runtime.ntpServerPreset = legacy.runtime.ntpServerPreset;
  memcpy(migrated.runtime.ntpServerCustom, legacy.runtime.ntpServerCustom,
         sizeof(migrated.runtime.ntpServerCustom));
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readV10SettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV10)) {
    return false;
  }
  PersistedSettingsV10 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != 10U ||
      legacy.structureSize != sizeof(PersistedSettingsV10) ||
      legacy.checksum != persistedSettingsV10Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly;
  migrated.runtime.canTareStartTimer = legacy.runtime.canTareStartTimer;
  migrated.runtime.brewConfirmationBeep = legacy.runtime.brewConfirmationBeep;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep;
  migrated.runtime.paddleReturnReminderIntervalMs =
      legacy.runtime.paddleReturnReminderIntervalMs;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      legacy.runtime.paddleReturnReminderMaxDurationMs;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.autoRetare = legacy.runtime.autoRetare;
  migrated.runtime.retareWindowMs = legacy.runtime.retareWindowMs;
  migrated.runtime.minimumCupWeightG = legacy.runtime.minimumCupWeightG;
  migrated.runtime.retareStabilitySamples =
      legacy.runtime.retareStabilitySamples;
  migrated.runtime.retareStabilityToleranceG =
      legacy.runtime.retareStabilityToleranceG;
  migrated.runtime.retareStabilityMaxGapMs =
      legacy.runtime.retareStabilityMaxGapMs;
  migrated.runtime.confirmationTimeoutMs =
      legacy.runtime.confirmationTimeoutMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.runtime.timezoneOffsetMinutes =
      legacy.runtime.timezoneOffsetMinutes;
  migrated.runtime.ntpServerPreset = legacy.runtime.ntpServerPreset;
  memcpy(migrated.runtime.ntpServerCustom, legacy.runtime.ntpServerCustom,
         sizeof(migrated.runtime.ntpServerCustom));
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readV11SettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV11)) {
    return false;
  }
  PersistedSettingsV11 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != 11U ||
      legacy.structureSize != sizeof(PersistedSettingsV11) ||
      legacy.checksum != persistedSettingsV11Checksum(legacy) ||
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
  migrateRuntimeConfigV11ToCurrent(legacy.runtime, migrated.runtime);
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
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

inline uint32_t persistedSettingsV4Checksum(
    const PersistedSettingsV4 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV4, checksum));
}

inline bool readLegacySettingsSlot(Preferences &preferences, const char *key,
                                   PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV3)) {
    return false;
  }
  PersistedSettingsV3 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      (legacy.schemaVersion != LEGACY_PRE_SCHEMA_FOUR_VERSION &&
       legacy.schemaVersion != LEGACY_CONFIG_SCHEMA_VERSION) ||
      legacy.structureSize != sizeof(PersistedSettingsV3) ||
      legacy.checksum != persistedSettingsV3Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare != 0;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly != 0;
  migrated.runtime.canTareStartTimer =
      legacy.runtime.canTareStartTimer != 0;
  migrated.runtime.brewConfirmationBeep =
      legacy.schemaVersion == LEGACY_CONFIG_SCHEMA_VERSION
          ? true
          : legacy.runtime.brewConfirmationBeep != 0;
  migrated.runtime.paddleReturnReminderBeep = true;
  migrated.runtime.paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.staConfigured = legacy.staConfigured != 0;
  migrated.staOpen = legacy.staOpen != 0;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readV4SettingsSlot(Preferences &preferences, const char *key,
                               PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV4)) {
    return false;
  }
  PersistedSettingsV4 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != LEGACY_SCHEMA_FOUR_VERSION ||
      legacy.structureSize != sizeof(PersistedSettingsV4) ||
      legacy.checksum != persistedSettingsV4Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare != 0;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly != 0;
  migrated.runtime.canTareStartTimer =
      legacy.runtime.canTareStartTimer != 0;
  migrated.runtime.brewConfirmationBeep =
      legacy.runtime.brewConfirmationBeep != 0;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep != 0;
  migrated.runtime.paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.staConfigured = legacy.staConfigured != 0;
  migrated.staOpen = legacy.staOpen != 0;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline uint32_t persistedSettingsV5Checksum(
    const PersistedSettingsV5 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV5, checksum));
}

inline bool readV5SettingsSlot(Preferences &preferences, const char *key,
                               PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV5)) {
    return false;
  }
  PersistedSettingsV5 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != LEGACY_SCHEMA_FIVE_VERSION ||
      legacy.structureSize != sizeof(PersistedSettingsV5) ||
      legacy.checksum != persistedSettingsV5Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare != 0;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly != 0;
  migrated.runtime.canTareStartTimer =
      legacy.runtime.canTareStartTimer != 0;
  migrated.runtime.brewConfirmationBeep =
      legacy.runtime.brewConfirmationBeep != 0;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep != 0;
  migrated.runtime.paddleReturnReminderIntervalMs =
      legacy.runtime.paddleReturnReminderIntervalMs;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.staConfigured = legacy.staConfigured != 0;
  migrated.staOpen = legacy.staOpen != 0;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
  if (validateRuntimeConfig(migrated.runtime) !=
      ConfigValidationError::NONE) {
    return false;
  }
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readV6SettingsSlot(Preferences &preferences, const char *key,
                               PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV6)) {
    return false;
  }
  PersistedSettingsV6 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != 6U ||
      legacy.structureSize != sizeof(PersistedSettingsV6) ||
      legacy.checksum != persistedSettingsV6Checksum(legacy) ||
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
  migrated.runtime.revision = legacy.runtime.revision;
  migrated.runtime.goalWeightG = legacy.runtime.goalWeightG;
  migrated.runtime.weightOffsetG = legacy.runtime.weightOffsetG;
  migrated.runtime.autoTare = legacy.runtime.autoTare != 0;
  migrated.runtime.timerOnly = legacy.runtime.timerOnly != 0;
  migrated.runtime.canTareStartTimer =
      legacy.runtime.canTareStartTimer != 0;
  migrated.runtime.brewConfirmationBeep =
      legacy.runtime.brewConfirmationBeep != 0;
  migrated.runtime.paddleReturnReminderBeep =
      legacy.runtime.paddleReturnReminderBeep != 0;
  migrated.runtime.paddleReturnReminderIntervalMs =
      legacy.runtime.paddleReturnReminderIntervalMs;
  migrated.runtime.paddleReturnReminderMaxDurationMs =
      legacy.runtime.paddleReturnReminderMaxDurationMs;
  migrated.runtime.rinseGestureMs = legacy.runtime.rinseGestureMs;
  migrated.runtime.rinseDurationMs = legacy.runtime.rinseDurationMs;
  migrated.runtime.operationalWallMs = legacy.runtime.operationalWallMs;
  migrated.runtime.timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  migrated.runtime.ntpServerPreset =
      static_cast<uint8_t>(NtpServerPreset::POOL);
  migrated.staConfigured = legacy.staConfigured != 0;
  migrated.staOpen = legacy.staOpen != 0;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  normalizeRuntimeConfirmationDefaults(migrated.runtime);
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
  const bool isLegacy = length == sizeof(PersistedSettingsV3) ||
                        length == sizeof(PersistedSettingsV4) ||
                        length == sizeof(PersistedSettingsV5) ||
                        length == sizeof(PersistedSettingsV6) ||
                        length == sizeof(PersistedSettingsV7) ||
                        length == sizeof(PersistedSettingsV8) ||
                        length == sizeof(PersistedSettingsV9) ||
                        length == sizeof(PersistedSettingsV10) ||
                        length == sizeof(PersistedSettingsV11);
  const bool valid =
      length == sizeof(PersistedSettings)
          ? readSettingsSlot(preferences, key, settings)
          : length == sizeof(PersistedSettingsV11)
                ? readV11SettingsSlot(preferences, key, settings)
                : length == sizeof(PersistedSettingsV10)
                ? readV10SettingsSlot(preferences, key, settings)
                : length == sizeof(PersistedSettingsV9)
                      ? readV9SettingsSlot(preferences, key, settings)
                : length == sizeof(PersistedSettingsV8)
                      ? readV8SettingsSlot(preferences, key, settings)
                : length == sizeof(PersistedSettingsV7)
                      ? readV7SettingsSlot(preferences, key, settings)
                      : length == sizeof(PersistedSettingsV6)
                            ? readV6SettingsSlot(preferences, key, settings)
                            : length == sizeof(PersistedSettingsV5)
                                  ? readV5SettingsSlot(preferences, key,
                                                       settings)
                                  : length == sizeof(PersistedSettingsV4)
                                        ? readV4SettingsSlot(preferences, key,
                                                             settings)
                                        : length == sizeof(PersistedSettingsV3) &&
                                              readLegacySettingsSlot(
                                                  preferences, key, settings);
  if (legacyFormat != nullptr) {
    *legacyFormat = valid && isLegacy;
  }
  return valid;
}

inline bool loadPersistedSettings(PersistedSettings &settings) {
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
  } else if (!secondValid) {
    settings = first;
  } else {
    const int32_t revisionDelta =
        static_cast<int32_t>(second.storageRevision - first.storageRevision);
    if (revisionDelta > 0 ||
        (revisionDelta == 0 && secondLegacy && !firstLegacy)) {
      settings = second;
    } else {
      settings = first;
    }
  }
  // Version 2 used the byte now occupied by brewConfirmationBeep as padding.
  // Version 3 used the byte now occupied by paddleReturnReminderBeep as
  // padding. Preserve the former always-on defaults while keeping old
  // credentials and workflow values readable.
  if (settings.schemaVersion == LEGACY_CONFIG_SCHEMA_VERSION) {
    settings.runtime.brewConfirmationBeep = true;
  }
  if (settings.schemaVersion <= LEGACY_PRE_SCHEMA_FOUR_VERSION) {
    settings.runtime.paddleReturnReminderBeep = true;
    settings.runtime.paddleReturnReminderIntervalMs =
        DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
    settings.runtime.paddleReturnReminderMaxDurationMs =
        DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  }
  if (settings.schemaVersion == 7U) {
    settings.runtime.paddleReturnReminderMaxDurationMs =
        DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
    settings.runtime.timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
    settings.runtime.ntpServerPreset =
        static_cast<uint8_t>(NtpServerPreset::POOL);
    settings.runtime.ntpServerCustom[0] = '\0';
  }
  if (settings.schemaVersion == 8U) {
    settings.runtime.autoRetare = true;
    settings.runtime.retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
    settings.runtime.minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
    settings.runtime.confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  }
  if (settings.schemaVersion == 9U) {
    settings.runtime.retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
    settings.runtime.retareStabilityToleranceG =
        DEFAULT_RETARE_STABILITY_TOLERANCE_G;
    settings.runtime.retareStabilityMaxGapMs =
        DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
    settings.runtime.retareStabilityMinDurationMs =
        DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  }
  if (settings.schemaVersion == PREVIOUS_CONFIG_SCHEMA_VERSION) {
    settings.runtime.retareStabilityMinDurationMs =
        DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  }
  if (settings.schemaVersion <= PREVIOUS_CONFIG_SCHEMA_VERSION) {
    normalizeRuntimeConfirmationDefaults(settings.runtime);
  }
  return true;
}

inline bool savePersistedSettings(PersistedSettings &settings) {
  PersistedSettings candidate = settings;
  PersistedSettings current = {};
  if (loadPersistedSettings(current)) {
    // Only a committed slot is allowed to advance the base revision. The
    // caller may contain a revision prepared by an earlier failed write.
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

inline bool initializeDefaultSettings(PersistedSettings &settings,
                                      uint8_t legacyGoal,
                                      uint8_t legacyOffsetTenths,
                                      bool *legacyMigrated = nullptr) {
  settings = PersistedSettings{};
  bool migrated = false;
  if (legacyGoal >= MIN_GOAL_WEIGHT_G && legacyGoal <= MAX_GOAL_WEIGHT_G) {
    settings.runtime.goalWeightG = legacyGoal;
    migrated = true;
  }
  const float legacyOffset = legacyOffsetTenths / 10.0f;
  if (isfinite(legacyOffset) && legacyOffset >= 0.0f &&
      legacyOffset <= MAX_OFFSET_G) {
    settings.runtime.weightOffsetG = legacyOffset;
    migrated = true;
  }
  if (!initializeDefaultAuthentication(settings)) {
    return false;
  }
  finalizePersistedSettings(settings);
  if (legacyMigrated != nullptr) {
    *legacyMigrated = migrated;
  }
  return true;
}

// A factory reset deliberately clears the whole namespace so records from an
// older schema or an abandoned alternate slot cannot be selected on a later
// boot. Recreate both redundant slots before reporting success; accepting one
// valid copy still makes the operation recoverable if the second NVS write
// fails.
inline bool resetPersistedSettingsToFactory(PersistedSettings &settings) {
  PersistedSettings first = {};
  if (!initializeDefaultSettings(first, 255, 255)) {
    return false;
  }
  first.storageRevision = 1;
  finalizePersistedSettings(first);
  PersistedSettings second = first;
  second.storageRevision = 2;
  finalizePersistedSettings(second);

  // Invalidate the pre-NVS values as well. Otherwise they could be migrated
  // back after a later loss of both NVS slots, partially undoing the reset.
  EEPROM.write(LEGACY_WEIGHT_EEPROM_ADDRESS, ERASED_EEPROM_VALUE);
  EEPROM.write(LEGACY_OFFSET_EEPROM_ADDRESS, ERASED_EEPROM_VALUE);
  if (!EEPROM.commit()) {
    return false;
  }

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
