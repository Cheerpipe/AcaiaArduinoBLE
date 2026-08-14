#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"
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

PersistedSettingsV12 makeSchemaTwelveRecord(const PersistedSettings &source,
                                            uint32_t storageRevision) {
  PersistedSettingsV12 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V12;
  legacy.structureSize = sizeof(PersistedSettingsV12);
  legacy.storageRevision = storageRevision;
  legacy.runtime.revision = source.runtime.revision;
  legacy.runtime.goalWeightG = source.runtime.goalWeightG;
  legacy.runtime.weightOffsetG = source.runtime.weightOffsetG;
  legacy.runtime.autoTare = source.runtime.autoTare;
  legacy.runtime.timerOnly = source.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = source.runtime.canTareStartTimer;
  legacy.runtime.firstDropBeep = source.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep = source.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      source.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      source.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = source.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = source.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = source.runtime.autoRetare;
  legacy.runtime.retareWindowMs = source.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = source.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples = source.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      source.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs = source.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      source.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = source.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = source.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes = source.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = source.runtime.ntpServerPreset;
  memcpy(legacy.runtime.ntpServerCustom, source.runtime.ntpServerCustom,
         sizeof(legacy.runtime.ntpServerCustom));
  legacy.staConfigured = source.staConfigured;
  legacy.staOpen = source.staOpen;
  memcpy(legacy.staSsid, source.staSsid, sizeof(legacy.staSsid));
  memcpy(legacy.staPassword, source.staPassword, sizeof(legacy.staPassword));
  memcpy(legacy.apPassword, source.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, source.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, source.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV12Checksum(legacy);
  return legacy;
}

void p01_defaults_are_valid_v16() {
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
  CHECK(verifyAdminPassword(settings, DEFAULT_AP_PASSWORD));
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
  CHECK(settings.runtime.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(DEFAULT_ALERT_OUTPUT_CHANNEL ==
        (BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY
                                : AlertOutputChannel::SCALE_ONLY));
  CHECK(settings.runtime.bookooMuteOnBuzzerOnly);
  CHECK(settings.runtime.bookooConnectBeepLevel ==
        DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(settings.runtime.buzzerExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(settings.runtime.avoidBbwShotWithoutScale);
  CHECK(settings.runtime.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
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
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision == firstRevision + 1);

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 47);
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
  CHECK(!refreshAuthentication(settings, DEFAULT_AP_PASSWORD));
  CHECK(refreshAuthentication(settings, "NuevaClaveSegura"));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, "NuevaClaveSegura"));
  CHECK(!passwordIsFactoryDefault(settings));
}

void p17_legacy_password_hash_still_verifies() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(calculatePasswordHashLegacy(settings.authSalt, settings.apPassword,
                                    settings.authHash));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, DEFAULT_AP_PASSWORD));
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
  strcpy(shot.scaleProtocol, "acaia");
  CHECK(store.persist(shot));
  CHECK(persistence_host::records.count("lastshot/record") == 1);

  LastShotStore reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.get().valid);
  CHECK(reloaded.get().cycleId == 42);
  CHECK(reloaded.get().goalWeightG == 36);
  CHECK(fabs(reloaded.get().currentWeightG - 36.2f) < 0.001f);
  CHECK(strcmp(reloaded.get().scaleProtocol, "acaia") == 0);

  CHECK(reloaded.clear());
  LastShotStore emptied;
  CHECK(emptied.load());
  CHECK(!emptied.get().valid);
}

void p06_schema_twelve_migrates_to_thirteen() {
  resetHostPersistence();
  PersistedSettings previous;
  CHECK(initializeDefaultSettings(previous));
  previous.runtime.goalWeightG = 52;
  previous.runtime.weightOffsetG = 2.1f;
  previous.staConfigured = true;
  previous.staOpen = false;
  strcpy(previous.staSsid, "DevNetwork");
  strcpy(previous.staPassword, "DevPassword");
  const PersistedSettingsV12 legacy = makeSchemaTwelveRecord(previous, 1);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 52);
  CHECK(std::fabs(loaded.runtime.weightOffsetG - 2.1f) < 0.001f);
  CHECK(!loaded.runtime.fastExtractionGuardEnabled);
  CHECK(std::fabs(loaded.runtime.maxRecoveryWeightG -
                  DEFAULT_MAX_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(loaded.runtime.minBrewTimeMs == DEFAULT_MIN_BREW_TIME_MS);
  CHECK(loaded.staConfigured);
  CHECK(strcmp(loaded.staSsid, "DevNetwork") == 0);
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

void p11_schema_thirteen_migrates_to_current() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV13 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V13;
  legacy.structureSize = sizeof(PersistedSettingsV13);
  legacy.storageRevision = 3;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 41;
  legacy.runtime.weightOffsetG = 2.0f;
  legacy.runtime.autoTare = true;
  legacy.runtime.timerOnly = false;
  legacy.runtime.canTareStartTimer = true;
  legacy.runtime.firstDropBeep = true;
  legacy.runtime.paddleReturnReminderBeep = true;
  legacy.runtime.paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  legacy.runtime.rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  legacy.runtime.rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  legacy.runtime.autoRetare = true;
  legacy.runtime.retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  legacy.runtime.minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  legacy.runtime.retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  legacy.runtime.retareStabilityToleranceG =
      DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  legacy.runtime.retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  legacy.runtime.retareStabilityMinDurationMs =
      DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  legacy.runtime.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  legacy.runtime.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  legacy.runtime.timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  legacy.runtime.ntpServerPreset =
      static_cast<uint8_t>(NtpServerPreset::POOL);
  legacy.runtime.fastExtractionGuardEnabled = true;
  legacy.runtime.maxRecoveryWeightG = 42.5f;
  legacy.runtime.minBrewTimeMs = 26000;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV13Checksum(legacy);

  Preferences preferences;
  CHECK(preferences.begin(SETTINGS_NAMESPACE, false));
  CHECK(preferences.putBytes(SETTINGS_SLOT_A, &legacy, sizeof(legacy)) ==
        sizeof(legacy));
  preferences.end();

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 41);
  CHECK(loaded.runtime.fastExtractionGuardEnabled);
  CHECK(loaded.runtime.autoToManualGuardEnabled);
  CHECK(loaded.runtime.autoToManualGuardSamplesDs[0] ==
        AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS);
  CHECK(loaded.staIpMode == static_cast<uint8_t>(StaIpMode::DHCP));
  CHECK(!loaded.lkgValid);
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
}

