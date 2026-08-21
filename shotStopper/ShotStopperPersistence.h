#pragma once

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperPersistedNetwork.h"
#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPreferences.h"
#include "ShotStopperSettingsMigrate.h"

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

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
  return true;
}

inline void finalizePersistedSettings(PersistedSettings &settings) {
  ensurePersistedPresetBank(settings);
  if (scaleMacCacheModeRequiresPreferred(settings.runtime.scaleMacCacheMode) &&
      settings.preferredScaleMac[0] == '\0') {
    settings.runtime.scaleMacCacheMode =
        static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  }
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

// Dual-slot scratch shared with shot-log flash I/O (see FlashIoScratch).
// Allocated from internal SRAM (heap, not BSS): source/destination of
// Preferences putBytes/getBytes while flash cache is disabled.
inline PersistedSettings &persistedSettingsScratch(uint8_t index) {
  static_assert(2 * sizeof(PersistedSettings) <= FLASH_IO_SCRATCH_BYTES,
                "PersistedSettings dual-slot scratch exceeds flash I/O buffer");
  auto *slots =
      reinterpret_cast<PersistedSettings *>(flashIoScratchBytes());
  return slots[index & 1U];
}

inline bool readSettingsSlot(Preferences &preferences, const char *key,
                             PersistedSettings &settings) {
  if (!preferences.isKey(key) ||
      preferences.getBytesLength(key) != sizeof(PersistedSettings)) {
    return false;
  }
  // Read directly into the out-param to avoid borrowing scratch slots that
  // load/save may already hold (candidate lives in scratch[1] during save).
  if (preferences.getBytes(key, &settings, sizeof(settings)) !=
      sizeof(settings)) {
    return false;
  }
  if (!validPersistedSettings(settings)) {
    return false;
  }
  return true;
}

inline bool lockSettingsNvs() { return lockFlashIo(); }
inline void unlockSettingsNvs() { unlockFlashIo(); }
inline void yieldSettingsNvs() { yieldFlashIo(); }
inline void feedSettingsNvsWatchdog() { feedFlashIoWatchdog(); }

inline bool savePersistedSettings(PersistedSettings &settings);

inline bool loadPersistedSettings(PersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    unlockSettingsNvs();
    return false;
  }
  PersistedSettings &first = persistedSettingsScratch(0);
  PersistedSettings &second = persistedSettingsScratch(1);
  first = PersistedSettings{};
  second = PersistedSettings{};
  bool firstValid = readSettingsSlot(preferences, SETTINGS_SLOT_A, first);
  bool secondValid = readSettingsSlot(preferences, SETTINGS_SLOT_B, second);
  preferences.end();

  const DualSlotChoice choice = chooseNewerRevision(
      firstValid, first.storageRevision, secondValid, second.storageRevision);
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
  if (!lockSettingsNvs()) {
    return;
  }
  durableStorageRevision() = revision;
  durableStorageRevisionValid() = revision != 0;
  unlockSettingsNvs();
}

inline void resetDurableStorageRevision() {
  if (!lockSettingsNvs()) {
    return;
  }
  durableStorageRevision() = 0;
  durableStorageRevisionValid() = false;
  unlockSettingsNvs();
}

inline bool savePersistedSettings(PersistedSettings &settings) {
  // Slots 0 and 1 only: candidate in [1], scratch [0] for revision probe /
  // verify. Never call loadPersistedSettings here — it needs both slots.
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  if (!lockSettingsNvs()) {
    feedSettingsNvsWatchdog();
    return false;
  }
  PersistedSettings &candidate = persistedSettingsScratch(1);
  PersistedSettings &scratch = persistedSettingsScratch(0);
  candidate = settings;
  if (durableStorageRevisionValid()) {
    candidate.storageRevision = durableStorageRevision();
  } else if (candidate.storageRevision == 0) {
    uint32_t revision = 0;
    bool haveRevision = false;
    Preferences probe;
    if (probe.begin(SETTINGS_NAMESPACE, true)) {
      scratch = PersistedSettings{};
      if (readSettingsSlot(probe, SETTINGS_SLOT_A, scratch)) {
        revision = scratch.storageRevision;
        haveRevision = true;
      }
      scratch = PersistedSettings{};
      if (readSettingsSlot(probe, SETTINGS_SLOT_B, scratch)) {
        if (!haveRevision ||
            secondRevisionIsNewer(revision, scratch.storageRevision)) {
          revision = scratch.storageRevision;
        }
        haveRevision = true;
      }
      probe.end();
    }
    if (haveRevision) {
      candidate.storageRevision = revision;
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
  scratch = PersistedSettings{};
  const bool saved = written &&
                     readSettingsSlot(preferences, target, scratch) &&
                     scratch.storageRevision == candidate.storageRevision &&
                     memcmp(&scratch, &candidate, sizeof(candidate)) == 0;
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
  if (!initializeDefaultAccessPointPassword(settings)) {
    return false;
  }
  finalizePersistedSettings(settings);
  return true;
}

inline bool resetPersistedSettingsToFactory(PersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  PersistedSettings &first = persistedSettingsScratch(0);
  first = PersistedSettings{};
  if (!initializeDefaultSettings(first)) {
    unlockSettingsNvs();
    return false;
  }
  first.storageRevision = 1;
  finalizePersistedSettings(first);
  PersistedSettings &second = persistedSettingsScratch(1);
  second = first;
  second.storageRevision = 2;
  finalizePersistedSettings(second);

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
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  const bool firstSaved =
      preferences.putBytes(SETTINGS_SLOT_A, &first, sizeof(first)) ==
      sizeof(first);
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  const bool secondSaved =
      preferences.putBytes(SETTINGS_SLOT_B, &second, sizeof(second)) ==
      sizeof(second);
  // Reuse the two slots for read-back verify (NVS already holds both blobs).
  first = PersistedSettings{};
  const bool firstVerified =
      firstSaved && readSettingsSlot(preferences, SETTINGS_SLOT_A, first);
  second = PersistedSettings{};
  const bool secondVerified =
      secondSaved && readSettingsSlot(preferences, SETTINGS_SLOT_B, second);
  preferences.end();
  unlockSettingsNvs();

  if (!firstVerified && !secondVerified) {
    return false;
  }
  settings = secondVerified ? second : first;
  noteDurableStorageRevision(settings.storageRevision);
  return true;
}

}  // namespace shotstopper
