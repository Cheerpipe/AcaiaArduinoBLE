#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"
#include "../ShotStopperBleCompanionPersistence.h"
#include "../ShotStopperRecovery.h"
#include "../ShotStopperRecoveryGesture.h"
#include "../ShotStopperShotLog.h"
#include "../ShotStopperLastShot.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace shotstopper;

namespace {

int failures = 0;
int testsRun = 0;

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: "          \
                << #expression << '\n';                                       \
      ++failures;                                                             \
    }                                                                         \
  } while (false)

void resetHostPersistence() {
  persistence_host::reset();
  resetDurableStorageRevision();
}

void p01_defaults_are_valid() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.staIpMode == static_cast<uint8_t>(StaIpMode::DHCP));
  CHECK(settings.staConfigState ==
        static_cast<uint8_t>(StaConfigState::CONFIRMED));
  CHECK(!settings.lkgValid);
  CHECK(settings.runtime.fastExtractionGuardEnabled);
  CHECK(std::fabs(settings.runtime.maxRecoveryWeightG -
                  DEFAULT_MAX_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(settings.runtime.minBrewTimeMs == DEFAULT_MIN_BREW_TIME_MS);
  CHECK(settings.runtime.slowExtractionGuardEnabled);
  CHECK(std::fabs(settings.runtime.minRecoveryWeightG -
                  DEFAULT_MIN_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(settings.runtime.maxBrewTimeMs == DEFAULT_MAX_BREW_TIME_MS);
  CHECK(settings.runtime.autoToManualGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardLimitMode ==
        static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO));
  CHECK(settings.runtime.autoToManualGuardManualLimitMs ==
        DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS);
  CHECK(settings.runtime.autoToManualGuardBaselineMs ==
        DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS);
  CHECK(std::fabs(settings.runtime.weightOffsetBaselineG -
                  DEFAULT_WEIGHT_OFFSET_G) < 0.001f);
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(settings.runtime.autoToManualGuardSamplesDs[i] ==
          AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS);
  }
  CHECK(validPersistedSettings(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
  CHECK(settings.runtime.paddleMode ==
        static_cast<uint8_t>(PaddleMode::NATURAL));
  CHECK(settings.runtime.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(!settings.runtime.soundAlertsMuted);
  CHECK(DEFAULT_ALERT_OUTPUT_CHANNEL ==
        (BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY
                                : AlertOutputChannel::SCALE_ONLY));
  CHECK(settings.runtime.bookooMuteOnBuzzerOnly);
  CHECK(settings.runtime.bookooConnectBeepLevel ==
        DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(settings.runtime.buzzerExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(settings.runtime.buzzerSlowExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(settings.runtime.avoidBbwShotWithoutScale);
  CHECK(settings.runtime.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
  CHECK(settings.runtime.dripDelayMs == DEFAULT_DRIP_DELAY_MS);
  CHECK(!settings.runtime.serialDebugOutput);
  CHECK(settings.runtime.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::NONE));
  CHECK(settings.runtime.buzzerScaleConnectedBeep);
  CHECK(validPreferredScaleMac(settings.preferredScaleMac));
  CHECK(validPreferredScaleName(settings.preferredScaleName));
  CHECK(validPreferredScaleName("Pearl-S"));
  CHECK(!validPreferredScaleName("bad\"name"));
  CHECK(validPreferredScaleMac("AA:BB:CC:DD:EE:FF"));
  CHECK(validPreferredScaleMac("aa:bb:cc:dd:ee:ff"));
  CHECK(!validPreferredScaleMac("AA:BB:CC:DD:EE"));
  CHECK(!validPreferredScaleMac("GG:BB:CC:DD:EE:FF"));
}

void p02_newest_valid_slot_is_loaded() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(savePersistedSettings(settings));
  const uint32_t firstRevision = settings.storageRevision;
  settings.runtime.goalWeightG = 47;
  settings.runtime.maxRecoveryWeightG = 55.0f;
  settings.runtime.soundAlertsMuted = true;
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision == firstRevision + 1);

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 47);
  CHECK(loaded.runtime.soundAlertsMuted);
}

void p02b_save_uses_ram_revision_when_slots_unreadable() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision != 0);
  persistence_host::records.clear();
  settings.runtime.goalWeightG = 41;
  settings.runtime.maxRecoveryWeightG = 50.0f;
  CHECK(savePersistedSettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 41);
  CHECK(loaded.storageRevision == settings.storageRevision);
}

void p02c_overlay_live_runtime_is_saved_not_stale_blob() {
  resetHostPersistence();
  PersistedSettings stale;
  CHECK(initializeDefaultSettings(stale));
  stale.runtime.goalWeightG = 36;
  CHECK(savePersistedSettings(stale));

  RuntimeConfig live = stale.runtime;
  live.goalWeightG = 44;
  live.maxRecoveryWeightG = 53.0f;
  ShotPresetBank livePresets = stale.presets;
  overlayLivePersistedSettings(stale, live, livePresets);
  CHECK(stale.runtime.goalWeightG == 44);
  CHECK(savePersistedSettings(stale));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 44);
  CHECK(loaded.storageRevision == stale.storageRevision);
}

void p03_corrupt_newest_slot_falls_back() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 40;
  CHECK(savePersistedSettings(settings));
  settings.runtime.goalWeightG = 48;
  settings.runtime.maxRecoveryWeightG = 55.0f;
  CHECK(savePersistedSettings(settings));
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, SETTINGS_SLOT_B,
                                  offsetof(PersistedSettings, runtime)));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 40);
}

