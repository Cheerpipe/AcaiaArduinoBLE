#pragma once

#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperPersistence.h"

namespace shotstopper {

constexpr uint32_t BLE_COMPANION_SETTINGS_MAGIC = 0x424C4543U;  // "BLEC"
constexpr uint16_t BLE_COMPANION_SETTINGS_VERSION = 1;
constexpr const char *BLE_COMPANION_SLOT_A = "bleCfgA";
constexpr const char *BLE_COMPANION_SLOT_B = "bleCfgB";

struct BleCompanionPersistedSettings {
  uint32_t magic = BLE_COMPANION_SETTINGS_MAGIC;
  uint16_t version = BLE_COMPANION_SETTINGS_VERSION;
  uint16_t structureSize = sizeof(BleCompanionPersistedSettings);
  uint32_t revision = 0;
  uint8_t enabled = 1;
  uint8_t reserved[3] = {};
  uint32_t checksum = 0;
};

inline uint32_t bleCompanionSettingsChecksum(
    const BleCompanionPersistedSettings &settings) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&settings);
  uint32_t hash = 2166136261U;
  for (size_t index = 0;
       index < offsetof(BleCompanionPersistedSettings, checksum); ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline void finalizeBleCompanionSettings(
    BleCompanionPersistedSettings &settings) {
  settings.magic = BLE_COMPANION_SETTINGS_MAGIC;
  settings.version = BLE_COMPANION_SETTINGS_VERSION;
  settings.structureSize = sizeof(BleCompanionPersistedSettings);
  settings.enabled = settings.enabled != 0 ? 1 : 0;
  settings.checksum = 0;
  settings.checksum = bleCompanionSettingsChecksum(settings);
}

inline bool validBleCompanionSettings(
    const BleCompanionPersistedSettings &settings) {
  return settings.magic == BLE_COMPANION_SETTINGS_MAGIC &&
         settings.version == BLE_COMPANION_SETTINGS_VERSION &&
         settings.structureSize == sizeof(BleCompanionPersistedSettings) &&
         settings.enabled <= 1 &&
         settings.checksum == bleCompanionSettingsChecksum(settings);
}

inline bool readBleCompanionSlot(Preferences &preferences, const char *key,
                                 BleCompanionPersistedSettings &settings) {
  if (!preferences.isKey(key) ||
      preferences.getBytesLength(key) != sizeof(settings) ||
      preferences.getBytes(key, &settings, sizeof(settings)) !=
          sizeof(settings)) {
    return false;
  }
  return validBleCompanionSettings(settings);
}

inline bool loadBleCompanionSettings(
    BleCompanionPersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    unlockSettingsNvs();
    return false;
  }
  BleCompanionPersistedSettings first;
  BleCompanionPersistedSettings second;
  const bool firstValid =
      readBleCompanionSlot(preferences, BLE_COMPANION_SLOT_A, first);
  const bool secondValid =
      readBleCompanionSlot(preferences, BLE_COMPANION_SLOT_B, second);
  preferences.end();
  const DualSlotChoice choice = chooseNewerRevision(
      firstValid, first.revision, secondValid, second.revision);
  bool loaded = false;
  if (choice == DualSlotChoice::SECOND) {
    settings = second;
    loaded = true;
  } else if (choice == DualSlotChoice::FIRST) {
    settings = first;
    loaded = true;
  }
  unlockSettingsNvs();
  return loaded;
}

inline bool saveBleCompanionSettings(
    BleCompanionPersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  BleCompanionPersistedSettings current;
  if (loadBleCompanionSettings(current)) {
    settings.revision = current.revision;
  }
  ++settings.revision;
  if (settings.revision == 0) {
    settings.revision = 1;
  }
  finalizeBleCompanionSettings(settings);
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  const char *target = (settings.revision & 1U) != 0
                           ? BLE_COMPANION_SLOT_A
                           : BLE_COMPANION_SLOT_B;
  const bool written = preferences.putBytes(target, &settings, sizeof(settings)) ==
                       sizeof(settings);
  BleCompanionPersistedSettings verified;
  const bool saved = written && readBleCompanionSlot(preferences, target, verified) &&
                     verified.revision == settings.revision;
  preferences.end();
  unlockSettingsNvs();
  return saved;
}

inline bool resetBleCompanionSettings(
    BleCompanionPersistedSettings &settings) {
  settings = BleCompanionPersistedSettings{};
  return saveBleCompanionSettings(settings);
}

}  // namespace shotstopper
