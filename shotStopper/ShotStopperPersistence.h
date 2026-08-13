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
// ~1k SHA-256 rounds: strong enough for SoftAP offline guessing, cheap on ESP32.
constexpr uint16_t AUTH_HASH_ITERATIONS = 1000;
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
  uint8_t staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t staIp[4] = {};
  uint8_t staNetmask[4] = {};
  uint8_t staGateway[4] = {};
  uint8_t staDns1[4] = {};
  uint8_t staDns2[4] = {};
  uint8_t staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  bool lkgValid = false;
  bool lkgOpen = false;
  char lkgSsid[WIFI_SSID_CAPACITY] = {};
  char lkgPassword[WIFI_PASSWORD_CAPACITY] = {};
  uint8_t lkgIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t lkgIp[4] = {};
  uint8_t lkgNetmask[4] = {};
  uint8_t lkgGateway[4] = {};
  uint8_t lkgDns1[4] = {};
  uint8_t lkgDns2[4] = {};
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
  bool firstDropBeep = true;
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
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct RuntimeConfigV13 {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  bool timerOnly = false;
  bool canTareStartTimer = true;
  bool firstDropBeep = true;
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
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
  bool fastExtractionGuardEnabled = false;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
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

struct PersistedSettingsV13 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfigV13 runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

struct PersistedSettingsV14 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint32_t structureSize;
  uint32_t storageRevision;
  RuntimeConfig runtime;
  bool staConfigured;
  bool staOpen;
  char staSsid[WIFI_SSID_CAPACITY];
  char staPassword[WIFI_PASSWORD_CAPACITY];
  char apPassword[WIFI_PASSWORD_CAPACITY];
  uint8_t authSalt[AUTH_SALT_LENGTH];
  uint8_t authHash[AUTH_HASH_LENGTH];
  uint32_t checksum;
};

inline bool sha256Bytes(const uint8_t *data, size_t length,
                        uint8_t output[AUTH_HASH_LENGTH]) {
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  const bool success =
      mbedtls_sha256_starts(&context, 0) == 0 &&
      mbedtls_sha256_update(&context, data, length) == 0 &&
      mbedtls_sha256_finish(&context, output) == 0;
  mbedtls_sha256_free(&context);
  return success;
}

