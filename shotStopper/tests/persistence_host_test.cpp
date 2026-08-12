#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"

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
  legacy.runtime.brewConfirmationBeep = source.runtime.brewConfirmationBeep;
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
  legacy.runtime.confirmationTimeoutMs = source.runtime.confirmationTimeoutMs;
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

void p01_defaults_are_valid_v14() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(!settings.runtime.fastExtractionGuardEnabled);
  CHECK(std::fabs(settings.runtime.maxRecoveryWeightG -
                  DEFAULT_MAX_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(settings.runtime.minBrewTimeMs == DEFAULT_MIN_BREW_TIME_MS);
  CHECK(settings.runtime.autoToManualGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardLimitMode ==
        static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO));
  CHECK(settings.runtime.autoToManualGuardManualLimitMs ==
        DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS);
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(settings.runtime.autoToManualGuardSamplesDs[i] ==
          AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS);
  }
  CHECK(validPersistedSettings(settings));
}

void p02_newest_valid_slot_is_loaded() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(savePersistedSettings(settings));
  const uint32_t firstRevision = settings.storageRevision;
  settings.runtime.goalWeightG = 47;
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
  CHECK(!validPersistedSettings(settings));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
}

void p05_password_change_updates_hash() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(refreshAuthentication(settings, "NuevaClaveSegura"));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, "NuevaClaveSegura"));
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
  CHECK(savePersistedSettings(settings));
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(!settings.runtime.fastExtractionGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardEnabled);
}

void p09_fast_extraction_guard_validation() {
  RuntimeConfig config = {};
  config.goalWeightG = 36;
  config.maxRecoveryWeightG = 42.5f;
  config.minBrewTimeMs = 26000;
  config.fastExtractionGuardEnabled = true;
  config.confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
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
  config.autoToManualGuardLimitMode = 9;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE);
}

void p11_schema_thirteen_migrates_to_fourteen() {
  persistence_host::reset();
  PersistedSettings current;
  CHECK(initializeDefaultSettings(current));
  PersistedSettingsV13 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = PREVIOUS_CONFIG_SCHEMA_VERSION;
  legacy.structureSize = sizeof(PersistedSettingsV13);
  legacy.storageRevision = 3;
  legacy.runtime.revision = current.runtime.revision;
  legacy.runtime.goalWeightG = 41;
  legacy.runtime.weightOffsetG = 2.0f;
  legacy.runtime.autoTare = true;
  legacy.runtime.timerOnly = false;
  legacy.runtime.canTareStartTimer = true;
  legacy.runtime.brewConfirmationBeep = true;
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
  legacy.runtime.confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
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
}

struct TestCase {
  const char *id;
  void (*function)();
};

const TestCase tests[] = {
    {"P01", p01_defaults_are_valid_v14},
    {"P02", p02_newest_valid_slot_is_loaded},
    {"P03", p03_corrupt_newest_slot_falls_back},
    {"P04", p04_crc_and_semantic_validation_reject_corruption},
    {"P05", p05_password_change_updates_hash},
    {"P06", p06_schema_twelve_migrates_to_thirteen},
    {"P07", p07_invalid_schema_uses_factory_on_missing_slots},
    {"P08", p08_factory_reset_rebuilds_defaults},
    {"P09", p09_fast_extraction_guard_validation},
    {"P10", p10_auto_to_manual_guard_trend_and_validation},
    {"P11", p11_schema_thirteen_migrates_to_fourteen},
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
