#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"

#include <cmath>
#include <cstdlib>
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

PersistedSettingsV3 makeSchemaThreeRecord(const PersistedSettings &source,
                                           uint32_t storageRevision) {
  PersistedSettingsV3 legacy = {};
  legacy.magic = PERSISTED_SETTINGS_MAGIC;
  legacy.schemaVersion = PREVIOUS_CONFIG_SCHEMA_VERSION;
  legacy.structureSize = sizeof(PersistedSettingsV3);
  legacy.storageRevision = storageRevision;
  legacy.runtime.revision = source.runtime.revision;
  legacy.runtime.goalWeightG = source.runtime.goalWeightG;
  legacy.runtime.weightOffsetG = source.runtime.weightOffsetG;
  legacy.runtime.autoTare = source.runtime.autoTare;
  legacy.runtime.timerOnly = source.runtime.timerOnly;
  legacy.runtime.canTareStartTimer = source.runtime.canTareStartTimer;
  legacy.runtime.brewConfirmationBeep = source.runtime.brewConfirmationBeep;
  legacy.runtime.rinseGestureMs = source.runtime.rinseGestureMs;
  legacy.runtime.rinseDurationMs = source.runtime.rinseDurationMs;
  legacy.runtime.brewConfirmMs = source.runtime.brewConfirmMs;
  legacy.runtime.minAutoStopMs = source.runtime.minAutoStopMs;
  legacy.runtime.operationalWallMs = source.runtime.operationalWallMs;
  legacy.staConfigured = source.staConfigured;
  legacy.staOpen = source.staOpen;
  memcpy(legacy.staSsid, source.staSsid, sizeof(legacy.staSsid));
  memcpy(legacy.staPassword, source.staPassword, sizeof(legacy.staPassword));
  memcpy(legacy.apPassword, source.apPassword, sizeof(legacy.apPassword));
  memcpy(legacy.authSalt, source.authSalt, sizeof(legacy.authSalt));
  memcpy(legacy.authHash, source.authHash, sizeof(legacy.authHash));
  legacy.checksum = persistedSettingsV3Checksum(legacy);
  return legacy;
}

void p01_valid_legacy_values_are_migrated() {
  persistence_host::reset();
  PersistedSettings settings;
  bool migrated = false;
  CHECK(initializeDefaultSettings(settings, 42, 17, &migrated));
  CHECK(migrated);
  CHECK(settings.runtime.goalWeightG == 42);
  CHECK(std::fabs(settings.runtime.weightOffsetG - 1.7f) < 0.001f);
  CHECK(validPersistedSettings(settings));
  CHECK(validAccessPointPassword(settings.apPassword));
  CHECK(verifyAdminPassword(settings, settings.apPassword));
}

void p02_invalid_legacy_values_use_safe_defaults() {
  persistence_host::reset();
  PersistedSettings settings;
  bool migrated = true;
  CHECK(initializeDefaultSettings(settings, 255, 255, &migrated));
  CHECK(!migrated);
  CHECK(settings.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(std::fabs(settings.runtime.weightOffsetG -
                  DEFAULT_WEIGHT_OFFSET_G) < 0.001f);
  CHECK(validPersistedSettings(settings));
}

void p03_newest_valid_slot_is_loaded() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  CHECK(savePersistedSettings(settings));
  const uint32_t firstRevision = settings.storageRevision;
  settings.runtime.goalWeightG = 47;
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision == firstRevision + 1);

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 47);
  CHECK(loaded.storageRevision == settings.storageRevision);
}

void p04_corrupt_newest_slot_falls_back() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  settings.runtime.goalWeightG = 40;
  CHECK(savePersistedSettings(settings));  // A, revision 1.
  settings.runtime.goalWeightG = 48;
  CHECK(savePersistedSettings(settings));  // B, revision 2.
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, SETTINGS_SLOT_B,
                                  offsetof(PersistedSettings, runtime)));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 40);
  CHECK(loaded.storageRevision == 1);
}

