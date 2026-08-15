#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperPresets.h"

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include "tests/persistence_host_stubs.h"
#else
#include <Preferences.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
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
  ShotPresetBank presets = {};
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
  char preferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredScaleName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY] = {};
  uint32_t checksum = 0;
};

static_assert(sizeof(PersistedSettings) <= PERSISTED_SETTINGS_NVS_BUDGET,
              "PersistedSettings exceeds NVS dual-slot budget");

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
  // Factory boot keeps the published SoftAP / Web UI password.
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

inline void ensurePersistedPresetBank(PersistedSettings &settings) {
  // Only migrate recipe→bank when empty. Invalid activeId is repaired in
  // ensureShotPresetBank without wiping customs or copying session Manual.
  if (settings.presets.count == 0) {
    migrateRecipeFromRuntimeToBank(settings.runtime, settings.presets);
  }
  ensureShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                       settings.runtime.autoRetare);
}

inline bool validPersistedSettings(const PersistedSettings &settings) {
  if (settings.magic != PERSISTED_SETTINGS_MAGIC ||
      settings.schemaVersion != CONFIG_SCHEMA_VERSION ||
      settings.structureSize != sizeof(PersistedSettings) ||
      settings.checksum != persistedSettingsChecksum(settings) ||
      validateRuntimeConfig(settings.runtime) != ConfigValidationError::NONE ||
      !validateShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                              settings.runtime.autoRetare) ||
      !validAccessPointPassword(settings.apPassword) ||
      !validPreferredScaleMac(settings.preferredScaleMac) ||
      !validPreferredScaleName(settings.preferredScaleName) ||
      !validScaleHistoryEntries(settings.scaleHistory) ||
      !validPersistedStaNetwork(settings)) {
    return false;
  }
  return passwordHashMatches(settings.authSalt, settings.apPassword,
                             settings.authHash);
}

inline void finalizePersistedSettings(PersistedSettings &settings) {
  ensurePersistedPresetBank(settings);
  uint32_t seedSeq = 0;
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (settings.scaleHistory[i].lastSeenSeq > seedSeq) {
      seedSeq = settings.scaleHistory[i].lastSeenSeq;
    }
  }
  seedScaleHistoryFromPreferred(settings.scaleHistory, seedSeq,
                                settings.preferredScaleMac,
                                settings.preferredScaleName);
  settings.magic = PERSISTED_SETTINGS_MAGIC;
  settings.schemaVersion = CONFIG_SCHEMA_VERSION;
  settings.structureSize = sizeof(PersistedSettings);
  settings.checksum = 0;
  settings.checksum = persistedSettingsChecksum(settings);
}

// Dual-slot / save scratch. Kept off the Arduino loopTask stack
// (default 8 KB): nested load→save would otherwise put multiple copies on the
// stack and panic in NVS (LoadStoreError).
inline PersistedSettings &persistedSettingsScratch(uint8_t index) {
  static PersistedSettings slots[3] = {};
  return slots[index % 3];
}

inline bool readSettingsSlot(Preferences &preferences, const char *key,
                             PersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(PersistedSettings)) {
    return false;
  }
  // Read directly into the out-param to avoid borrowing scratch slots that
  // load/save may already hold (candidate lives in scratch[2] during save).
  if (preferences.getBytes(key, &settings, sizeof(settings)) !=
      sizeof(settings)) {
    return false;
  }
  if (!validPersistedSettings(settings)) {
    return false;
  }
  return true;
}

inline bool loadPersistedSettings(PersistedSettings &settings) {
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    return false;
  }
  PersistedSettings &first = persistedSettingsScratch(0);
  PersistedSettings &second = persistedSettingsScratch(1);
  first = PersistedSettings{};
  second = PersistedSettings{};
  const bool firstValid = readSettingsSlot(preferences, SETTINGS_SLOT_A, first);
  const bool secondValid = readSettingsSlot(preferences, SETTINGS_SLOT_B, second);
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
    if (revisionDelta > 0) {
      settings = second;
    } else {
      settings = first;
    }
  }
  return true;
}
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
inline SemaphoreHandle_t settingsNvsMutexHandle() {
  static SemaphoreHandle_t handle = xSemaphoreCreateMutex();
  return handle;
}

inline void lockSettingsNvs() {
  SemaphoreHandle_t handle = settingsNvsMutexHandle();
  if (handle != nullptr) {
    xSemaphoreTake(handle, portMAX_DELAY);
  }
}

inline void unlockSettingsNvs() {
  SemaphoreHandle_t handle = settingsNvsMutexHandle();
  if (handle != nullptr) {
    xSemaphoreGive(handle);
  }
}