void p04_crc_and_semantic_validation_reject_corruption() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 50;
  settings.runtime.maxRecoveryWeightG = 58.0f;
  CHECK(!validPersistedSettings(settings));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
}

void p05_password_change_updates_hash() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(!setAccessPointPassword(settings, DEFAULT_AP_PASSWORD));
  CHECK(setAccessPointPassword(settings, "NuevaClaveSegura"));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(strcmp(settings.apPassword, "NuevaClaveSegura") == 0);
  CHECK(!passwordIsFactoryDefault(settings));
}

void p18_shot_log_keeps_history_when_inactive_slot_write_fails() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  record.actualWeightCg = 3600;
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  persistence_host::failNextWrite = true;
  ShotLogRecord second = {};
  second.durationDs = 260;
  second.goalWeightG = 36;
  CHECK(!log.append(second));
  CHECK(log.count() == 1);

  ShotLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 1);
}

void p19_shot_log_weight_sentinel_allows_int16_max() {
  CHECK(shotLogWeightToCentigrams(327.67f) == INT16_MAX);
  CHECK(shotLogWeightIsMissing(SHOT_LOG_WEIGHT_MISSING));
  CHECK(shotLogWeightIsMissing(SHOT_LOG_WEIGHT_MISSING_LEGACY));
  CHECK(!shotLogWeightIsMissing(3600));
  CHECK(shotLogWeightToCentigrams(400.0f) == SHOT_LOG_WEIGHT_MISSING);
}

void p29_last_shot_persists_and_clears() {
  resetHostPersistence();
  LastShotStore store;
  CHECK(store.load());
  CHECK(!store.get().valid);

  PersistedLastShot shot = {};
  shot.valid = true;
  shot.cycleId = 42;
  shot.durationMs = 28500;
  shot.goalWeightG = 36;
  shot.weightValid = true;
  shot.currentWeightG = 36.2f;
  shot.shotType = static_cast<uint8_t>(LastShotType::AUTO);
  shot.noScaleShotGuardEnabled = true;
  shot.noScaleShotGuardArmed = true;
  strcpy(shot.scaleProtocol, "acaia");
  CHECK(store.persist(shot));
  CHECK(persistence_host::records.count("lastshot/record") == 1);

  LastShotStore reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.get().valid);
  CHECK(reloaded.get().cycleId == 42);
  CHECK(reloaded.get().goalWeightG == 36);
  CHECK(fabs(reloaded.get().currentWeightG - 36.2f) < 0.001f);
  CHECK(reloaded.get().noScaleShotGuardEnabled);
  CHECK(reloaded.get().noScaleShotGuardArmed);
  CHECK(strcmp(reloaded.get().scaleProtocol, "acaia") == 0);

  CHECK(reloaded.clear());
  LastShotStore emptied;
  CHECK(emptied.load());
  CHECK(!emptied.get().valid);
}

void p07_invalid_schema_uses_factory_on_missing_slots() {
  resetHostPersistence();
  PersistedSettings loaded;
  CHECK(!loadPersistedSettings(loaded));
}

void p08_factory_reset_rebuilds_defaults() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 63;
  settings.runtime.maxRecoveryWeightG = 70.0f;
  strcpy(settings.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  strcpy(settings.preferredScaleName, "Lunar");
  settings.runtime.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(settings.runtime.fastExtractionGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardEnabled);
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
}

