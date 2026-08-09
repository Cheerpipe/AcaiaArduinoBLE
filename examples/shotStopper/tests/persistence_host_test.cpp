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

void p01_valid_legacy_values_are_migrated() {
  persistence_host::reset();
  PersistedSettings settings;
  bool migrated = false;
  CHECK(initializeDefaultSettings(settings, 42, 17, &migrated));
  CHECK(migrated);
  CHECK(settings.runtime.goalWeightG == 42);
  CHECK(std::fabs(settings.runtime.weightOffsetG - 1.7f) < 0.001f);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, DEFAULT_AP_PASSWORD));
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
  CHECK(!refreshAuthentication(settings, "1234"));
  CHECK(refreshAuthentication(settings, "NuevaClaveSegura"));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(verifyAdminPassword(settings, "NuevaClaveSegura"));
  CHECK(!verifyAdminPassword(settings, DEFAULT_AP_PASSWORD));
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
  CHECK(loaded.schemaVersion == LEGACY_CONFIG_SCHEMA_VERSION);
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