void p05_crc_and_semantic_validation_reject_corruption() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  settings.runtime.goalWeightG = 50;
  CHECK(!validPersistedSettings(settings));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  settings.runtime.operationalWallMs = HARD_MAX_CN9_CLOSED_MS + 1;
  finalizePersistedSettings(settings);
  CHECK(!validPersistedSettings(settings));
}

void p06_password_change_updates_hash_and_bounds() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  char generatedPassword[WIFI_PASSWORD_CAPACITY] = {};
  strcpy(generatedPassword, settings.apPassword);
  CHECK(!refreshAuthentication(settings, "1234"));
  CHECK(refreshAuthentication(settings, "NuevaClaveSegura"));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, "NuevaClaveSegura"));
  CHECK(!verifyAdminPassword(settings, generatedPassword));
}

void p07_revision_comparison_survives_wrap() {
  persistence_host::reset();
  PersistedSettings older;
  CHECK(initializeDefaultSettings(older, 255, 255));
  older.storageRevision = UINT32_MAX;
  older.runtime.goalWeightG = 41;
  finalizePersistedSettings(older);
  PersistedSettings newer = older;
  newer.storageRevision = 1;
  newer.runtime.goalWeightG = 43;
  finalizePersistedSettings(newer);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &older,
                           sizeof(older));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &newer,
                           sizeof(newer));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 43);
}

void p08_existing_nvs_wins_over_changed_legacy_bytes() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 35, 12));
  CHECK(savePersistedSettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 35);
  CHECK(std::fabs(loaded.runtime.weightOffsetG - 1.2f) < 0.001f);
}

void p09_failed_alternate_write_preserves_previous_slot() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  settings.runtime.goalWeightG = 44;
  CHECK(savePersistedSettings(settings));
  settings.runtime.goalWeightG = 55;
  persistence_host::failNextWrite = true;
  CHECK(!savePersistedSettings(settings));
  CHECK(settings.storageRevision == 1);
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 44);

  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision == 2);
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 55);
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, SETTINGS_SLOT_B,
                                  offsetof(PersistedSettings, checksum)));
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 44);
}

void p10_schema_two_defaults_to_brew_confirmation_beep() {
  persistence_host::reset();
  PersistedSettings legacy;
  CHECK(initializeDefaultSettings(legacy, 255, 255));
  legacy.schemaVersion = LEGACY_CONFIG_SCHEMA_VERSION;
  legacy.runtime.brewConfirmationBeep = false;
  legacy.checksum = 0;
  legacy.checksum = persistedSettingsChecksum(legacy);
  CHECK(validPersistedSettings(legacy));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.brewConfirmationBeep);
  CHECK(loaded.runtime.paddleReturnReminderBeep);
  CHECK(loaded.schemaVersion == LEGACY_CONFIG_SCHEMA_VERSION);
}

void p11_schema_three_defaults_to_paddle_return_reminder_beep() {
  persistence_host::reset();
  PersistedSettings legacy;
  CHECK(initializeDefaultSettings(legacy, 255, 255));
  legacy.schemaVersion = PREVIOUS_CONFIG_SCHEMA_VERSION;
  legacy.runtime.paddleReturnReminderBeep = false;
  legacy.checksum = 0;
  legacy.checksum = persistedSettingsChecksum(legacy);
  CHECK(validPersistedSettings(legacy));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacy,
                           sizeof(legacy));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.paddleReturnReminderBeep);
  CHECK(loaded.schemaVersion == PREVIOUS_CONFIG_SCHEMA_VERSION);
}