void p09_fast_extraction_guard_validation() {
  RuntimeConfig config = {};
  config.goalWeightG = 36;
  config.maxRecoveryWeightG = 42.5f;
  config.minBrewTimeMs = 26000;
  config.fastExtractionGuardEnabled = true;
  config.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  config.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.maxRecoveryWeightG = 36.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION);

  config = {};
  config.goalWeightG = 36;
  config.minRecoveryWeightG = 30.0f;
  config.maxBrewTimeMs = 44000;
  config.slowExtractionGuardEnabled = true;
  config.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  config.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.minRecoveryWeightG = 36.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION);
  config.minRecoveryWeightG = 30.0f;
  config.fastExtractionGuardEnabled = true;
  config.maxRecoveryWeightG = 42.5f;
  config.minBrewTimeMs = 28000;
  config.maxBrewTimeMs = 28000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION);
}

void p10_auto_to_manual_guard_trend_and_validation() {
  uint16_t samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {300, 300, 300, 300,
                                                         300};
  CHECK(autoToManualGuardTrendMs(samples, 60000) == 30000);
  samples[0] = 200;
  samples[1] = 220;
  samples[2] = 240;
  samples[3] = 260;
  samples[4] = 280;
  const uint32_t rising = autoToManualGuardTrendMs(samples, 60000);
  CHECK(rising == 30000);

  RuntimeConfig config = {};
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.autoToManualGuardManualLimitMs = 5000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT);
  config.autoToManualGuardManualLimitMs = 30000;
  config.autoToManualGuardBaselineMs = 5000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_BASELINE);
  config.autoToManualGuardBaselineMs = 30000;
  config.autoToManualGuardLimitMode = 9;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE);
  config.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  config.weightOffsetBaselineG = -0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::WEIGHT_OFFSET_BASELINE);
  config.weightOffsetBaselineG = MAX_OFFSET_G + 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::WEIGHT_OFFSET_BASELINE);
  config.weightOffsetBaselineG = 2.25f;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);

  uint16_t seeded[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {};
  resetAutoToManualGuardSamples(seeded, 28000);
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(seeded[i] == 280);
  }
}

void p12_shot_log_persists_compact_blob() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  record.actualWeightCg = 3600;
  record.actualWeightSource =
      static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  const auto foundA = persistence_host::records.find("shotlog/recordsA");
  const auto foundB = persistence_host::records.find("shotlog/recordsB");
  CHECK(foundA != persistence_host::records.end() ||
        foundB != persistence_host::records.end());
  const auto &blob =
      foundA != persistence_host::records.end() ? foundA->second : foundB->second;
  CHECK(blob.size() == sizeof(ShotLogHeader) + sizeof(ShotLogRecord));
  CHECK(persistence_host::records.count("shotlog/active") == 1);

  ShotLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 1);
  ShotLogRecord out[1] = {};
  CHECK(reloaded.copyNewestFirst(out, 1) == 1);
  CHECK(out[0].goalWeightG == 36);
  CHECK(out[0].actualWeightSource ==
        static_cast<uint8_t>(ActualWeightSource::POST_DRIP));
  CHECK(shotLogPackGuardFlags(true, true) ==
        (SHOT_LOG_FAST_GUARD_BIT | SHOT_LOG_SLOW_GUARD_BIT));
  CHECK(shotLogSlowGuardEnabled(shotLogPackGuardFlags(false, true)));
  CHECK(shotLogSlowExtended(shotLogPackExtendedFlags(false, true)));
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::SLOW_MAX_TIME),
               "slow_max_time") == 0);
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::SLOW_MIN_WEIGHT),
               "slow_min_weight") == 0);
}

void p47_rejects_non_current_schema_blob() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  finalizePersistedSettings(settings);
  settings.schemaVersion = 31;
  settings.checksum = 0;
  settings.checksum = persistedSettingsChecksum(settings);
  // Wrong schemaVersion must fail validPersistedSettings even with matching CRC.
  CHECK(!validPersistedSettings(settings));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &settings,
                           sizeof(settings));
  PersistedSettings loaded;
  CHECK(!loadPersistedSettings(loaded));
}

void p46_ring_retain_log_level_persists_round_trip() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.runtime.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::NONE));
  settings.runtime.ringRetainLogLevel = static_cast<uint8_t>(LogLevel::INFO);
  CHECK(savePersistedSettings(settings));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::INFO));
}

