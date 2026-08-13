#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"
#include "../ShotStopperShotLog.h"

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
  persistence_host::reset();
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
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(settings.runtime.autoToManualGuardSamplesDs[i] ==
          AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS);
  }
  CHECK(validPersistedSettings(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(verifyAdminPassword(settings, DEFAULT_AP_PASSWORD));
}

void p02_newest_valid_slot_is_loaded() {
  persistence_host::reset();
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

void p03_corrupt_newest_slot_falls_back() {
  persistence_host::reset();
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
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 50;
  settings.runtime.maxRecoveryWeightG = 58.0f;
  CHECK(!validPersistedSettings(settings));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
}

void p05_password_change_updates_hash() {
  persistence_host::reset();
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
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(calculatePasswordHashLegacy(settings.authSalt, settings.apPassword,
                                    settings.authHash));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, DEFAULT_AP_PASSWORD));
}

void p18_shot_log_keeps_history_when_inactive_slot_write_fails() {
  persistence_host::reset();
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

void p06_schema_twelve_migrates_to_thirteen() {
  persistence_host::reset();
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
  persistence_host::reset();
  PersistedSettings loaded;
  CHECK(!loadPersistedSettings(loaded));
}

void p08_factory_reset_rebuilds_defaults() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 63;
  settings.runtime.maxRecoveryWeightG = 70.0f;
  CHECK(savePersistedSettings(settings));
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(settings.runtime.fastExtractionGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardEnabled);
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

  uint16_t seeded[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {};
  resetAutoToManualGuardSamples(seeded, 28000);
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(seeded[i] == 280);
  }
}

void p11_schema_thirteen_migrates_to_current() {
  persistence_host::reset();
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
  persistence_host::reset();
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
  persistence_host::reset();
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
  persistence_host::reset();
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
  persistence_host::reset();
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
  CHECK(loaded.runtime.shotTimerStartDelayMs ==
        DEFAULT_SHOT_TIMER_START_DELAY_MS);
  CHECK(strcmp(loaded.staSsid, "ShopWiFi") == 0);
}

void p20_schema_sixteen_migrates_to_seventeen() {
  persistence_host::reset();
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
  CHECK(loaded.runtime.shotTimerStartDelayMs ==
        DEFAULT_SHOT_TIMER_START_DELAY_MS);
  CHECK(strcmp(loaded.staSsid, "CafeWiFi") == 0);
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
    {"P16", p16_static_ip_address_validation},
    {"P17", p17_legacy_password_hash_still_verifies},
    {"P18", p18_shot_log_keeps_history_when_inactive_slot_write_fails},
    {"P19", p19_shot_log_weight_sentinel_allows_int16_max},
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