void p13_shot_log_migrates_v5_full_blob_to_compact() {
  resetHostPersistence();
  ShotLogStoreV5 legacy = {};
  legacy.header.bootId = 9;
  legacy.header.nextRecordId = 2;
  legacy.header.count = 1;
  legacy.header.writeIndex = 1;
  legacy.records[0].id = 1;
  legacy.records[0].bootId = 9;
  legacy.records[0].durationDs = 300;
  legacy.records[0].goalWeightG = 40;
  legacy.records[0].actualWeightCg = 4000;
  legacy.header.magic = SHOT_LOG_MAGIC;
  legacy.header.schemaVersion = 5;
  legacy.header.recordSize = sizeof(ShotLogRecordV5);
  legacy.header.checksum = shotLogChecksumBytes(legacy.header);
  CHECK(validShotLogStoreV5(legacy));
  persistence_host::putRaw("shotlog", "records", &legacy, sizeof(legacy));

  ShotLog log;
  CHECK(log.load());
  CHECK(log.count() == 1);
  const auto foundA = persistence_host::records.find("shotlog/recordsA");
  const auto foundB = persistence_host::records.find("shotlog/recordsB");
  CHECK(foundA != persistence_host::records.end() ||
        foundB != persistence_host::records.end());
  const auto &blob =
      foundA != persistence_host::records.end() ? foundA->second : foundB->second;
  CHECK(blob.size() == sizeof(ShotLogHeader) + sizeof(ShotLogRecord));
}

void p14_schema_fourteen_migrates_to_current() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV14 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V14;
  legacy.structureSize = sizeof(PersistedSettingsV14);
  legacy.storageRevision = 7;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 44;
  legacy.runtime.weightOffsetG = current.runtime.weightOffsetG;
  legacy.runtime.autoTare = current.runtime.autoTare;
  legacy.runtime.timerOnly = current.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = current.runtime.canTareStartTimer;
  legacy.runtime.firstDropBeep = current.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = current.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = current.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = current.runtime.autoRetare;
  legacy.runtime.retareWindowMs = current.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = current.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples =
      current.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs =
      current.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = current.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = current.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes =
      current.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = current.runtime.ntpServerPreset;
  legacy.runtime.fastExtractionGuardEnabled =
      current.runtime.fastExtractionGuardEnabled;
  legacy.runtime.maxRecoveryWeightG = 52.0f;
  legacy.runtime.minBrewTimeMs = current.runtime.minBrewTimeMs;
  legacy.runtime.autoToManualGuardEnabled = true;
  legacy.runtime.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  legacy.runtime.autoToManualGuardManualLimitMs = 30000;
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    legacy.runtime.autoToManualGuardSamplesDs[i] = 300;
  }
  legacy.staConfigured = true;
  legacy.staOpen = false;
  strcpy(legacy.staSsid, "CafeLAN");
  strcpy(legacy.staPassword, "CafePass1");
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV14Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 44);
  CHECK(loaded.runtime.autoToManualGuardBaselineMs == 30000);
  CHECK(loaded.runtime.autoToManualGuardSamplesDs[0] == 300);
  CHECK(loaded.staConfigured);
  CHECK(strcmp(loaded.staSsid, "CafeLAN") == 0);
  CHECK(loaded.staIpMode == static_cast<uint8_t>(StaIpMode::DHCP));
  CHECK(loaded.staConfigState ==
        static_cast<uint8_t>(StaConfigState::CONFIRMED));
  CHECK(!loaded.lkgValid);
}

void p15_schema_fifteen_migrates_to_sixteen() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV15 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V15;
  legacy.structureSize = sizeof(PersistedSettingsV15);
  legacy.storageRevision = 9;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 38;
  legacy.runtime.weightOffsetG = current.runtime.weightOffsetG;
  legacy.runtime.autoTare = current.runtime.autoTare;
  legacy.runtime.timerOnly = current.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = current.runtime.canTareStartTimer;
  legacy.runtime.firstDropBeep = current.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = current.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = current.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = current.runtime.autoRetare;
  legacy.runtime.retareWindowMs = current.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = current.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples =
      current.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs =
      current.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = current.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = current.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes =
      current.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = current.runtime.ntpServerPreset;
  legacy.runtime.fastExtractionGuardEnabled = true;
  legacy.runtime.maxRecoveryWeightG = 45.0f;
  legacy.runtime.minBrewTimeMs = current.runtime.minBrewTimeMs;
  legacy.runtime.autoToManualGuardEnabled = true;
  legacy.runtime.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
  legacy.runtime.autoToManualGuardManualLimitMs = 27000;
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    legacy.runtime.autoToManualGuardSamplesDs[i] = 250;
  }
  legacy.staConfigured = true;
  legacy.staOpen = false;
  strcpy(legacy.staSsid, "ShopWiFi");
  strcpy(legacy.staPassword, "ShopPass12");
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV15Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 38);
  CHECK(loaded.runtime.autoToManualGuardManualLimitMs == 27000);
  CHECK(loaded.runtime.autoToManualGuardBaselineMs == 27000);
  CHECK(loaded.runtime.autoToManualGuardSamplesDs[4] == 250);
  CHECK(loaded.runtime.scaleTimerStopExtraDelayMs ==
        DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS);
  CHECK(strcmp(loaded.staSsid, "ShopWiFi") == 0);
}

void p20_schema_sixteen_migrates_to_seventeen() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV16 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V16;
  legacy.structureSize = sizeof(PersistedSettingsV16);
  legacy.storageRevision = 10;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 40;
  legacy.runtime.weightOffsetG = current.runtime.weightOffsetG;
  legacy.runtime.autoTare = current.runtime.autoTare;
  legacy.runtime.timerOnly = current.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = current.runtime.canTareStartTimer;
  legacy.runtime.firstDropBeep = current.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = current.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = current.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = current.runtime.autoRetare;
  legacy.runtime.retareWindowMs = current.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = current.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples =
      current.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs =
      current.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = current.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = current.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes =
      current.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = current.runtime.ntpServerPreset;
  legacy.runtime.fastExtractionGuardEnabled = true;
  legacy.runtime.maxRecoveryWeightG = 48.0f;
  legacy.runtime.minBrewTimeMs = current.runtime.minBrewTimeMs;
  legacy.runtime.autoToManualGuardEnabled = true;
  legacy.runtime.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  legacy.runtime.autoToManualGuardManualLimitMs = 30000;
  legacy.runtime.autoToManualGuardBaselineMs = 31000;
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    legacy.runtime.autoToManualGuardSamplesDs[i] = 280;
  }
  legacy.staConfigured = true;
  legacy.staOpen = false;
  strcpy(legacy.staSsid, "CafeWiFi");
  strcpy(legacy.staPassword, "CafePass12");
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV16Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 40);
  CHECK(loaded.runtime.autoToManualGuardBaselineMs == 31000);
  CHECK(loaded.runtime.scaleTimerStopExtraDelayMs ==
        DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS);
  CHECK(strcmp(loaded.staSsid, "CafeWiFi") == 0);
  CHECK(loaded.preferredScaleMac[0] == '\0');
}