void p43_scale_history_upsert_and_lru() {
  ScaleHistoryEntry entries[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:01", "One"));
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:02", "Two"));
  CHECK(!upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:01", "One"));
  // Case-insensitive: lowercase must not create a duplicate slot.
  CHECK(!upsertScaleHistory(entries, seq, "aa:bb:cc:dd:ee:01", "One"));
  CHECK(scaleHistoryOccupiedCount(entries) == 2);
  CHECK(strcmp(entries[0].mac, "AA:BB:CC:DD:EE:01") == 0);
  for (uint8_t i = 3; i <= 9; ++i) {
    char mac[PREFERRED_SCALE_MAC_CAPACITY];
    snprintf(mac, sizeof(mac), "AA:BB:CC:DD:EE:%02X", i);
    CHECK(upsertScaleHistory(entries, seq, mac, "X"));
  }
  CHECK(scaleHistoryOccupiedCount(entries) == SCALE_HISTORY_CAPACITY);
  bool foundOne = false;
  bool foundTwo = false;
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (strcmp(entries[i].mac, "AA:BB:CC:DD:EE:01") == 0) {
      foundOne = true;
    }
    if (strcmp(entries[i].mac, "AA:BB:CC:DD:EE:02") == 0) {
      foundTwo = true;
    }
  }
  // Refreshing :01 made it newest; :02 is the LRU victim when the table fills.
  CHECK(foundOne);
  CHECK(!foundTwo);
}

void p44_scale_history_canonicalizes_mac_case() {
  ScaleHistoryEntry entries[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  CHECK(upsertScaleHistory(entries, seq, "aa:bb:cc:dd:ee:10", "Pearl"));
  CHECK(strcmp(entries[0].mac, "AA:BB:CC:DD:EE:10") == 0);
  CHECK(preferredScaleMacEqual(entries[0].mac, "Aa:Bb:Cc:Dd:Ee:10"));
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  CHECK(findScaleHistoryName(entries, "AA:BB:CC:DD:EE:10", name, sizeof(name)));
  CHECK(strcmp(name, "Pearl") == 0);
  CHECK(findScaleHistoryName(entries, "aa:bb:cc:dd:ee:10", name, sizeof(name)));
  CHECK(strcmp(name, "Pearl") == 0);
}

void p24_preset_bank_size_and_crud_budgets() {
  CHECK(sizeof(ShotPreset) <= 128);
  CHECK(sizeof(ShotPresetBank) <= 1100);
  CHECK(sizeof(PersistedSettings) <= PERSISTED_SETTINGS_NVS_BUDGET);
  CHECK(sizeof(WebCommand) <= 512);
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  CHECK(bank.count == 2);
  CHECK(bank.activeId == FACTORY_PRESET_ID_DOUBLE);
  CHECK(bank.presets[0].id == FACTORY_PRESET_ID_DOUBLE);
  CHECK(bank.presets[1].id == FACTORY_PRESET_ID_SINGLE);
  CHECK(bank.presets[0].slowExtractionGuardEnabled);
  CHECK(std::fabs(bank.presets[0].minRecoveryWeightG -
                  DEFAULT_MIN_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(bank.presets[0].maxBrewTimeMs == DEFAULT_MAX_BREW_TIME_MS);
  CHECK(bank.presets[1].fastExtractionGuardEnabled);
  CHECK(bank.presets[1].slowExtractionGuardEnabled);
  CHECK(bank.presets[1].minBrewTimeMs == FACTORY_SINGLE_MIN_BREW_TIME_MS);
  CHECK(std::fabs(bank.presets[1].minRecoveryWeightG -
                  FACTORY_SINGLE_MIN_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(bank.presets[1].maxBrewTimeMs == FACTORY_SINGLE_MAX_BREW_TIME_MS);

  ShotPresetBank resetBank = bank;
  ShotPreset *single = mutableShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE);
  CHECK(single != nullptr);
  single->fastExtractionGuardEnabled = false;
  single->slowExtractionGuardEnabled = false;
  CHECK(restoreFactoryShotPresetValues(resetBank, FACTORY_PRESET_ID_SINGLE));
  CHECK(findShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE)->fastExtractionGuardEnabled);
  CHECK(findShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE)->slowExtractionGuardEnabled);

  // Legacy Single-then-Double banks reorder on ensure.
  ShotPresetBank legacy;
  memset(&legacy, 0, sizeof(legacy));
  legacy.count = 2;
  legacy.activeId = FACTORY_PRESET_ID_DOUBLE;
  legacy.nextId = 3;
  fillFactorySinglePreset(legacy.presets[0]);
  fillFactoryDoublePreset(legacy.presets[1]);
  ensureShotPresetBank(legacy, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(legacy.presets[0].id == FACTORY_PRESET_ID_DOUBLE);
  CHECK(legacy.presets[1].id == FACTORY_PRESET_ID_SINGLE);

  CHECK(!deleteShotPreset(bank, FACTORY_PRESET_ID_DOUBLE));
  CHECK(!deleteShotPreset(bank, FACTORY_PRESET_ID_SINGLE));
  CHECK(bank.count == 2);

  ShotPreset *dbl = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE);
  CHECK(dbl != nullptr);
  dbl->goalWeightG = 40;
  CHECK(restoreFactoryShotPresetValues(bank, FACTORY_PRESET_ID_DOUBLE));
  CHECK(findShotPreset(bank, FACTORY_PRESET_ID_DOUBLE)->goalWeightG ==
        DEFAULT_GOAL_WEIGHT_G);

  uint8_t newId = 0;
  CHECK(createUntitledShotPreset(bank, newId));
  CHECK(newId != 0);
  CHECK(bank.activeId == newId);
  const ShotPreset *created = findShotPreset(bank, newId);
  CHECK(created != nullptr);
  CHECK(created->goalWeightG == 36);
  CHECK(std::fabs(created->weightOffsetBaselineG - 1.5f) < 0.001f);
  CHECK(std::fabs(created->weightOffsetG - 1.5f) < 0.001f);

  uint8_t copyId = 0;
  CHECK(duplicateShotPreset(bank, FACTORY_PRESET_ID_DOUBLE, copyId));
  const ShotPreset *copy = findShotPreset(bank, copyId);
  CHECK(copy != nullptr);
  CHECK(strcmp(copy->name, "Double copy") == 0);
  CHECK(!copy->isFactory);

  uint8_t copy2 = 0;
  CHECK(duplicateShotPreset(bank, FACTORY_PRESET_ID_DOUBLE, copy2));
  const ShotPreset *copyB = findShotPreset(bank, copy2);
  CHECK(copyB != nullptr);
  CHECK(strcmp(copyB->name, "Double copy 2") == 0);

  CHECK(renameShotPreset(bank, copyId, "Double light"));
  CHECK(strcmp(findShotPreset(bank, copyId)->name, "Double light") == 0);
  CHECK(!renameShotPreset(bank, copy2, "Double light"));

  RuntimeConfig machine = {};
  machine.retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  machine.autoRetare = true;
  machine.timerOnly = true;  // session Manual
  RuntimeConfig composed = composeEffectiveConfig(machine, bank);
  CHECK(composed.timerOnly);  // Manual preserved
  CHECK(composed.goalWeightG == activeShotPreset(bank).goalWeightG);

  while (bank.count < MAX_SHOT_PRESETS) {
    uint8_t id = 0;
    CHECK(createUntitledShotPreset(bank, id));
  }
  uint8_t overflow = 0;
  CHECK(!createUntitledShotPreset(bank, overflow));
  CHECK(!duplicateShotPreset(bank, FACTORY_PRESET_ID_SINGLE, overflow));
}


void p25_invalid_active_id_keeps_customs() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == 3);
  bank.activeId = 99;  // missing
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == 3);
  CHECK(findShotPresetIndex(bank, customId) >= 0);
  CHECK(findShotPresetIndex(bank, bank.activeId) >= 0);

  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(createUntitledShotPreset(settings.presets, customId));
  settings.presets.activeId = 99;
  settings.runtime.timerOnly = true;
  ensurePersistedPresetBank(settings);
  CHECK(settings.presets.count >= 3);
  CHECK(findShotPresetIndex(settings.presets, customId) >= 0);
  const ShotPreset *dbl =
      findShotPreset(settings.presets, FACTORY_PRESET_ID_DOUBLE);
  CHECK(dbl != nullptr);
  CHECK(dbl->brewByWeight);  // must not inherit session Manual
}

