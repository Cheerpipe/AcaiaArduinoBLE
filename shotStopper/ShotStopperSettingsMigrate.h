#pragma once

// Settings schema migrations.
//
// Current on-disk schema is CONFIG_SCHEMA_VERSION (V2). V1 (1912 bytes) has
// unnamed padding after staOpen; V2 names that byte staWifiSleep (factory
// default on; V1 migrate stays off) without growing the blob. Unrecognized
// blobs are rejected and factory defaults are used instead.
//
// When bumping CONFIG_SCHEMA_VERSION:
// 1. Keep the previous blob layout/size as a named legacy constant.
// 2. Add migratePersistedSettingsFromV<N>(...) that validates magic, version,
//    size, and checksum, then maps into PersistedSettings.
// 3. Call it from readSettingsSlot() for that storedLength / version.
// 4. Cover the path in persistence_host_test.cpp.

#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPresets.h"

#include <cstddef>
#include <cstring>

namespace shotstopper {

inline void ensurePersistedPresetBank(PersistedSettings &settings) {
  // Only migrate recipe→bank when empty. Invalid activeId is repaired in
  // ensureShotPresetBank without wiping customs or copying session Manual.
  if (settings.presets.count == 0) {
    migrateRecipeFromRuntimeToBank(settings.runtime, settings.presets);
  }
  ensureShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                       settings.runtime.autoRetare);
}

// V1 on-disk layout: identical to V2 except it has no staWifiSleep after
// staOpen. Size is the locked 1912-byte V1 blob.
struct PersistedSettingsV1 {
  uint32_t magic = PERSISTED_SETTINGS_MAGIC;
  uint32_t schemaVersion = 1;
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
  char devicePassword[WIFI_PASSWORD_CAPACITY] = {};
  char preferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredScaleName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY] = {};
  uint32_t checksum = 0;
};

static_assert(sizeof(PersistedSettingsV1) == 1912, "V1 settings blob size");
static_assert(sizeof(PersistedSettings) == 1912,
              "V2 names the idle-sleep flag; size stays 1912 (end padding)");
static_assert(offsetof(PersistedSettingsV1, staOpen) ==
                  offsetof(PersistedSettings, staOpen),
              "V1/V2 header through staOpen must match");
static_assert(offsetof(PersistedSettings, staWifiSleep) ==
                  offsetof(PersistedSettings, staOpen) + sizeof(bool),
              "staWifiSleep must sit immediately after staOpen");
static_assert(offsetof(PersistedSettingsV1, staSsid) + sizeof(bool) ==
                  offsetof(PersistedSettings, staSsid),
              "V2 inserts staWifiSleep immediately before staSsid");

inline uint32_t persistedSettingsV1Checksum(const PersistedSettingsV1 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV1, checksum));
}

inline bool migratePersistedSettingsFromV1(const PersistedSettingsV1 &v1,
                                           PersistedSettings &out) {
  if (v1.magic != PERSISTED_SETTINGS_MAGIC || v1.schemaVersion != 1 ||
      v1.structureSize != sizeof(PersistedSettingsV1) ||
      v1.checksum != persistedSettingsV1Checksum(v1)) {
    return false;
  }
  out = PersistedSettings{};
  memcpy(&out, &v1, offsetof(PersistedSettingsV1, staSsid));
  // V1 had no sleep preference; keep off so upgrades do not flip behavior.
  out.staWifiSleep = false;
  memcpy(&out.staSsid, &v1.staSsid,
         offsetof(PersistedSettings, checksum) -
             offsetof(PersistedSettings, staSsid));
  out.schemaVersion = CONFIG_SCHEMA_VERSION;
  out.structureSize = sizeof(PersistedSettings);
  out.checksum = 0;
  out.checksum = persistedSettingsChecksum(out);
  return true;
}

}  // namespace shotstopper