void p21_schema_seventeen_migrates_to_current() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV17 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V17;
  legacy.structureSize = sizeof(PersistedSettingsV17);
  legacy.storageRevision = 11;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 41;
  legacy.runtime.weightOffsetG = current.runtime.weightOffsetG;
  legacy.runtime.autoTare = current.runtime.autoTare;
  legacy.runtime.timerOnly = current.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = current.runtime.canTareStartTimer;
  legacy.runtime.shotTimerStartDelayMs = 250;
  legacy.runtime.firstDropBeep = current.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = current.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = current.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = current.runtime.autoRetare;
  legacy.runtime.retareWindowMs = current.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = current.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples =
      current.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs =
      current.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = current.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = current.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes =
      current.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = current.runtime.ntpServerPreset;
  memcpy(legacy.runtime.ntpServerCustom, current.runtime.ntpServerCustom,
         sizeof(legacy.runtime.ntpServerCustom));
  legacy.runtime.fastExtractionGuardEnabled =
      current.runtime.fastExtractionGuardEnabled;
  legacy.runtime.maxRecoveryWeightG = current.runtime.maxRecoveryWeightG;
  legacy.runtime.minBrewTimeMs = current.runtime.minBrewTimeMs;
  legacy.runtime.autoToManualGuardEnabled =
      current.runtime.autoToManualGuardEnabled;
  legacy.runtime.autoToManualGuardLimitMode =
      current.runtime.autoToManualGuardLimitMode;
  legacy.runtime.autoToManualGuardManualLimitMs =
      current.runtime.autoToManualGuardManualLimitMs;
  legacy.runtime.autoToManualGuardBaselineMs =
      current.runtime.autoToManualGuardBaselineMs;
  memcpy(legacy.runtime.autoToManualGuardSamplesDs,
         current.runtime.autoToManualGuardSamplesDs,
         sizeof(legacy.runtime.autoToManualGuardSamplesDs));
  legacy.staConfigured = true;
  legacy.staOpen = false;
  strcpy(legacy.staSsid, "CafeWiFi17");
  strcpy(legacy.staPassword, "CafePass17");
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV17Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 41);
  CHECK(std::fabs(loaded.runtime.weightOffsetBaselineG -
                  DEFAULT_WEIGHT_OFFSET_G) < 0.001f);
  CHECK(strcmp(loaded.staSsid, "CafeWiFi17") == 0);
  CHECK(loaded.preferredScaleMac[0] == '\0');

  strcpy(loaded.preferredScaleMac, "11:22:33:44:55:66");
  finalizePersistedSettings(loaded);
  CHECK(savePersistedSettings(loaded));
  PersistedSettings reloaded;
  CHECK(loadPersistedSettings(reloaded));
  CHECK(strcmp(reloaded.preferredScaleMac, "11:22:33:44:55:66") == 0);
}

void p22_schema_eighteen_migrates_to_nineteen() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV18 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V18;
  legacy.structureSize = sizeof(PersistedSettingsV18);
  legacy.storageRevision = 12;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 39;
  legacy.runtime.weightOffsetG = 2.75f;
  legacy.runtime.autoTare = current.runtime.autoTare;
  legacy.runtime.timerOnly = current.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = current.runtime.canTareStartTimer;
  legacy.runtime.shotTimerStartDelayMs = 250;
  legacy.runtime.firstDropBeep = current.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = current.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = current.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = current.runtime.autoRetare;
  legacy.runtime.retareWindowMs = current.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = current.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples =
      current.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs =
      current.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = current.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = current.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes =
      current.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = current.runtime.ntpServerPreset;
  memcpy(legacy.runtime.ntpServerCustom, current.runtime.ntpServerCustom,
         sizeof(legacy.runtime.ntpServerCustom));
  legacy.runtime.fastExtractionGuardEnabled =
      current.runtime.fastExtractionGuardEnabled;
  legacy.runtime.maxRecoveryWeightG = current.runtime.maxRecoveryWeightG;
  legacy.runtime.minBrewTimeMs = current.runtime.minBrewTimeMs;
  legacy.runtime.autoToManualGuardEnabled =
      current.runtime.autoToManualGuardEnabled;
  legacy.runtime.autoToManualGuardLimitMode =
      current.runtime.autoToManualGuardLimitMode;
  legacy.runtime.autoToManualGuardManualLimitMs =
      current.runtime.autoToManualGuardManualLimitMs;
  legacy.runtime.autoToManualGuardBaselineMs =
      current.runtime.autoToManualGuardBaselineMs;
  memcpy(legacy.runtime.autoToManualGuardSamplesDs,
         current.runtime.autoToManualGuardSamplesDs,
         sizeof(legacy.runtime.autoToManualGuardSamplesDs));
  legacy.staConfigured = true;
  legacy.staOpen = false;
  strcpy(legacy.staSsid, "CafeWiFi18");
  strcpy(legacy.staPassword, "CafePass18");
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  legacy.checksum = persistedSettingsV18Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 39);
  CHECK(std::fabs(loaded.runtime.weightOffsetG - 2.75f) < 0.001f);
  CHECK(std::fabs(loaded.runtime.weightOffsetBaselineG -
                  DEFAULT_WEIGHT_OFFSET_G) < 0.001f);
  CHECK(loaded.runtime.scaleTimerStopExtraDelayMs ==
        DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS);
  CHECK(strcmp(loaded.staSsid, "CafeWiFi18") == 0);
  CHECK(strcmp(loaded.preferredScaleMac, "AA:BB:CC:DD:EE:FF") == 0);
}


