#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperPresets.h"

namespace shotstopper {

constexpr uint32_t PERSISTED_SETTINGS_MAGIC = 0x53544F50U;  // "STOP"
constexpr const char *SETTINGS_NAMESPACE = "shotstopper";
constexpr const char *SETTINGS_SLOT_A = "settingsA";
constexpr const char *SETTINGS_SLOT_B = "settingsB";
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
  char preferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredScaleName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY] = {};
  uint32_t checksum = 0;
};

struct PersistedSettingsHeader {
  uint32_t magic = 0;
  uint32_t schemaVersion = 0;
  uint32_t structureSize = 0;
  uint32_t storageRevision = 0;
};

static_assert(offsetof(PersistedSettings, storageRevision) + sizeof(uint32_t) ==
                  sizeof(PersistedSettingsHeader),
              "PersistedSettings header layout changed");

static_assert(sizeof(PersistedSettings) <= PERSISTED_SETTINGS_NVS_BUDGET,
              "PersistedSettings exceeds NVS dual-slot budget");

inline uint32_t persistedSettingsChecksum(const PersistedSettings &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettings, checksum));
}

}  // namespace shotstopper