void p12_real_schema_three_layout_recovers_network_after_bad_upgrade() {
  persistence_host::reset();

  PersistedSettings previous;
  CHECK(initializeDefaultSettings(previous, 255, 255));
  previous.runtime.goalWeightG = 47;
  previous.staConfigured = true;
  strcpy(previous.staSsid, "RecoveredNetwork");
  strcpy(previous.staPassword, "RecoveredPassword");
  const PersistedSettingsV3 legacy = makeSchemaThreeRecord(previous, 1);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &legacy,
                           sizeof(legacy));

  // Reproduce the broken upgrade: a factory-default schema-4 record with the
  // same revision was written to A while the valid old record remained in B.
  PersistedSettings accidentalDefault;
  CHECK(initializeDefaultSettings(accidentalDefault, 255, 255));
  accidentalDefault.storageRevision = 1;
  finalizePersistedSettings(accidentalDefault);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A,
                           &accidentalDefault, sizeof(accidentalDefault));

  PersistedSettings recovered;
  CHECK(loadPersistedSettings(recovered));
  CHECK(recovered.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(recovered.structureSize == sizeof(PersistedSettings));
  CHECK(recovered.storageRevision == 1);
  CHECK(recovered.runtime.goalWeightG == 47);
  CHECK(recovered.runtime.paddleReturnReminderBeep);
  CHECK(recovered.staConfigured);
  CHECK(strcmp(recovered.staSsid, "RecoveredNetwork") == 0);
  CHECK(strcmp(recovered.staPassword, "RecoveredPassword") == 0);
  CHECK(verifyAdminPassword(recovered, previous.apPassword));

  CHECK(savePersistedSettings(recovered));
  CHECK(recovered.storageRevision == 2);
  PersistedSettings reloaded;
  CHECK(loadPersistedSettings(reloaded));
  CHECK(strcmp(reloaded.staSsid, "RecoveredNetwork") == 0);
}

void p13_factory_reset_erases_every_record_and_rebuilds_redundancy() {
  persistence_host::reset();
  EEPROM.write(LEGACY_WEIGHT_EEPROM_ADDRESS, 63);
  EEPROM.write(LEGACY_OFFSET_EEPROM_ADDRESS, 42);
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  settings.runtime.goalWeightG = 63;
  settings.runtime.weightOffsetG = 4.2f;
  settings.runtime.autoTare = false;
  settings.runtime.canTareStartTimer = false;
  settings.runtime.rinseGestureMs = 1800;
  settings.staConfigured = true;
  strcpy(settings.staSsid, "SavedNetwork");
  strcpy(settings.staPassword, "SavedPassword");
  CHECK(refreshAuthentication(settings, "CustomAdminPassword"));
  CHECK(savePersistedSettings(settings));
  CHECK(savePersistedSettings(settings));
  const uint32_t obsolete = 0xDEADBEEFU;
  persistence_host::putRaw(SETTINGS_NAMESPACE, "obsoleteRecord", &obsolete,
                           sizeof(obsolete));

  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(EEPROM.read(LEGACY_WEIGHT_EEPROM_ADDRESS) == ERASED_EEPROM_VALUE);
  CHECK(EEPROM.read(LEGACY_OFFSET_EEPROM_ADDRESS) == ERASED_EEPROM_VALUE);
  CHECK(persistence_host::records.count(
            persistence_host::storageKey(SETTINGS_NAMESPACE,
                                         "obsoleteRecord")) == 0);
  CHECK(persistence_host::records.at(
            persistence_host::storageKey(SETTINGS_NAMESPACE,
                                         SETTINGS_SLOT_A)).size() ==
        sizeof(PersistedSettings));
  CHECK(persistence_host::records.at(
            persistence_host::storageKey(SETTINGS_NAMESPACE,
                                         SETTINGS_SLOT_B)).size() ==
        sizeof(PersistedSettings));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.storageRevision == 2);
  CHECK(loaded.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(std::fabs(loaded.runtime.weightOffsetG - DEFAULT_WEIGHT_OFFSET_G) <
        0.001f);
  CHECK(loaded.runtime.autoTare);
  CHECK(loaded.runtime.rinseGestureMs == DEFAULT_RINSE_GESTURE_MS);
  CHECK(loaded.runtime.canTareStartTimer);
  CHECK(loaded.runtime.brewConfirmationBeep);
  CHECK(loaded.runtime.paddleReturnReminderBeep);
  CHECK(!loaded.staConfigured);
  CHECK(!loaded.staOpen);
  CHECK(loaded.staSsid[0] == '\0');
  CHECK(loaded.staPassword[0] == '\0');
  CHECK(validAccessPointPassword(loaded.apPassword));
  CHECK(verifyAdminPassword(loaded, settings.apPassword));
  CHECK(!verifyAdminPassword(loaded, "CustomAdminPassword"));

  // If the newest factory slot is later corrupted, the other copy must still
  // contain factory defaults rather than resurrecting the old configuration.
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, SETTINGS_SLOT_B,
                                  offsetof(PersistedSettings, checksum)));
  PersistedSettings fallback;
  CHECK(loadPersistedSettings(fallback));
  CHECK(fallback.storageRevision == 1);
  CHECK(fallback.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(!fallback.staConfigured);
  CHECK(strcmp(fallback.apPassword, loaded.apPassword) == 0);
  CHECK(verifyAdminPassword(fallback, loaded.apPassword));
}