void p23_schema_nineteen_migrates_to_twenty_with_preset_bank() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV19 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V19;
  legacy.structureSize = sizeof(PersistedSettingsV19);
  legacy.storageRevision = 14;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 40;
  legacy.runtime.weightOffsetG = 2.25f;
  legacy.runtime.weightOffsetBaselineG = 1.75f;
  legacy.runtime.autoTare = current.runtime.autoTare;
  legacy.runtime.timerOnly = false;
  legacy.runtime.canTareStartTimer = current.runtime.canTareStartTimer;
  legacy.runtime.shotTimerStartDelayMs = 250;
  legacy.runtime.firstDropBeep = current.runtime.firstDropBeep;
  legacy.runtime.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  legacy.runtime.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  legacy.runtime.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  legacy.runtime.rinseGestureMs = current.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = current.runtime.rinseDurationMs;
  legacy.runtime.autoRetare = current.runtime.autoRetare;
  legacy.runtime.retareWindowMs = current.runtime.retareWindowMs;
  legacy.runtime.minimumCupWeightG = current.runtime.minimumCupWeightG;
  legacy.runtime.retareStabilitySamples =
      current.runtime.retareStabilitySamples;
  legacy.runtime.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  legacy.runtime.retareStabilityMaxGapMs =
      current.runtime.retareStabilityMaxGapMs;
  legacy.runtime.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  legacy.runtime.bbwProtectionMs = current.runtime.bbwProtectionMs;
  legacy.runtime.operationalWallMs = current.runtime.operationalWallMs;
  legacy.runtime.timezoneOffsetMinutes =
      current.runtime.timezoneOffsetMinutes;
  legacy.runtime.ntpServerPreset = current.runtime.ntpServerPreset;
  memcpy(legacy.runtime.ntpServerCustom, current.runtime.ntpServerCustom,
         sizeof(legacy.runtime.ntpServerCustom));
  legacy.runtime.fastExtractionGuardEnabled = true;
  legacy.runtime.maxRecoveryWeightG = 48.0f;
  legacy.runtime.minBrewTimeMs = 25000;
  legacy.runtime.autoToManualGuardEnabled =
      current.runtime.autoToManualGuardEnabled;
  legacy.runtime.autoToManualGuardLimitMode =
      current.runtime.autoToManualGuardLimitMode;
  legacy.runtime.autoToManualGuardManualLimitMs =
      current.runtime.autoToManualGuardManualLimitMs;
  legacy.runtime.autoToManualGuardBaselineMs =
      current.runtime.autoToManualGuardBaselineMs;
  memcpy(legacy.runtime.autoToManualGuardSamplesDs,
         current.runtime.autoToManualGuardSamplesDs,
         sizeof(legacy.runtime.autoToManualGuardSamplesDs));
  legacy.staConfigured = true;
  legacy.staOpen = false;
  strcpy(legacy.staSsid, "CafeWiFi19");
  strcpy(legacy.staPassword, "CafePass19");
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "AA:BB:CC:DD:EE:11");
  legacy.checksum = persistedSettingsV19Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.presets.count >= 2);
  CHECK(loaded.presets.activeId == FACTORY_PRESET_ID_DOUBLE);
  const ShotPreset *dbl =
      findShotPreset(loaded.presets, FACTORY_PRESET_ID_DOUBLE);
  const ShotPreset *sgl =
      findShotPreset(loaded.presets, FACTORY_PRESET_ID_SINGLE);
  CHECK(dbl != nullptr);
  CHECK(sgl != nullptr);
  CHECK(dbl->goalWeightG == 40);
  CHECK(std::fabs(dbl->weightOffsetG - 2.25f) < 0.001f);
  CHECK(std::fabs(dbl->weightOffsetBaselineG - 1.75f) < 0.001f);
  CHECK(std::fabs(dbl->maxRecoveryWeightG - 48.0f) < 0.001f);
  CHECK(dbl->minBrewTimeMs == 25000);
  CHECK(sgl->goalWeightG == 18);
  CHECK(std::fabs(sgl->weightOffsetBaselineG - 0.5f) < 0.001f);
  CHECK(std::fabs(sgl->weightOffsetG - 0.5f) < 0.001f);
  CHECK(strcmp(loaded.staSsid, "CafeWiFi19") == 0);
}

void p27_schema_twenty_migrates_to_twenty_one() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  ensurePersistedPresetBank(current);
  finalizePersistedSettings(current);

  PersistedSettingsV20 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V20;
  legacy.structureSize = sizeof(PersistedSettingsV20);
  legacy.storageRevision = 7;
  RuntimeConfigV20 runtimeV20 = {};
  runtimeV20.revision = 3;
  runtimeV20.goalWeightG = 42;
  runtimeV20.weightOffsetG = current.runtime.weightOffsetG;
  runtimeV20.weightOffsetBaselineG = current.runtime.weightOffsetBaselineG;
  runtimeV20.autoTare = current.runtime.autoTare;
  runtimeV20.timerOnly = current.runtime.timerOnly;
  runtimeV20.canTareStartTimer = current.runtime.canTareStartTimer;
  runtimeV20.shotTimerStartDelayMs = 250;
  runtimeV20.firstDropBeep = current.runtime.firstDropBeep;
  runtimeV20.paddleReturnReminderBeep = current.runtime.paddleReturnReminderBeep;
  runtimeV20.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  runtimeV20.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  runtimeV20.rinseGestureMs = current.runtime.rinseGestureMs;
  runtimeV20.rinseDurationMs = current.runtime.rinseDurationMs;
  runtimeV20.autoRetare = current.runtime.autoRetare;
  runtimeV20.retareWindowMs = current.runtime.retareWindowMs;
  runtimeV20.minimumCupWeightG = current.runtime.minimumCupWeightG;
  runtimeV20.retareStabilitySamples = current.runtime.retareStabilitySamples;
  runtimeV20.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  runtimeV20.retareStabilityMaxGapMs = current.runtime.retareStabilityMaxGapMs;
  runtimeV20.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  runtimeV20.bbwProtectionMs = current.runtime.bbwProtectionMs;
  runtimeV20.operationalWallMs = current.runtime.operationalWallMs;
  runtimeV20.timezoneOffsetMinutes = current.runtime.timezoneOffsetMinutes;
  runtimeV20.ntpServerPreset = current.runtime.ntpServerPreset;
  memcpy(runtimeV20.ntpServerCustom, current.runtime.ntpServerCustom,
         sizeof(runtimeV20.ntpServerCustom));
  runtimeV20.fastExtractionGuardEnabled =
      current.runtime.fastExtractionGuardEnabled;
  runtimeV20.maxRecoveryWeightG = current.runtime.maxRecoveryWeightG;
  runtimeV20.minBrewTimeMs = current.runtime.minBrewTimeMs;
  runtimeV20.autoToManualGuardEnabled = current.runtime.autoToManualGuardEnabled;
  runtimeV20.autoToManualGuardLimitMode =
      current.runtime.autoToManualGuardLimitMode;
  runtimeV20.autoToManualGuardManualLimitMs =
      current.runtime.autoToManualGuardManualLimitMs;
  runtimeV20.autoToManualGuardBaselineMs =
      current.runtime.autoToManualGuardBaselineMs;
  memcpy(runtimeV20.autoToManualGuardSamplesDs,
         current.runtime.autoToManualGuardSamplesDs,
         sizeof(runtimeV20.autoToManualGuardSamplesDs));
  legacy.runtime = runtimeV20;
  legacy.presets = current.presets;
  legacy.staConfigured = false;
  legacy.staOpen = false;
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "11:22:33:44:55:66");
  legacy.checksum = persistedSettingsV20Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 42);
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
  CHECK(strcmp(loaded.preferredScaleMac, "11:22:33:44:55:66") == 0);
  CHECK(loaded.preferredScaleName[0] == '\0');
}