void p26_save_candidate_validation_does_not_require_live_mutation() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  ShotPreset *preset = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE);
  CHECK(preset != nullptr);
  const uint8_t originalGoal = preset->goalWeightG;
  ShotPreset candidate = *preset;
  candidate.goalWeightG = 5;  // invalid
  CHECK(!validateShotPresetRecipe(candidate, DEFAULT_RETARE_WINDOW_MS, true));
  CHECK(preset->goalWeightG == originalGoal);
}

void p35_invalid_fast_extraction_recipe_keeps_custom() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == 3);
  ShotPreset *custom = mutableShotPreset(bank, customId);
  CHECK(custom != nullptr);
  custom->fastExtractionGuardEnabled = true;
  custom->goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  custom->maxRecoveryWeightG = 30.0f;
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == 3);
  const ShotPreset *kept = findShotPreset(bank, customId);
  CHECK(kept != nullptr);
  CHECK(!kept->fastExtractionGuardEnabled ||
        kept->maxRecoveryWeightG > static_cast<float>(kept->goalWeightG));
}

void p38_invalid_slow_extraction_recipe_keeps_custom() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == 3);
  ShotPreset *custom = mutableShotPreset(bank, customId);
  CHECK(custom != nullptr);
  custom->slowExtractionGuardEnabled = true;
  custom->goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  custom->minRecoveryWeightG = 36.0f;
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == 3);
  const ShotPreset *kept = findShotPreset(bank, customId);
  CHECK(kept != nullptr);
  CHECK(!kept->slowExtractionGuardEnabled ||
        kept->minRecoveryWeightG < static_cast<float>(kept->goalWeightG));
}