inline void yieldSettingsNvs() { vTaskDelay(pdMS_TO_TICKS(1)); }

inline void feedSettingsNvsWatchdog() { (void)esp_task_wdt_reset(); }
#else
inline void lockSettingsNvs() {}
inline void unlockSettingsNvs() {}
inline void yieldSettingsNvs() {}
inline void feedSettingsNvsWatchdog() {}
#endif

inline void overlayLivePersistedSettings(PersistedSettings &settings,
                                         const RuntimeConfig &runtime,
                                         const ShotPresetBank &presets) {
  settings.runtime = runtime;
  settings.presets = presets;
}

inline uint32_t &durableStorageRevision() {
  static uint32_t revision = 0;
  return revision;
}

inline bool &durableStorageRevisionValid() {
  static bool valid = false;
  return valid;
}

inline void noteDurableStorageRevision(uint32_t revision) {
  lockSettingsNvs();
  durableStorageRevision() = revision;
  durableStorageRevisionValid() = revision != 0;
  unlockSettingsNvs();
}

inline void resetDurableStorageRevision() {
  lockSettingsNvs();
  durableStorageRevision() = 0;
  durableStorageRevisionValid() = false;
  unlockSettingsNvs();
}

inline bool savePersistedSettings(PersistedSettings &settings) {
  PersistedSettings &candidate = persistedSettingsScratch(2);
  PersistedSettings &current = persistedSettingsScratch(0);
  candidate = settings;
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  lockSettingsNvs();
  if (durableStorageRevisionValid()) {
    candidate.storageRevision = durableStorageRevision();
  } else if (candidate.storageRevision == 0) {
    current = PersistedSettings{};
    if (loadPersistedSettings(current)) {
      candidate.storageRevision = current.storageRevision;
    }
  }
  ++candidate.storageRevision;
  if (candidate.storageRevision == 0) {
    candidate.storageRevision = 1;
  }
  finalizePersistedSettings(candidate);

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    feedSettingsNvsWatchdog();
    return false;
  }
  const char *target =
      (candidate.storageRevision & 1U) ? SETTINGS_SLOT_A : SETTINGS_SLOT_B;
  const bool written =
      preferences.putBytes(target, &candidate, sizeof(candidate)) ==
      sizeof(candidate);
  PersistedSettings &verified = persistedSettingsScratch(0);
  verified = PersistedSettings{};
  const bool saved = written &&
                     readSettingsSlot(preferences, target, verified) &&
                     verified.storageRevision == candidate.storageRevision &&
                     memcmp(&verified, &candidate, sizeof(candidate)) == 0;
  preferences.end();
  if (saved) {
    durableStorageRevision() = candidate.storageRevision;
    durableStorageRevisionValid() = true;
    settings = candidate;
  }
  unlockSettingsNvs();
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
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
  PersistedSettings &first = persistedSettingsScratch(0);
  first = PersistedSettings{};
  if (!initializeDefaultSettings(first)) {
    return false;
  }
  first.storageRevision = 1;
  finalizePersistedSettings(first);
  PersistedSettings &second = persistedSettingsScratch(1);
  second = first;
  second.storageRevision = 2;
  finalizePersistedSettings(second);

  lockSettingsNvs();
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  if (!preferences.clear()) {
    preferences.end();
    unlockSettingsNvs();
    return false;
  }
  const bool firstSaved =
      preferences.putBytes(SETTINGS_SLOT_A, &first, sizeof(first)) ==
      sizeof(first);
  const bool secondSaved =
      preferences.putBytes(SETTINGS_SLOT_B, &second, sizeof(second)) ==
      sizeof(second);
  PersistedSettings &verifiedFirst = persistedSettingsScratch(2);
  verifiedFirst = PersistedSettings{};
  const bool firstVerified =
      firstSaved && readSettingsSlot(preferences, SETTINGS_SLOT_A,
                                     verifiedFirst);
  // Reuse slot 0 for the second verification (slot contents no longer needed).
  PersistedSettings &verifiedSecond = persistedSettingsScratch(0);
  verifiedSecond = PersistedSettings{};
  const bool secondVerified =
      secondSaved && readSettingsSlot(preferences, SETTINGS_SLOT_B,
                                      verifiedSecond);
  preferences.end();
  unlockSettingsNvs();

  if (!firstVerified && !secondVerified) {
    return false;
  }
  settings = secondVerified ? verifiedSecond : verifiedFirst;
  noteDurableStorageRevision(settings.storageRevision);
  return true;
}

}  // namespace shotstopper