void p28_schema_twenty_one_migrates_to_twenty_two() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  ensurePersistedPresetBank(current);
  finalizePersistedSettings(current);

  PersistedSettingsV21 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V21;
  legacy.structureSize = sizeof(PersistedSettingsV21);
  legacy.storageRevision = 9;
  RuntimeConfigV21 runtimeV21 = {};
  runtimeV21.revision = 4;
  runtimeV21.goalWeightG = 38;
  runtimeV21.weightOffsetG = current.runtime.weightOffsetG;
  runtimeV21.weightOffsetBaselineG = current.runtime.weightOffsetBaselineG;
  runtimeV21.autoTare = current.runtime.autoTare;
  runtimeV21.timerOnly = current.runtime.timerOnly;
  runtimeV21.canTareStartTimer = current.runtime.canTareStartTimer;
  runtimeV21.shotTimerStartDelayMs = 250;
  runtimeV21.firstDropBeep = current.runtime.firstDropBeep;
  runtimeV21.paddleReturnReminderBeep = current.runtime.paddleReturnReminderBeep;
  runtimeV21.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  runtimeV21.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  runtimeV21.rinseGestureMs = current.runtime.rinseGestureMs;
  runtimeV21.rinseDurationMs = current.runtime.rinseDurationMs;
  runtimeV21.autoRetare = current.runtime.autoRetare;
  runtimeV21.retareWindowMs = current.runtime.retareWindowMs;
  runtimeV21.minimumCupWeightG = current.runtime.minimumCupWeightG;
  runtimeV21.retareStabilitySamples = current.runtime.retareStabilitySamples;
  runtimeV21.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  runtimeV21.retareStabilityMaxGapMs = current.runtime.retareStabilityMaxGapMs;
  runtimeV21.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  runtimeV21.bbwProtectionMs = current.runtime.bbwProtectionMs;
  runtimeV21.operationalWallMs = current.runtime.operationalWallMs;
  runtimeV21.timezoneOffsetMinutes = current.runtime.timezoneOffsetMinutes;
  runtimeV21.ntpServerPreset = current.runtime.ntpServerPreset;
  memcpy(runtimeV21.ntpServerCustom, current.runtime.ntpServerCustom,
         sizeof(runtimeV21.ntpServerCustom));
  runtimeV21.fastExtractionGuardEnabled =
      current.runtime.fastExtractionGuardEnabled;
  runtimeV21.maxRecoveryWeightG = current.runtime.maxRecoveryWeightG;
  runtimeV21.minBrewTimeMs = current.runtime.minBrewTimeMs;
  runtimeV21.autoToManualGuardEnabled = current.runtime.autoToManualGuardEnabled;
  runtimeV21.autoToManualGuardLimitMode =
      current.runtime.autoToManualGuardLimitMode;
  runtimeV21.autoToManualGuardManualLimitMs =
      current.runtime.autoToManualGuardManualLimitMs;
  runtimeV21.autoToManualGuardBaselineMs =
      current.runtime.autoToManualGuardBaselineMs;
  memcpy(runtimeV21.autoToManualGuardSamplesDs,
         current.runtime.autoToManualGuardSamplesDs,
         sizeof(runtimeV21.autoToManualGuardSamplesDs));
  runtimeV21.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  legacy.runtime = runtimeV21;
  legacy.presets = current.presets;
  legacy.staConfigured = false;
  legacy.staOpen = false;
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  strcpy(legacy.preferredScaleName, "Lunar");
  legacy.checksum = persistedSettingsV21Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 38);
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
  CHECK(loaded.runtime.buzzerScaleLostBeep);
  CHECK(loaded.runtime.buzzerAutoToManualGuardEndBeep);
  CHECK(loaded.runtime.buzzerManualNoScaleBeep);
  CHECK(loaded.runtime.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(strcmp(loaded.preferredScaleMac, "AA:BB:CC:DD:EE:FF") == 0);
  CHECK(strcmp(loaded.preferredScaleName, "Lunar") == 0);
}

