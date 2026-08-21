#define SHOT_STOPPER_HOST_TEST

#include "../ShotStopperJsonArena.h"

#include <cJSON.h>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using shotstopper::initJsonArenaHooks;
using shotstopper::jsonArenaBytesUsed;
using shotstopper::resetJsonArena;

int failures = 0;

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      std::cerr << __func__ << ":" << __LINE__                        \
                << ": check failed: " << #condition << "\n";          \
      ++failures;                                                     \
      return;                                                         \
    }                                                                 \
  } while (false)

void testArenaResetAndHooks() {
  initJsonArenaHooks();
  resetJsonArena();
  CHECK(jsonArenaBytesUsed() == 0);
  void *block = shotstopper::detail::jsonArenaMalloc(64);
  CHECK(block != nullptr);
  CHECK(jsonArenaBytesUsed() >= 64);
  resetJsonArena();
  CHECK(jsonArenaBytesUsed() == 0);
}

void testConfigPatchWorstCaseFitsArena() {
  initJsonArenaHooks();
  // Mirrors every allowed field in configHandler() with representative values.
  const char *body =
      "{"
      "\"baseRevision\":42,"
      "\"goalWeightG\":18,"
      "\"rinseGestureMs\":3000,"
      "\"rinseDurationMs\":6000,"
      "\"operationalWallMs\":120000,"
      "\"autoTare\":true,"
      "\"postTareBaselineGraceMs\":500,"
      "\"brewByWeight\":true,"
      "\"canTareStartTimer\":false,"
      "\"scaleTimerStopExtraDelayMs\":250,"
      "\"dripDelayMs\":3000,"
      "\"soundAlertsEnabled\":true,"
      "\"firstDropBeep\":true,"
      "\"scaleConnectedLed\":true,"
      "\"paddleReturnReminderBeep\":false,"
      "\"paddleReturnReminderIntervalMs\":15000,"
      "\"paddleReturnReminderMaxDurationMs\":60000,"
      "\"paddleMode\":1,"
      "\"buzzerScaleLostBeep\":true,"
      "\"buzzerAutoToManualGuardEndBeep\":false,"
      "\"buzzerManualNoScaleBeep\":true,"
      "\"buzzerScaleConnectedBeep\":true,"
      "\"buzzerExtendedPulseRate\":2,"
      "\"buzzerSlowExtendedPulseRate\":1,"
      "\"alertOutputChannel\":0,"
      "\"autoRetare\":true,"
      "\"retareWindowMs\":8000,"
      "\"minimumCupWeightG\":5,"
      "\"retareStabilitySamples\":4,"
      "\"retareStabilityToleranceG\":0.2,"
      "\"retareStabilityMaxGapMs\":500,"
      "\"retareStabilityMinDurationMs\":800,"
      "\"bbwProtectionMs\":3000,"
      "\"fastExtractionGuardEnabled\":true,"
      "\"maxRecoveryWeightG\":2.5,"
      "\"minBrewTimeMs\":18000,"
      "\"slowExtractionGuardEnabled\":true,"
      "\"minRecoveryWeightG\":1.5,"
      "\"maxBrewTimeMs\":45000,"
      "\"autoToManualGuardEnabled\":true,"
      "\"autoToManualGuardLimitMode\":1,"
      "\"autoToManualGuardManualLimitMs\":60000,"
      "\"autoToManualGuardBaselineMs\":30000,"
      "\"weightOffsetBaselineG\":0.1,"
      "\"timezoneOffsetMinutes\":-240,"
      "\"ntpServerPreset\":1,"
      "\"ntpServerCustom\":\"pool.ntp.org\","
      "\"scaleMacCacheMode\":1,"
      "\"bookooMuteOnBuzzerOnly\":false,"
      "\"bookooConnectBeepLevel\":2,"
      "\"avoidBbwShotWithoutScale\":true,"
      "\"lastShotCooldownMs\":5000,"
      "\"serialDebugOutput\":false,"
      "\"ringRetainLogLevel\":2"
      "}";

  resetJsonArena();
  cJSON *root = cJSON_Parse(body);
  CHECK(root != nullptr);
  CHECK(jsonArenaBytesUsed() <= shotstopper::JSON_ARENA_CAPACITY);
  CHECK(jsonArenaBytesUsed() > 0);
  cJSON_Delete(root);
  resetJsonArena();
  CHECK(jsonArenaBytesUsed() == 0);
}

void testRepeatedParsesReuseArena() {
  initJsonArenaHooks();
  const char *body = "{\"confirm\":\"UNSAFE_WEBUI_OVERRIDE\"}";
  for (int attempt = 0; attempt < 32; ++attempt) {
    resetJsonArena();
    cJSON *root = cJSON_Parse(body);
    CHECK(root != nullptr);
    cJSON_Delete(root);
    resetJsonArena();
    CHECK(jsonArenaBytesUsed() == 0);
  }
}

void testArenaRejectsOversizedAlloc() {
  initJsonArenaHooks();
  resetJsonArena();
  const uint32_t before = shotstopper::jsonArenaAllocFailures();
  CHECK(shotstopper::detail::jsonArenaMalloc(
            shotstopper::JSON_ARENA_CAPACITY + 1) == nullptr);
  CHECK(shotstopper::jsonArenaAllocFailures() == before + 1);
  CHECK(jsonArenaBytesUsed() == 0);
  CHECK(shotstopper::detail::jsonArenaMalloc(
            static_cast<size_t>(-1)) == nullptr);
  CHECK(shotstopper::jsonArenaAllocFailures() == before + 2);
  CHECK(jsonArenaBytesUsed() == 0);
  void *ok = shotstopper::detail::jsonArenaMalloc(64);
  CHECK(ok != nullptr);
  CHECK(shotstopper::detail::jsonArenaMalloc(
            shotstopper::JSON_ARENA_CAPACITY) == nullptr);
  CHECK(jsonArenaBytesUsed() >= 64);
}

}  // namespace

int main() {
  testArenaResetAndHooks();
  testConfigPatchWorstCaseFitsArena();
  testRepeatedParsesReuseArena();
  testArenaRejectsOversizedAlloc();
  if (failures != 0) {
    std::cerr << failures << " json arena host test(s) failed\n";
    return 1;
  }
  std::cout << "json arena host tests passed\n";
  return 0;
}