void p16_static_ip_address_validation() {
  uint8_t ip[4] = {192, 168, 1, 50};
  uint8_t mask[4] = {255, 255, 255, 0};
  uint8_t gateway[4] = {192, 168, 1, 1};
  uint8_t dns1[4] = {1, 1, 1, 1};
  uint8_t dns2[4] = {0, 0, 0, 0};
  CHECK(validStaAddressConfig(static_cast<uint8_t>(StaIpMode::STATIC), ip, mask,
                              gateway, dns1, dns2));
  uint8_t softAp[4] = {192, 168, 4, 10};
  CHECK(!validStaAddressConfig(static_cast<uint8_t>(StaIpMode::STATIC), softAp,
                               mask, gateway, dns1, dns2));
  uint8_t zero[4] = {0, 0, 0, 0};
  CHECK(validStaAddressConfig(static_cast<uint8_t>(StaIpMode::DHCP), zero, zero,
                              zero, zero, zero));
  CHECK(!validStaAddressConfig(static_cast<uint8_t>(StaIpMode::DHCP), ip, mask,
                               gateway, dns1, dns2));

  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.staConfigured = true;
  strcpy(settings.staSsid, "CafeLAN");
  strcpy(settings.staPassword, "CafePass1");
  settings.staIpMode = static_cast<uint8_t>(StaIpMode::STATIC);
  memcpy(settings.staIp, ip, 4);
  memcpy(settings.staNetmask, mask, 4);
  memcpy(settings.staGateway, gateway, 4);
  memcpy(settings.staDns1, dns1, 4);
  settings.staConfigState = static_cast<uint8_t>(StaConfigState::PENDING);
  copyActiveStaToLkg(settings);
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(restoreLkgToActive(settings));
  CHECK(settings.staConfigState ==
        static_cast<uint8_t>(StaConfigState::CONFIRMED));
}

void p48_ble_companion_defaults_and_dual_slot_round_trip() {
  resetHostPersistence();
  BleCompanionPersistedSettings settings;
  CHECK(settings.enabled == 1);
  CHECK(saveBleCompanionSettings(settings));
  CHECK(settings.revision == 1);
  settings.enabled = 0;
  CHECK(saveBleCompanionSettings(settings));
  CHECK(settings.revision == 2);
  BleCompanionPersistedSettings loaded;
  CHECK(loadBleCompanionSettings(loaded));
  CHECK(loaded.enabled == 0);
  CHECK(loaded.revision == 2);
  CHECK(validBleCompanionSettings(loaded));
}

void p49_ble_companion_corruption_falls_back_and_reset_enables() {
  resetHostPersistence();
  BleCompanionPersistedSettings settings;
  CHECK(saveBleCompanionSettings(settings));
  settings.enabled = 0;
  CHECK(saveBleCompanionSettings(settings));
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, BLE_COMPANION_SLOT_B,
                                  offsetof(BleCompanionPersistedSettings,
                                           checksum)));
  BleCompanionPersistedSettings loaded;
  CHECK(loadBleCompanionSettings(loaded));
  CHECK(loaded.enabled == 1);
  CHECK(loaded.revision == 1);
  CHECK(resetBleCompanionSettings(loaded));
  CHECK(loaded.enabled == 1);
}

RecoveryGestureResult recoveryEdge(RecoveryGestureRecognizer &recognizer,
                                   uint32_t atMs, bool paddleOn) {
  return recognizer.update(atMs, paddleOn, paddleOn, !paddleOn);
}