void p30_schema_twenty_two_migrates_to_twenty_three() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  ensurePersistedPresetBank(current);
  finalizePersistedSettings(current);

  PersistedSettingsV22 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V22;
  legacy.structureSize = sizeof(PersistedSettingsV22);
  legacy.storageRevision = 11;
  RuntimeConfigV22 runtimeV22 = {};
  runtimeV22.revision = 5;
  runtimeV22.goalWeightG = 37;
  runtimeV22.weightOffsetG = current.runtime.weightOffsetG;
  runtimeV22.weightOffsetBaselineG = current.runtime.weightOffsetBaselineG;
  runtimeV22.autoTare = current.runtime.autoTare;
  runtimeV22.timerOnly = current.runtime.timerOnly;
  runtimeV22.canTareStartTimer = current.runtime.canTareStartTimer;
  runtimeV22.scaleTimerStopExtraDelayMs =
      current.runtime.scaleTimerStopExtraDelayMs;
  runtimeV22.firstDropBeep = current.runtime.firstDropBeep;
  runtimeV22.paddleReturnReminderBeep =
      current.runtime.paddleReturnReminderBeep;
  runtimeV22.paddleReturnReminderIntervalMs =
      current.runtime.paddleReturnReminderIntervalMs;
  runtimeV22.paddleReturnReminderMaxDurationMs =
      current.runtime.paddleReturnReminderMaxDurationMs;
  runtimeV22.buzzerScaleLostBeep = false;
  runtimeV22.buzzerAutoToManualGuardEndBeep = true;
  runtimeV22.buzzerManualNoScaleBeep = false;
  runtimeV22.reservedConfig = 0;
  runtimeV22.rinseGestureMs = current.runtime.rinseGestureMs;
  runtimeV22.rinseDurationMs = current.runtime.rinseDurationMs;
  runtimeV22.autoRetare = current.runtime.autoRetare;
  runtimeV22.retareWindowMs = current.runtime.retareWindowMs;
  runtimeV22.minimumCupWeightG = current.runtime.minimumCupWeightG;
  runtimeV22.retareStabilitySamples = current.runtime.retareStabilitySamples;
  runtimeV22.retareStabilityToleranceG =
      current.runtime.retareStabilityToleranceG;
  runtimeV22.retareStabilityMaxGapMs = current.runtime.retareStabilityMaxGapMs;
  runtimeV22.retareStabilityMinDurationMs =
      current.runtime.retareStabilityMinDurationMs;
  runtimeV22.bbwProtectionMs = current.runtime.bbwProtectionMs;
  runtimeV22.operationalWallMs = current.runtime.operationalWallMs;
  runtimeV22.timezoneOffsetMinutes = current.runtime.timezoneOffsetMinutes;
  runtimeV22.ntpServerPreset = current.runtime.ntpServerPreset;
  memcpy(runtimeV22.ntpServerCustom, current.runtime.ntpServerCustom,
         sizeof(runtimeV22.ntpServerCustom));
  runtimeV22.fastExtractionGuardEnabled =
      current.runtime.fastExtractionGuardEnabled;
  runtimeV22.maxRecoveryWeightG = current.runtime.maxRecoveryWeightG;
  runtimeV22.minBrewTimeMs = current.runtime.minBrewTimeMs;
  runtimeV22.autoToManualGuardEnabled = current.runtime.autoToManualGuardEnabled;
  runtimeV22.autoToManualGuardLimitMode =
      current.runtime.autoToManualGuardLimitMode;
  runtimeV22.autoToManualGuardManualLimitMs =
      current.runtime.autoToManualGuardManualLimitMs;
  runtimeV22.autoToManualGuardBaselineMs =
      current.runtime.autoToManualGuardBaselineMs;
  memcpy(runtimeV22.autoToManualGuardSamplesDs,
         current.runtime.autoToManualGuardSamplesDs,
         sizeof(runtimeV22.autoToManualGuardSamplesDs));
  runtimeV22.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::PARTIAL);
  legacy.runtime = runtimeV22;
  legacy.presets = current.presets;
  legacy.staConfigured = false;
  legacy.staOpen = false;
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "01:23:45:67:89:AB");
  strcpy(legacy.preferredScaleName, "Bookoo");
  legacy.checksum = persistedSettingsV22Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 37);
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
  CHECK(!loaded.runtime.buzzerScaleLostBeep);
  CHECK(loaded.runtime.buzzerAutoToManualGuardEndBeep);
  CHECK(!loaded.runtime.buzzerManualNoScaleBeep);
  CHECK(loaded.runtime.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(loaded.runtime.bookooMuteOnBuzzerOnly);
  CHECK(loaded.runtime.bookooConnectBeepLevel ==
        DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(strcmp(loaded.preferredScaleMac, "01:23:45:67:89:AB") == 0);
}

void fillRuntimeConfigV23FromCurrent(const RuntimeConfig &source,
                                     RuntimeConfigV23 &out) {
  out = RuntimeConfigV23{};
  out.revision = source.revision;
  out.goalWeightG = source.goalWeightG;
  out.weightOffsetG = source.weightOffsetG;
  out.weightOffsetBaselineG = source.weightOffsetBaselineG;
  out.autoTare = source.autoTare;
  out.timerOnly = source.timerOnly;
  out.canTareStartTimer = source.canTareStartTimer;
  out.scaleTimerStopExtraDelayMs = source.scaleTimerStopExtraDelayMs;
  out.firstDropBeep = source.firstDropBeep;
  out.paddleReturnReminderBeep = source.paddleReturnReminderBeep;
  out.paddleReturnReminderIntervalMs = source.paddleReturnReminderIntervalMs;
  out.paddleReturnReminderMaxDurationMs =
      source.paddleReturnReminderMaxDurationMs;
  out.buzzerScaleLostBeep = source.buzzerScaleLostBeep;
  out.buzzerAutoToManualGuardEndBeep = source.buzzerAutoToManualGuardEndBeep;
  out.buzzerManualNoScaleBeep = source.buzzerManualNoScaleBeep;
  out.alertOutputChannel = source.alertOutputChannel;
  out.reservedConfig = source.reservedConfig;
  out.reservedConfig2 = source.reservedConfig2;
  out.rinseGestureMs = source.rinseGestureMs;
  out.rinseDurationMs = source.rinseDurationMs;
  out.autoRetare = source.autoRetare;
  out.retareWindowMs = source.retareWindowMs;
  out.minimumCupWeightG = source.minimumCupWeightG;
  out.retareStabilitySamples = source.retareStabilitySamples;
  out.retareStabilityToleranceG = source.retareStabilityToleranceG;
  out.retareStabilityMaxGapMs = source.retareStabilityMaxGapMs;
  out.retareStabilityMinDurationMs = source.retareStabilityMinDurationMs;
  out.bbwProtectionMs = source.bbwProtectionMs;
  out.operationalWallMs = source.operationalWallMs;
  out.timezoneOffsetMinutes = source.timezoneOffsetMinutes;
  out.ntpServerPreset = source.ntpServerPreset;
  memcpy(out.ntpServerCustom, source.ntpServerCustom,
         sizeof(out.ntpServerCustom));
  out.fastExtractionGuardEnabled = source.fastExtractionGuardEnabled;
  out.maxRecoveryWeightG = source.maxRecoveryWeightG;
  out.minBrewTimeMs = source.minBrewTimeMs;
  out.autoToManualGuardEnabled = source.autoToManualGuardEnabled;
  out.autoToManualGuardLimitMode = source.autoToManualGuardLimitMode;
  out.autoToManualGuardManualLimitMs = source.autoToManualGuardManualLimitMs;
  out.autoToManualGuardBaselineMs = source.autoToManualGuardBaselineMs;
  memcpy(out.autoToManualGuardSamplesDs, source.autoToManualGuardSamplesDs,
         sizeof(out.autoToManualGuardSamplesDs));
  out.scaleMacCacheMode = source.scaleMacCacheMode;
}

void p31_schema_twenty_three_migrates_to_twenty_four() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  ensurePersistedPresetBank(current);
  finalizePersistedSettings(current);

  PersistedSettingsV23 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V23;
  legacy.structureSize = sizeof(PersistedSettingsV23);
  legacy.storageRevision = 12;
  RuntimeConfigV23 runtimeV23 = {};
  fillRuntimeConfigV23FromCurrent(current.runtime, runtimeV23);
  runtimeV23.revision = 6;
  runtimeV23.goalWeightG = 41;
  runtimeV23.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runtimeV23.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  legacy.runtime = runtimeV23;
  legacy.presets = current.presets;
  legacy.staConfigured = false;
  legacy.staOpen = false;
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "AA:BB:CC:DD:EE:11");
  strcpy(legacy.preferredScaleName, "Themis");
  legacy.checksum = persistedSettingsV23Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 41);
  CHECK(loaded.runtime.alertOutputChannel ==
        static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY));
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FULL));
  CHECK(loaded.runtime.bookooMuteOnBuzzerOnly);
  CHECK(loaded.runtime.bookooConnectBeepLevel ==
        DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(strcmp(loaded.preferredScaleMac, "AA:BB:CC:DD:EE:11") == 0);
}