// Legacy single-pass SHA-256(salt || password). Kept for verifying older NVS.
inline bool calculatePasswordHashLegacy(const uint8_t salt[AUTH_SALT_LENGTH],
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

// Iterated SHA-256: H0 = SHA256(salt || password), Hi = SHA256(Hi-1 || salt).
// Avoids mbedtls PBKDF2/HMAC deps while remaining cheap on ESP32.
inline bool calculatePasswordHash(const uint8_t salt[AUTH_SALT_LENGTH],
                                  const char *password,
                                  uint8_t output[AUTH_HASH_LENGTH]) {
  size_t passwordLength = 0;
  if (!boundedCString(password, WIFI_PASSWORD_CAPACITY, &passwordLength)) {
    return false;
  }

  uint8_t block[AUTH_SALT_LENGTH + WIFI_PASSWORD_CAPACITY] = {};
  memcpy(block, salt, AUTH_SALT_LENGTH);
  memcpy(block + AUTH_SALT_LENGTH, password, passwordLength);
  if (!sha256Bytes(block, AUTH_SALT_LENGTH + passwordLength, output)) {
    memset(block, 0, sizeof(block));
    return false;
  }
  memset(block, 0, sizeof(block));

  uint8_t roundInput[AUTH_HASH_LENGTH + AUTH_SALT_LENGTH] = {};
  for (uint16_t round = 1; round < AUTH_HASH_ITERATIONS; ++round) {
    memcpy(roundInput, output, AUTH_HASH_LENGTH);
    memcpy(roundInput + AUTH_HASH_LENGTH, salt, AUTH_SALT_LENGTH);
    if (!sha256Bytes(roundInput, sizeof(roundInput), output)) {
      memset(roundInput, 0, sizeof(roundInput));
      return false;
    }
  }
  memset(roundInput, 0, sizeof(roundInput));
  return true;
}

inline bool constantTimeEqual(const uint8_t *left, const uint8_t *right,
                              size_t length) {
  uint8_t difference = 0;
  for (size_t index = 0; index < length; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

inline bool passwordHashMatches(const uint8_t salt[AUTH_SALT_LENGTH],
                                const char *password,
                                const uint8_t stored[AUTH_HASH_LENGTH]) {
  uint8_t candidate[AUTH_HASH_LENGTH] = {};
  if (calculatePasswordHash(salt, password, candidate) &&
      constantTimeEqual(stored, candidate, AUTH_HASH_LENGTH)) {
    memset(candidate, 0, sizeof(candidate));
    return true;
  }
  if (calculatePasswordHashLegacy(salt, password, candidate) &&
      constantTimeEqual(stored, candidate, AUTH_HASH_LENGTH)) {
    memset(candidate, 0, sizeof(candidate));
    return true;
  }
  memset(candidate, 0, sizeof(candidate));
  return false;
}

inline bool isFactoryDefaultPassword(const char *password) {
  size_t storedLength = 0;
  size_t defaultLength = 0;
  if (!boundedCString(password, WIFI_PASSWORD_CAPACITY, &storedLength) ||
      !boundedCString(DEFAULT_AP_PASSWORD, WIFI_PASSWORD_CAPACITY,
                      &defaultLength) ||
      storedLength != defaultLength) {
    return false;
  }
  return constantTimeEqual(reinterpret_cast<const uint8_t *>(password),
                           reinterpret_cast<const uint8_t *>(DEFAULT_AP_PASSWORD),
                           storedLength);
}

inline bool passwordIsFactoryDefault(const PersistedSettings &settings) {
  return isFactoryDefaultPassword(settings.apPassword);
}

inline bool refreshAuthentication(PersistedSettings &settings,
                                  const char *newPassword) {
  if (!validAccessPointPassword(newPassword)) {
    return false;
  }
  // Refuse re-selecting the published factory credential.
  if (isFactoryDefaultPassword(newPassword)) {
    return false;
  }
  memset(settings.apPassword, 0, sizeof(settings.apPassword));
  strncpy(settings.apPassword, newPassword, sizeof(settings.apPassword) - 1);
  esp_fill_random(settings.authSalt, sizeof(settings.authSalt));
  return calculatePasswordHash(settings.authSalt, settings.apPassword,
                               settings.authHash);
}

inline bool initializeDefaultAuthentication(PersistedSettings &settings) {
  // Factory boot keeps the known SoftAP password, but UI mutations require a
  // change (see passwordIsFactoryDefault).
  if (!validAccessPointPassword(DEFAULT_AP_PASSWORD)) {
    return false;
  }
  memset(settings.apPassword, 0, sizeof(settings.apPassword));
  strncpy(settings.apPassword, DEFAULT_AP_PASSWORD,
          sizeof(settings.apPassword) - 1);
  esp_fill_random(settings.authSalt, sizeof(settings.authSalt));
  return calculatePasswordHash(settings.authSalt, settings.apPassword,
                               settings.authHash);
}

inline bool verifyAdminPassword(const PersistedSettings &settings,
                                const char *candidate) {
  return passwordHashMatches(settings.authSalt, candidate, settings.authHash);
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

inline uint32_t persistedSettingsV13Checksum(
    const PersistedSettingsV13 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV13, checksum));
}

inline uint32_t persistedSettingsV14Checksum(
    const PersistedSettingsV14 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV14, checksum));
}

inline void clearStaAddressFields(PersistedSettings &settings) {
  settings.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  memset(settings.staIp, 0, sizeof(settings.staIp));
  memset(settings.staNetmask, 0, sizeof(settings.staNetmask));
  memset(settings.staGateway, 0, sizeof(settings.staGateway));
  memset(settings.staDns1, 0, sizeof(settings.staDns1));
  memset(settings.staDns2, 0, sizeof(settings.staDns2));
  settings.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
}

inline void clearLkgNetwork(PersistedSettings &settings) {
  settings.lkgValid = false;
  settings.lkgOpen = false;
  memset(settings.lkgSsid, 0, sizeof(settings.lkgSsid));
  memset(settings.lkgPassword, 0, sizeof(settings.lkgPassword));
  settings.lkgIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  memset(settings.lkgIp, 0, sizeof(settings.lkgIp));
  memset(settings.lkgNetmask, 0, sizeof(settings.lkgNetmask));
  memset(settings.lkgGateway, 0, sizeof(settings.lkgGateway));
  memset(settings.lkgDns1, 0, sizeof(settings.lkgDns1));
  memset(settings.lkgDns2, 0, sizeof(settings.lkgDns2));
}

