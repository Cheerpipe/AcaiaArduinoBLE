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
constexpr const char *DEFAULT_AP_PASSWORD = "Micra1234";
constexpr size_t AUTH_SALT_LENGTH = 16;
constexpr size_t AUTH_HASH_LENGTH = 32;

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
  char apPassword[WIFI_PASSWORD_CAPACITY] = "Micra1234";
  uint8_t authSalt[AUTH_SALT_LENGTH] = {};
  uint8_t authHash[AUTH_HASH_LENGTH] = {};
  uint32_t checksum = 0;
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

inline bool validPersistedSettings(const PersistedSettings &settings) {
  if (settings.magic != PERSISTED_SETTINGS_MAGIC ||
      (settings.schemaVersion != CONFIG_SCHEMA_VERSION &&
       settings.schemaVersion != LEGACY_CONFIG_SCHEMA_VERSION) ||
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

inline bool loadPersistedSettings(PersistedSettings &settings) {
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    return false;
  }
  PersistedSettings first = {};
  PersistedSettings second = {};
  const bool firstValid = readSettingsSlot(preferences, SETTINGS_SLOT_A, first);
  const bool secondValid =
      readSettingsSlot(preferences, SETTINGS_SLOT_B, second);
  preferences.end();

  if (!firstValid && !secondValid) {
    return false;
  }
  if (!firstValid) {
    settings = second;
  } else if (!secondValid) {
    settings = first;
  } else {
    settings = static_cast<int32_t>(second.storageRevision -
                                    first.storageRevision) > 0
                   ? second
                   : first;
  }
  // Version 2 used the byte now occupied by brewConfirmationBeep as padding.
  // Preserve the previous always-beep behaviour while keeping old credentials
  // and workflow values readable.
  if (settings.schemaVersion == LEGACY_CONFIG_SCHEMA_VERSION) {
    settings.runtime.brewConfirmationBeep = true;
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
  const bool saved =
      preferences.putBytes(target, &candidate, sizeof(candidate)) ==
      sizeof(candidate);
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
  if (!refreshAuthentication(settings, DEFAULT_AP_PASSWORD)) {
    return false;
  }
  finalizePersistedSettings(settings);
  if (legacyMigrated != nullptr) {
    *legacyMigrated = migrated;
  }
  return true;
}

}  // namespace shotstopper