void fillRuntimeConfigV24FromCurrent(const RuntimeConfig &source,
                                     RuntimeConfigV24 &out) {
  out = RuntimeConfigV24{};
  out.revision = source.revision;
  out.goalWeightG = source.goalWeightG;
  out.weightOffsetG = source.weightOffsetG;
  out.weightOffsetBaselineG = source.weightOffsetBaselineG;
  out.autoTare = source.autoTare;
  out.timerOnly = source.timerOnly;
  out.canTareStartTimer = source.canTareStartTimer;
  out.scaleTimerStopExtraDelayMs = source.scaleTimerStopExtraDelayMs;
  out.firstDropBeep = source.firstDropBeep;
  out.paddleReturnReminderBeep = source.paddleReturnReminderBeep;
  out.paddleReturnReminderIntervalMs = source.paddleReturnReminderIntervalMs;
  out.paddleReturnReminderMaxDurationMs =
      source.paddleReturnReminderMaxDurationMs;
  out.buzzerScaleLostBeep = source.buzzerScaleLostBeep;
  out.buzzerAutoToManualGuardEndBeep = source.buzzerAutoToManualGuardEndBeep;
  out.buzzerManualNoScaleBeep = source.buzzerManualNoScaleBeep;
  out.alertOutputChannel = source.alertOutputChannel;
  out.reservedConfig = source.reservedConfig;
  out.reservedConfig2 = source.reservedConfig2;
  out.rinseGestureMs = source.rinseGestureMs;
  out.rinseDurationMs = source.rinseDurationMs;
  out.autoRetare = source.autoRetare;
  out.retareWindowMs = source.retareWindowMs;
  out.minimumCupWeightG = source.minimumCupWeightG;
  out.retareStabilitySamples = source.retareStabilitySamples;
  out.retareStabilityToleranceG = source.retareStabilityToleranceG;
  out.retareStabilityMaxGapMs = source.retareStabilityMaxGapMs;
  out.retareStabilityMinDurationMs = source.retareStabilityMinDurationMs;
  out.bbwProtectionMs = source.bbwProtectionMs;
  out.operationalWallMs = source.operationalWallMs;
  out.timezoneOffsetMinutes = source.timezoneOffsetMinutes;
  out.ntpServerPreset = source.ntpServerPreset;
  memcpy(out.ntpServerCustom, source.ntpServerCustom,
         sizeof(out.ntpServerCustom));
  out.fastExtractionGuardEnabled = source.fastExtractionGuardEnabled;
  out.maxRecoveryWeightG = source.maxRecoveryWeightG;
  out.minBrewTimeMs = source.minBrewTimeMs;
  out.autoToManualGuardEnabled = source.autoToManualGuardEnabled;
  out.autoToManualGuardLimitMode = source.autoToManualGuardLimitMode;
  out.autoToManualGuardManualLimitMs = source.autoToManualGuardManualLimitMs;
  out.autoToManualGuardBaselineMs = source.autoToManualGuardBaselineMs;
  memcpy(out.autoToManualGuardSamplesDs, source.autoToManualGuardSamplesDs,
         sizeof(out.autoToManualGuardSamplesDs));
  out.scaleMacCacheMode = source.scaleMacCacheMode;
  out.bookooMuteOnBuzzerOnly = source.bookooMuteOnBuzzerOnly;
  out.bookooConnectBeepLevel = source.bookooConnectBeepLevel;
  out.reservedConfig3 = source.reservedConfig3;
}