inline void clearStaNetwork(PersistedSettings &settings) {
  settings.staConfigured = false;
  settings.staOpen = false;
  memset(settings.staSsid, 0, sizeof(settings.staSsid));
  memset(settings.staPassword, 0, sizeof(settings.staPassword));
  clearStaAddressFields(settings);
  clearLkgNetwork(settings);
}

inline void copyActiveStaToLkg(PersistedSettings &settings) {
  if (!settings.staConfigured) {
    clearLkgNetwork(settings);
    return;
  }
  settings.lkgValid = true;
  settings.lkgOpen = settings.staOpen;
  memset(settings.lkgSsid, 0, sizeof(settings.lkgSsid));
  memset(settings.lkgPassword, 0, sizeof(settings.lkgPassword));
  strncpy(settings.lkgSsid, settings.staSsid, sizeof(settings.lkgSsid) - 1);
  strncpy(settings.lkgPassword, settings.staPassword,
          sizeof(settings.lkgPassword) - 1);
  settings.lkgIpMode = settings.staIpMode;
  memcpy(settings.lkgIp, settings.staIp, sizeof(settings.lkgIp));
  memcpy(settings.lkgNetmask, settings.staNetmask, sizeof(settings.lkgNetmask));
  memcpy(settings.lkgGateway, settings.staGateway, sizeof(settings.lkgGateway));
  memcpy(settings.lkgDns1, settings.staDns1, sizeof(settings.lkgDns1));
  memcpy(settings.lkgDns2, settings.staDns2, sizeof(settings.lkgDns2));
}

inline bool restoreLkgToActive(PersistedSettings &settings) {
  if (!settings.lkgValid || !validWifiSsid(settings.lkgSsid) ||
      !validWifiPassword(settings.lkgPassword, settings.lkgOpen) ||
      !validStaAddressConfig(settings.lkgIpMode, settings.lkgIp,
                             settings.lkgNetmask, settings.lkgGateway,
                             settings.lkgDns1, settings.lkgDns2)) {
    return false;
  }
  settings.staConfigured = true;
  settings.staOpen = settings.lkgOpen;
  memset(settings.staSsid, 0, sizeof(settings.staSsid));
  memset(settings.staPassword, 0, sizeof(settings.staPassword));
  strncpy(settings.staSsid, settings.lkgSsid, sizeof(settings.staSsid) - 1);
  strncpy(settings.staPassword, settings.lkgPassword,
          sizeof(settings.staPassword) - 1);
  settings.staIpMode = settings.lkgIpMode;
  memcpy(settings.staIp, settings.lkgIp, sizeof(settings.staIp));
  memcpy(settings.staNetmask, settings.lkgNetmask, sizeof(settings.staNetmask));
  memcpy(settings.staGateway, settings.lkgGateway, sizeof(settings.staGateway));
  memcpy(settings.staDns1, settings.lkgDns1, sizeof(settings.staDns1));
  memcpy(settings.staDns2, settings.lkgDns2, sizeof(settings.staDns2));
  settings.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  return true;
}

inline bool validPersistedStaNetwork(const PersistedSettings &settings) {
  if (!settings.staConfigured) {
    return !settings.lkgValid &&
           settings.staConfigState ==
               static_cast<uint8_t>(StaConfigState::CONFIRMED) &&
           validStaAddressConfig(settings.staIpMode, settings.staIp,
                                 settings.staNetmask, settings.staGateway,
                                 settings.staDns1, settings.staDns2);
  }
  if (!validWifiSsid(settings.staSsid) ||
      !validWifiPassword(settings.staPassword, settings.staOpen) ||
      !validStaConfigState(settings.staConfigState) ||
      !validStaAddressConfig(settings.staIpMode, settings.staIp,
                             settings.staNetmask, settings.staGateway,
                             settings.staDns1, settings.staDns2)) {
    return false;
  }
  if (!settings.lkgValid) {
    return true;
  }
  return validWifiSsid(settings.lkgSsid) &&
         validWifiPassword(settings.lkgPassword, settings.lkgOpen) &&
         validStaAddressConfig(settings.lkgIpMode, settings.lkgIp,
                               settings.lkgNetmask, settings.lkgGateway,
                               settings.lkgDns1, settings.lkgDns2);
}

