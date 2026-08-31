#pragma once

// Settings schema migrations.
//
// Current on-disk schema is CONFIG_SCHEMA_VERSION (V3). V3 adds the fixed-size
// Bullseye melody record. V1/V2 are both 1912 bytes; V1 has unnamed padding
// after staOpen while V2 names that byte staWifiSleep. Unrecognized blobs are
// rejected and factory defaults are used instead.
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

struct PersistedSettingsV2 {
  uint32_t magic = PERSISTED_SETTINGS_MAGIC;
  uint32_t schemaVersion = 2;
  uint32_t structureSize = 0;
  uint32_t storageRevision = 0;
  RuntimeConfig runtime = {};
  ShotPresetBank presets = {};
  bool staConfigured = false;
  bool staOpen = false;
  bool staWifiSleep = true;
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
static_assert(sizeof(PersistedSettingsV2) == 1912, "V2 settings blob size");
static_assert(offsetof(PersistedSettingsV1, staOpen) ==
                  offsetof(PersistedSettingsV2, staOpen),
              "V1/V2 header through staOpen must match");
static_assert(offsetof(PersistedSettingsV2, staWifiSleep) ==
                  offsetof(PersistedSettingsV2, staOpen) + sizeof(bool),
              "staWifiSleep must sit immediately after staOpen");
static_assert(offsetof(PersistedSettingsV1, staSsid) + sizeof(bool) ==
                  offsetof(PersistedSettingsV2, staSsid),
              "V2 inserts staWifiSleep immediately before staSsid");

inline uint32_t persistedSettingsV1Checksum(const PersistedSettingsV1 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV1, checksum));
}

inline uint32_t persistedSettingsV2Checksum(const PersistedSettingsV2 &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettingsV2, checksum));
}

inline bool migratePersistedSettingsFromV1(const PersistedSettingsV1 &v1,
                                           PersistedSettings &out) {
  if (v1.magic != PERSISTED_SETTINGS_MAGIC || v1.schemaVersion != 1 ||
      v1.structureSize != sizeof(PersistedSettingsV1) ||
      v1.checksum != persistedSettingsV1Checksum(v1)) {
    return false;
  }
  out = PersistedSettings{};
  out.storageRevision = v1.storageRevision;
  out.runtime = v1.runtime;
  memcpy(&out.presets, &v1.presets,
         offsetof(PersistedSettingsV1, staSsid) -
             offsetof(PersistedSettingsV1, presets));
  // V1 had no sleep preference; keep off so upgrades do not flip behavior.
  out.staWifiSleep = false;
  memcpy(&out.staSsid, &v1.staSsid,
         offsetof(PersistedSettingsV1, checksum) -
             offsetof(PersistedSettingsV1, staSsid));
  out.schemaVersion = CONFIG_SCHEMA_VERSION;
  out.structureSize = sizeof(PersistedSettings);
  out.checksum = 0;
  out.checksum = persistedSettingsChecksum(out);
  return true;
}

inline bool migratePersistedSettingsFromV2(const PersistedSettingsV2 &v2,
                                           PersistedSettings &out) {
  if (v2.magic != PERSISTED_SETTINGS_MAGIC || v2.schemaVersion != 2 ||
      v2.structureSize != sizeof(PersistedSettingsV2) ||
      v2.checksum != persistedSettingsV2Checksum(v2)) {
    return false;
  }
  out = PersistedSettings{};
  out.storageRevision = v2.storageRevision;
  out.runtime = v2.runtime;
  memcpy(&out.presets, &v2.presets,
         offsetof(PersistedSettingsV2, checksum) -
             offsetof(PersistedSettingsV2, presets));
  out.schemaVersion = CONFIG_SCHEMA_VERSION;
  out.structureSize = sizeof(PersistedSettings);
  out.checksum = 0;
  out.checksum = persistedSettingsChecksum(out);
  return true;
}

}  // namespace shotstopper