void p32_schema_twenty_four_migrates_to_twenty_five() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  ensurePersistedPresetBank(current);
  finalizePersistedSettings(current);

  PersistedSettingsV24 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V24;
  legacy.structureSize = sizeof(PersistedSettingsV24);
  legacy.storageRevision = 13;
  RuntimeConfigV24 runtimeV24 = {};
  fillRuntimeConfigV24FromCurrent(current.runtime, runtimeV24);
  runtimeV24.revision = 7;
  runtimeV24.goalWeightG = 39;
  runtimeV24.bookooConnectBeepLevel = 2;
  runtimeV24.bookooMuteOnBuzzerOnly = false;
  legacy.runtime = runtimeV24;
  legacy.presets = current.presets;
  legacy.staConfigured = false;
  legacy.staOpen = false;
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "AA:BB:CC:DD:EE:22");
  strcpy(legacy.preferredScaleName, "Pyxis");
  legacy.checksum = persistedSettingsV24Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 39);
  CHECK(loaded.runtime.bookooConnectBeepLevel == 2);
  CHECK(!loaded.runtime.bookooMuteOnBuzzerOnly);
  CHECK(loaded.runtime.buzzerExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(loaded.runtime.avoidBbwShotWithoutScale);
  CHECK(loaded.runtime.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
  CHECK(strcmp(loaded.preferredScaleMac, "AA:BB:CC:DD:EE:22") == 0);
}

void fillRuntimeConfigV25FromCurrent(const RuntimeConfig &source,
                                     RuntimeConfigV25 &out) {
  out = RuntimeConfigV25{};
  out.revision = source.revision;
  out.goalWeightG = source.goalWeightG;
  out.weightOffsetG = source.weightOffsetG;
  out.weightOffsetBaselineG = source.weightOffsetBaselineG;
  out.autoTare = source.autoTare;
  out.timerOnly = source.timerOnly;
  out.canTareStartTimer = source.canTareStartTimer;
  out.scaleTimerStopExtraDelayMs = source.scaleTimerStopExtraDelayMs;
  out.firstDropBeep = source.firstDropBeep;
  out.paddleReturnReminderBeep = source.paddleReturnReminderBeep;
  out.paddleReturnReminderIntervalMs = source.paddleReturnReminderIntervalMs;
  out.paddleReturnReminderMaxDurationMs =
      source.paddleReturnReminderMaxDurationMs;
  out.buzzerScaleLostBeep = source.buzzerScaleLostBeep;
  out.buzzerAutoToManualGuardEndBeep = source.buzzerAutoToManualGuardEndBeep;
  out.buzzerManualNoScaleBeep = source.buzzerManualNoScaleBeep;
  out.buzzerExtendedPulseRate = source.buzzerExtendedPulseRate;
  out.alertOutputChannel = source.alertOutputChannel;
  out.reservedConfig = source.reservedConfig;
  out.reservedConfig2 = source.reservedConfig2;
  out.rinseGestureMs = source.rinseGestureMs;
  out.rinseDurationMs = source.rinseDurationMs;
  out.autoRetare = source.autoRetare;
  out.retareWindowMs = source.retareWindowMs;
  out.minimumCupWeightG = source.minimumCupWeightG;
  out.retareStabilitySamples = source.retareStabilitySamples;
  out.retareStabilityToleranceG = source.retareStabilityToleranceG;
  out.retareStabilityMaxGapMs = source.retareStabilityMaxGapMs;
  out.retareStabilityMinDurationMs = source.retareStabilityMinDurationMs;
  out.bbwProtectionMs = source.bbwProtectionMs;
  out.operationalWallMs = source.operationalWallMs;
  out.timezoneOffsetMinutes = source.timezoneOffsetMinutes;
  out.ntpServerPreset = source.ntpServerPreset;
  memcpy(out.ntpServerCustom, source.ntpServerCustom,
         sizeof(out.ntpServerCustom));
  out.fastExtractionGuardEnabled = source.fastExtractionGuardEnabled;
  out.maxRecoveryWeightG = source.maxRecoveryWeightG;
  out.minBrewTimeMs = source.minBrewTimeMs;
  out.autoToManualGuardEnabled = source.autoToManualGuardEnabled;
  out.autoToManualGuardLimitMode = source.autoToManualGuardLimitMode;
  out.autoToManualGuardManualLimitMs = source.autoToManualGuardManualLimitMs;
  out.autoToManualGuardBaselineMs = source.autoToManualGuardBaselineMs;
  memcpy(out.autoToManualGuardSamplesDs, source.autoToManualGuardSamplesDs,
         sizeof(out.autoToManualGuardSamplesDs));
  out.scaleMacCacheMode = source.scaleMacCacheMode;
  out.bookooMuteOnBuzzerOnly = source.bookooMuteOnBuzzerOnly;
  out.bookooConnectBeepLevel = source.bookooConnectBeepLevel;
  out.reservedConfig3 = source.reservedConfig3;
}

void p33_schema_twenty_five_migrates_to_twenty_six() {
  resetHostPersistence();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  ensurePersistedPresetBank(current);
  finalizePersistedSettings(current);

  PersistedSettingsV25 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = CONFIG_SCHEMA_VERSION_V25;
  legacy.structureSize = sizeof(PersistedSettingsV25);
  legacy.storageRevision = 14;
  RuntimeConfigV25 runtimeV25 = {};
  fillRuntimeConfigV25FromCurrent(current.runtime, runtimeV25);
  runtimeV25.revision = 8;
  runtimeV25.goalWeightG = 40;
  legacy.runtime = runtimeV25;
  legacy.presets = current.presets;
  legacy.staConfigured = false;
  legacy.staOpen = false;
  legacy.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  legacy.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  legacy.lkgValid = false;
  memcpy(legacy.apPassword, current.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, current.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, current.authHash, sizeof(legacy.authHash));
  strcpy(legacy.preferredScaleMac, "AA:BB:CC:DD:EE:33");
  strcpy(legacy.preferredScaleName, "Lunar");
  legacy.checksum = persistedSettingsV25Checksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  bool migrated = false;
  CHECK(loadPersistedSettings(loaded, &migrated));
  CHECK(migrated);
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.runtime.goalWeightG == 40);
  CHECK(loaded.runtime.avoidBbwShotWithoutScale);
  CHECK(loaded.runtime.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
  CHECK(strcmp(loaded.preferredScaleMac, "AA:BB:CC:DD:EE:33") == 0);
}

void p24_preset_bank_size_and_crud_budgets() {
  CHECK(sizeof(ShotPreset) <= 128);
  CHECK(sizeof(ShotPresetBank) <= 1100);
  CHECK(sizeof(PersistedSettings) <= PERSISTED_SETTINGS_NVS_BUDGET);
  CHECK(sizeof(WebCommand) <= 512);
  CHECK(sizeof(PersistedSettings) != sizeof(PersistedSettingsV25));
  CHECK(sizeof(PersistedSettings) > sizeof(PersistedSettingsV19));

  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  CHECK(bank.count == 2);
  CHECK(bank.activeId == FACTORY_PRESET_ID_DOUBLE);
  CHECK(bank.presets[0].id == FACTORY_PRESET_ID_DOUBLE);
  CHECK(bank.presets[1].id == FACTORY_PRESET_ID_SINGLE);

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

struct TestCase {
  const char *id;
  void (*function)();
};

const TestCase tests[] = {
    {"P01", p01_defaults_are_valid_v16},
    {"P02", p02_newest_valid_slot_is_loaded},
    {"P02B", p02b_save_uses_ram_revision_when_slots_unreadable},
    {"P02C", p02c_overlay_live_runtime_is_saved_not_stale_blob},
    {"P03", p03_corrupt_newest_slot_falls_back},
    {"P04", p04_crc_and_semantic_validation_reject_corruption},
    {"P05", p05_password_change_updates_hash},
    {"P06", p06_schema_twelve_migrates_to_thirteen},
    {"P07", p07_invalid_schema_uses_factory_on_missing_slots},
    {"P08", p08_factory_reset_rebuilds_defaults},
    {"P09", p09_fast_extraction_guard_validation},
    {"P10", p10_auto_to_manual_guard_trend_and_validation},
    {"P11", p11_schema_thirteen_migrates_to_current},
    {"P12", p12_shot_log_persists_compact_blob},
    {"P13", p13_shot_log_migrates_v5_full_blob_to_compact},
    {"P14", p14_schema_fourteen_migrates_to_current},
    {"P15", p15_schema_fifteen_migrates_to_sixteen},
    {"P20", p20_schema_sixteen_migrates_to_seventeen},
    {"P21", p21_schema_seventeen_migrates_to_current},
    {"P22", p22_schema_eighteen_migrates_to_nineteen},
    {"P23", p23_schema_nineteen_migrates_to_twenty_with_preset_bank},
    {"P27", p27_schema_twenty_migrates_to_twenty_one},
    {"P28", p28_schema_twenty_one_migrates_to_twenty_two},
    {"P30", p30_schema_twenty_two_migrates_to_twenty_three},
    {"P31", p31_schema_twenty_three_migrates_to_twenty_four},
    {"P32", p32_schema_twenty_four_migrates_to_twenty_five},
    {"P33", p33_schema_twenty_five_migrates_to_twenty_six},
    {"P24", p24_preset_bank_size_and_crud_budgets},
    {"P25", p25_invalid_active_id_keeps_customs},
    {"P26", p26_save_candidate_validation_does_not_require_live_mutation},
    {"P16", p16_static_ip_address_validation},
    {"P17", p17_legacy_password_hash_still_verifies},
    {"P18", p18_shot_log_keeps_history_when_inactive_slot_write_fails},
    {"P19", p19_shot_log_weight_sentinel_allows_int16_max},
    {"P29", p29_last_shot_persists_and_clears},
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