void p50_recovery_three_cycles_confirm_network_reset() {
  CHECK(recoveryGestureEntryAllowed(true, true));
  CHECK(!recoveryGestureEntryAllowed(false, true));
  CHECK(!recoveryGestureEntryAllowed(true, false));
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  CHECK(recoveryEdge(recognizer, 100, false) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 200, true) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 300, false) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 400, true) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 500, false) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 600, true) == RecoveryGestureResult::NONE);
  CHECK(recognizer.update(3599, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(recognizer.update(3600, true, false, false) ==
        RecoveryGestureResult::NETWORK_ACCESS_RESET);
}

void p51_recovery_five_cycles_upgrade_factory_candidate() {
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  for (uint32_t cycle = 0; cycle < 5; ++cycle) {
    CHECK(recoveryEdge(recognizer, 100 + cycle * 200, false) ==
          RecoveryGestureResult::NONE);
    CHECK(recoveryEdge(recognizer, 200 + cycle * 200, true) ==
          RecoveryGestureResult::NONE);
  }
  CHECK(recognizer.completedCycles == 5);
  CHECK(recognizer.update(3999, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(recognizer.update(4000, true, false, false) ==
        RecoveryGestureResult::FACTORY_RESET);
}

void p52_recovery_rejects_four_slow_and_late_confirmation() {
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    (void)recoveryEdge(recognizer, 100 + cycle * 200, false);
    (void)recoveryEdge(recognizer, 200 + cycle * 200, true);
  }
  CHECK(recognizer.update(5101, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(!recognizer.attemptActive);

  recognizer.begin(0);
  for (uint32_t cycle = 0; cycle < 6; ++cycle) {
    (void)recoveryEdge(recognizer, 100 + cycle * 200, false);
    (void)recoveryEdge(recognizer, 200 + cycle * 200, true);
  }
  CHECK(!recognizer.attemptActive);
  CHECK(recognizer.update(5000, true, false, false) ==
        RecoveryGestureResult::NONE);

  (void)recoveryEdge(recognizer, 10000, false);
  (void)recoveryEdge(recognizer, 16000, true);
  CHECK(recognizer.completedCycles == 0);
  CHECK(!recognizer.attemptActive);

  recognizer.begin(0);
  (void)recoveryEdge(recognizer, 58000, false);
  (void)recoveryEdge(recognizer, 58200, true);
  (void)recoveryEdge(recognizer, 58400, false);
  (void)recoveryEdge(recognizer, 58600, true);
  (void)recoveryEdge(recognizer, 58800, false);
  (void)recoveryEdge(recognizer, 59000, true);
  CHECK(recognizer.update(59999, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(recognizer.update(60000, true, false, false) ==
        RecoveryGestureResult::TIMED_OUT);
}

void p53_recovery_boundaries_and_millis_wraparound() {
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  (void)recoveryEdge(recognizer, 0, false);
  (void)recoveryEdge(recognizer, 1000, true);
  (void)recoveryEdge(recognizer, 2000, false);
  (void)recoveryEdge(recognizer, 3000, true);
  (void)recoveryEdge(recognizer, 4000, false);
  (void)recoveryEdge(recognizer, 5000, true);
  CHECK(recognizer.update(8000, true, false, false) ==
        RecoveryGestureResult::NETWORK_ACCESS_RESET);

  constexpr uint32_t base = UINT32_MAX - 1000U;
  recognizer.begin(base);
  (void)recoveryEdge(recognizer, base + 100U, false);
  (void)recoveryEdge(recognizer, base + 200U, true);
  (void)recoveryEdge(recognizer, base + 300U, false);
  (void)recoveryEdge(recognizer, base + 400U, true);
  (void)recoveryEdge(recognizer, base + 500U, false);
  (void)recoveryEdge(recognizer, base + 600U, true);
  CHECK(recognizer.update(base + 3600U, true, false, false) ==
        RecoveryGestureResult::NETWORK_ACCESS_RESET);
}

void p54_recovery_intent_round_trip_corruption_and_clear() {
  resetHostPersistence();
  CHECK(saveRecoveryIntent(RecoveryOperation::FACTORY_RESET));
  CHECK(recoveryIntentRecordPresent());
  RecoveryIntent intent;
  CHECK(loadRecoveryIntent(intent));
  CHECK(intent.operation ==
        static_cast<uint8_t>(RecoveryOperation::FACTORY_RESET));
  CHECK(persistence_host::corrupt(RECOVERY_NAMESPACE, RECOVERY_INTENT_KEY,
                                  offsetof(RecoveryIntent, checksum)));
  CHECK(!loadRecoveryIntent(intent));
  CHECK(recoveryIntentRecordPresent());
  CHECK(clearRecoveryIntent());
  CHECK(!recoveryIntentRecordPresent());

  persistence_host::failNextWrite = true;
  CHECK(!saveRecoveryIntent(RecoveryOperation::NETWORK_ACCESS_RESET));
}

void p55_network_access_reset_preserves_non_network_settings() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 41;
  strcpy(settings.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  strcpy(settings.preferredScaleName, "Lunar");
  settings.staConfigured = true;
  settings.staOpen = false;
  strcpy(settings.staSsid, "CafeLAN");
  strcpy(settings.staPassword, "CafePass1");
  settings.staIpMode = static_cast<uint8_t>(StaIpMode::STATIC);
  settings.staIp[0] = 192;
  settings.staIp[1] = 168;
  settings.staIp[2] = 10;
  settings.staIp[3] = 50;
  settings.staNetmask[0] = 255;
  settings.staNetmask[1] = 255;
  settings.staNetmask[2] = 255;
  settings.staGateway[0] = 192;
  settings.staGateway[1] = 168;
  settings.staGateway[2] = 10;
  settings.staGateway[3] = 1;
  settings.staDns1[0] = 1;
  settings.staDns1[1] = 1;
  settings.staDns1[2] = 1;
  settings.staDns1[3] = 1;
  settings.lkgValid = true;
  settings.lkgOpen = false;
  strcpy(settings.lkgSsid, "OldCafeLAN");
  strcpy(settings.lkgPassword, "OldCafe1");
  settings.lkgIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  CHECK(setAccessPointPassword(settings, "NewAccess1"));
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));

  PersistedSettings reset;
  CHECK(resetPersistedNetworkAccess(reset));
  CHECK(!reset.staConfigured);
  CHECK(!reset.lkgValid);
  CHECK(reset.staIpMode == static_cast<uint8_t>(StaIpMode::DHCP));
  CHECK(passwordIsFactoryDefault(reset));
  CHECK(reset.runtime.goalWeightG == 41);
  CHECK(strcmp(reset.preferredScaleMac, "AA:BB:CC:DD:EE:FF") == 0);
  CHECK(strcmp(reset.preferredScaleName, "Lunar") == 0);
}

struct TestCase {
  const char *id;
  void (*function)();
};

const TestCase tests[] = {
    {"P01", p01_defaults_are_valid},
    {"P02", p02_newest_valid_slot_is_loaded},
    {"P02B", p02b_save_uses_ram_revision_when_slots_unreadable},
    {"P02C", p02c_overlay_live_runtime_is_saved_not_stale_blob},
    {"P03", p03_corrupt_newest_slot_falls_back},
    {"P04", p04_crc_and_semantic_validation_reject_corruption},
    {"P05", p05_password_change_updates_hash},
    {"P07", p07_invalid_schema_uses_factory_on_missing_slots},
    {"P08", p08_factory_reset_rebuilds_defaults},
    {"P09", p09_fast_extraction_guard_validation},
    {"P10", p10_auto_to_manual_guard_trend_and_validation},
    {"P12", p12_shot_log_persists_compact_blob},
    {"P47", p47_rejects_non_current_schema_blob},
    {"P46", p46_ring_retain_log_level_persists_round_trip},
    {"P43", p43_scale_history_upsert_and_lru},
    {"P44", p44_scale_history_canonicalizes_mac_case},
    {"P24", p24_preset_bank_size_and_crud_budgets},
    {"P25", p25_invalid_active_id_keeps_customs},
    {"P26", p26_save_candidate_validation_does_not_require_live_mutation},
    {"P35", p35_invalid_fast_extraction_recipe_keeps_custom},
    {"P38", p38_invalid_slow_extraction_recipe_keeps_custom},
    {"P16", p16_static_ip_address_validation},
    {"P18", p18_shot_log_keeps_history_when_inactive_slot_write_fails},
    {"P19", p19_shot_log_weight_sentinel_allows_int16_max},
    {"P29", p29_last_shot_persists_and_clears},
    {"P48", p48_ble_companion_defaults_and_dual_slot_round_trip},
    {"P49", p49_ble_companion_corruption_falls_back_and_reset_enables},
    {"P50", p50_recovery_three_cycles_confirm_network_reset},
    {"P51", p51_recovery_five_cycles_upgrade_factory_candidate},
    {"P52", p52_recovery_rejects_four_slow_and_late_confirmation},
    {"P53", p53_recovery_boundaries_and_millis_wraparound},
    {"P54", p54_recovery_intent_round_trip_corruption_and_clear},
    {"P55", p55_network_access_reset_preserves_non_network_settings},
};

}  // namespace

int main() {
  for (const TestCase &test : tests) {
    const int before = failures;
    test.function();
    ++testsRun;
    std::cout << test.id << (failures == before ? " PASS" : " FAIL") << '\n';
  }
  std::cout << testsRun << " persistence tests, " << failures
            << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