inline void normalizeRuntimeBbwProtectionDefaults(RuntimeConfig &runtime) {
  if (runtime.bbwProtectionMs < DEFAULT_BBW_PROTECTION_MS) {
    runtime.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  }
  const uint32_t minimum = minimumBbwProtectionMs(runtime);
  if (runtime.bbwProtectionMs < minimum) {
    runtime.bbwProtectionMs = minimum;
  }
}

inline void applyAutoToManualGuardDefaults(RuntimeConfig &runtime) {
  runtime.autoToManualGuardEnabled = true;
  runtime.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  runtime.autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  resetAutoToManualGuardSamples(runtime.autoToManualGuardSamplesDs);
}

inline void migrateRuntimeConfigV13ToV14(const RuntimeConfigV13 &legacy,
                                         RuntimeConfig &runtime) {
  runtime = RuntimeConfig{};
  runtime.revision = legacy.revision;
  runtime.goalWeightG = legacy.goalWeightG;
  runtime.weightOffsetG = legacy.weightOffsetG;
  runtime.autoTare = legacy.autoTare;
  runtime.timerOnly = legacy.timerOnly;
  runtime.canTareStartTimer = legacy.canTareStartTimer;
  runtime.firstDropBeep = legacy.firstDropBeep;
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
  runtime.bbwProtectionMs = legacy.bbwProtectionMs;
  runtime.operationalWallMs = legacy.operationalWallMs;
  runtime.timezoneOffsetMinutes = legacy.timezoneOffsetMinutes;
  runtime.ntpServerPreset = legacy.ntpServerPreset;
  memcpy(runtime.ntpServerCustom, legacy.ntpServerCustom,
         sizeof(runtime.ntpServerCustom));
  runtime.fastExtractionGuardEnabled = legacy.fastExtractionGuardEnabled;
  runtime.maxRecoveryWeightG = legacy.maxRecoveryWeightG;
  runtime.minBrewTimeMs = legacy.minBrewTimeMs;
  applyAutoToManualGuardDefaults(runtime);
  normalizeRuntimeBbwProtectionDefaults(runtime);
}

inline void migrateRuntimeConfigV12ToV13(const RuntimeConfigV12 &legacy,
                                         RuntimeConfigV13 &runtime) {
  runtime = RuntimeConfigV13{};
  runtime.revision = legacy.revision;
  runtime.goalWeightG = legacy.goalWeightG;
  runtime.weightOffsetG = legacy.weightOffsetG;
  runtime.autoTare = legacy.autoTare;
  runtime.timerOnly = legacy.timerOnly;
  runtime.canTareStartTimer = legacy.canTareStartTimer;
  runtime.firstDropBeep = legacy.firstDropBeep;
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
  runtime.bbwProtectionMs = legacy.bbwProtectionMs;
  runtime.operationalWallMs = legacy.operationalWallMs;
  runtime.timezoneOffsetMinutes = legacy.timezoneOffsetMinutes;
  runtime.ntpServerPreset = legacy.ntpServerPreset;
  memcpy(runtime.ntpServerCustom, legacy.ntpServerCustom,
         sizeof(runtime.ntpServerCustom));
  runtime.fastExtractionGuardEnabled = false;
  runtime.maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  runtime.minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
}

inline void migrateRuntimeConfigV12ToV14(const RuntimeConfigV12 &legacy,
                                         RuntimeConfig &runtime) {
  RuntimeConfigV13 mid = {};
  migrateRuntimeConfigV12ToV13(legacy, mid);
  migrateRuntimeConfigV13ToV14(mid, runtime);
}

inline bool validPersistedSettings(const PersistedSettings &settings) {
  if (settings.magic != PERSISTED_SETTINGS_MAGIC ||
      settings.schemaVersion != CONFIG_SCHEMA_VERSION ||
      settings.structureSize != sizeof(PersistedSettings) ||
      settings.checksum != persistedSettingsChecksum(settings) ||
      validateRuntimeConfig(settings.runtime) != ConfigValidationError::NONE ||
      !validAccessPointPassword(settings.apPassword) ||
      !validPersistedStaNetwork(settings)) {
    return false;
  }
  return passwordHashMatches(settings.authSalt, settings.apPassword,
                             settings.authHash);
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
      legacy.schemaVersion != CONFIG_SCHEMA_VERSION_V12 ||
      legacy.structureSize != sizeof(PersistedSettingsV12) ||
      legacy.checksum != persistedSettingsV12Checksum(legacy) ||
      !validAccessPointPassword(legacy.apPassword) ||
      (legacy.staConfigured != 0 &&
       (!validWifiSsid(legacy.staSsid) ||
        !validWifiPassword(legacy.staPassword, legacy.staOpen != 0)))) {
    return false;
  }

  if (!passwordHashMatches(legacy.authSalt, legacy.apPassword,
                           legacy.authHash)) {
    return false;
  }

  PersistedSettings migrated = {};
  migrated.storageRevision = legacy.storageRevision;
  migrateRuntimeConfigV12ToV14(legacy.runtime, migrated.runtime);
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  clearStaAddressFields(migrated);
  clearLkgNetwork(migrated);
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