void p14_factory_reset_survives_one_failed_redundant_write() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  settings.runtime.goalWeightG = 71;
  settings.staConfigured = true;
  strcpy(settings.staSsid, "OldNetwork");
  strcpy(settings.staPassword, "OldPassword");
  CHECK(savePersistedSettings(settings));

  // The first put after namespace clear fails; the second redundant slot must
  // still make the reset durable and must not retain the old slot A record.
  persistence_host::failNextWrite = true;
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(persistence_host::records.count(
            persistence_host::storageKey(SETTINGS_NAMESPACE,
                                         SETTINGS_SLOT_A)) == 0);
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.storageRevision == 2);
  CHECK(loaded.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(!loaded.staConfigured);
  CHECK(verifyAdminPassword(loaded, settings.apPassword));
}

void p15_normal_save_requires_verified_readback() {
  persistence_host::reset();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings, 255, 255));
  const uint32_t originalRevision = settings.storageRevision;
  persistence_host::corruptNextWrite = true;
  CHECK(!savePersistedSettings(settings));
  CHECK(settings.storageRevision == originalRevision);

  PersistedSettings invalid;
  CHECK(!loadPersistedSettings(invalid));
  CHECK(savePersistedSettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(validPersistedSettings(loaded));
}

void p16_provisioning_credentials_are_unique_per_generation() {
  persistence_host::reset();
  PersistedSettings first;
  PersistedSettings second;
  CHECK(initializeDefaultSettings(first, 255, 255));
  CHECK(initializeDefaultSettings(second, 255, 255));
  CHECK(validAccessPointPassword(first.apPassword));
  CHECK(validAccessPointPassword(second.apPassword));
  CHECK(strcmp(first.apPassword, second.apPassword) != 0);
  CHECK(verifyAdminPassword(first, first.apPassword));
  CHECK(verifyAdminPassword(second, second.apPassword));
}

struct TestCase {
  const char *id;
  void (*function)();
};

const TestCase tests[] = {
    {"P01", p01_valid_legacy_values_are_migrated},
    {"P02", p02_invalid_legacy_values_use_safe_defaults},
    {"P03", p03_newest_valid_slot_is_loaded},
    {"P04", p04_corrupt_newest_slot_falls_back},
    {"P05", p05_crc_and_semantic_validation_reject_corruption},
    {"P06", p06_password_change_updates_hash_and_bounds},
    {"P07", p07_revision_comparison_survives_wrap},
    {"P08", p08_existing_nvs_wins_over_changed_legacy_bytes},
    {"P09", p09_failed_alternate_write_preserves_previous_slot},
    {"P10", p10_schema_two_defaults_to_brew_confirmation_beep},
    {"P11", p11_schema_three_defaults_to_paddle_return_reminder_beep},
    {"P12", p12_real_schema_three_layout_recovers_network_after_bad_upgrade},
    {"P13", p13_factory_reset_erases_every_record_and_rebuilds_redundancy},
    {"P14", p14_factory_reset_survives_one_failed_redundant_write},
    {"P15", p15_normal_save_requires_verified_readback},
    {"P16", p16_provisioning_credentials_are_unique_per_generation},
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