inline bool readV13SettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV13)) {
    return false;
  }
  PersistedSettingsV13 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != CONFIG_SCHEMA_VERSION_V13 ||
      legacy.structureSize != sizeof(PersistedSettingsV13) ||
      legacy.checksum != persistedSettingsV13Checksum(legacy) ||
      !validAccessPointPassword(legacy.apPassword) ||
      (legacy.staConfigured != 0 &&
       (!validWifiSsid(legacy.staSsid) ||
        !validWifiPassword(legacy.staPassword, legacy.staOpen != 0)))) {
    return false;
  }

  if (!passwordHashMatches(legacy.authSalt, legacy.apPassword,
                           legacy.authHash)) {
    return false;
  }

  PersistedSettings migrated = {};
  migrated.storageRevision = legacy.storageRevision;
  migrateRuntimeConfigV13ToV14(legacy.runtime, migrated.runtime);
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  clearStaAddressFields(migrated);
  clearLkgNetwork(migrated);
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

inline bool readV14SettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettingsV14)) {
    return false;
  }
  PersistedSettingsV14 legacy = {};
  if (preferences.getBytes(key, &legacy, sizeof(legacy)) != sizeof(legacy) ||
      legacy.magic != PERSISTED_SETTINGS_MAGIC ||
      legacy.schemaVersion != PREVIOUS_CONFIG_SCHEMA_VERSION ||
      legacy.structureSize != sizeof(PersistedSettingsV14) ||
      legacy.checksum != persistedSettingsV14Checksum(legacy) ||
      !validAccessPointPassword(legacy.apPassword) ||
      validateRuntimeConfig(legacy.runtime) != ConfigValidationError::NONE ||
      (legacy.staConfigured != 0 &&
       (!validWifiSsid(legacy.staSsid) ||
        !validWifiPassword(legacy.staPassword, legacy.staOpen != 0)))) {
    return false;
  }

  if (!passwordHashMatches(legacy.authSalt, legacy.apPassword,
                           legacy.authHash)) {
    return false;
  }

  PersistedSettings migrated = {};
  migrated.storageRevision = legacy.storageRevision;
  migrated.runtime = legacy.runtime;
  migrated.staConfigured = legacy.staConfigured;
  migrated.staOpen = legacy.staOpen;
  memcpy(migrated.staSsid, legacy.staSsid, sizeof(migrated.staSsid));
  memcpy(migrated.staPassword, legacy.staPassword,
         sizeof(migrated.staPassword));
  clearStaAddressFields(migrated);
  clearLkgNetwork(migrated);
  memcpy(migrated.apPassword, legacy.apPassword,
         sizeof(migrated.apPassword));
  memcpy(migrated.authSalt, legacy.authSalt, sizeof(migrated.authSalt));
  memcpy(migrated.authHash, legacy.authHash, sizeof(migrated.authHash));
  finalizePersistedSettings(migrated);
  settings = migrated;
  return true;
}

inline bool readAnySettingsSlot(Preferences &preferences, const char *key,
                                PersistedSettings &settings,
                                bool *legacyFormat = nullptr) {
  const size_t length = preferences.getBytesLength(key);
  const bool isLegacy = length == sizeof(PersistedSettingsV12) ||
                        length == sizeof(PersistedSettingsV13) ||
                        length == sizeof(PersistedSettingsV14);
  bool valid = false;
  if (length == sizeof(PersistedSettings)) {
    valid = readSettingsSlot(preferences, key, settings);
  } else if (length == sizeof(PersistedSettingsV14)) {
    valid = readV14SettingsSlot(preferences, key, settings);
  } else if (length == sizeof(PersistedSettingsV13)) {
    valid = readV13SettingsSlot(preferences, key, settings);
  } else if (length == sizeof(PersistedSettingsV12)) {
    valid = readV12SettingsSlot(preferences, key, settings);
  }
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
