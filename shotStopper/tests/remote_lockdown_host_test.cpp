#define SHOT_STOPPER_HOST_TEST
#define ARDUINO_ESP32S3_DEV
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 0
#ifndef SHOT_STOPPER_ENABLE_BUZZER
#define SHOT_STOPPER_ENABLE_BUZZER 1
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "../shotStopper.cpp"

namespace {

int failures = 0;
int testsRun = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__ << ": check failed: "       \
                << #condition << "\n";                                         \
      ++failures;                                                              \
      return;                                                                  \
    }                                                                          \
  } while (false)

static_assert(!shotstopper::REMOTE_MACHINE_CONTROL_ENABLED,
              "Lockdown host tests must compile with remote control off");

void deleteHostResources() {
  delete scaleCommandQueue;
  delete scaleEventQueue;
  delete webCommandQueue;
  delete bleCompanionRequestQueue;
  delete bleCompanionResultQueue;
  delete relaySafetyTimer;
  delete operationalLimitTimer;
  scaleCommandQueue = nullptr;
  scaleEventQueue = nullptr;
  webCommandQueue = nullptr;
  bleCompanionRequestQueue = nullptr;
  bleCompanionResultQueue = nullptr;
  relaySafetyTimer = nullptr;
  operationalLimitTimer = nullptr;
  independentSafetyTimer.resetForHost();
}

void resetHarness() {
  deleteHostResources();

  hostMillis = 0;
  bootStartedAtMs = 0;
  hostPinLevel.fill(HIGH);
  hostPinMode.fill(0);
  hostTrackedRelayPin = RELAY_GPIO;
  hostTrackedRelayOpenLevel = RELAY_OPEN_LEVEL;
  hostTrackedRelayClosedLevel = RELAY_CLOSED_LEVEL;
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
  hostEspTimerCreateSucceeds = true;
  hostEspTimerStartSucceeds = true;
  hostGptimerCreateSucceeds = true;
  hostGptimerArmSucceeds = true;
  hostCircuitArmBeforeCommitHook = nullptr;
  hostTaskWatchdogOperationsSucceed = true;
  hostTaskWatchdogConfigured = false;
  hostTaskWatchdogSubscriptions = 0;
  hostTaskWatchdogFeeds = 0;
  resetSafetyResetGuardForHost();

  stopperState = StopperState::REQUIRES_OFF;
  shot = ShotTrajectory{};
  session = CycleSession{};
  resetCupPresence();
  pendingFinalize = PendingShotFinalize{};
  pendingScaleTimerStop = PendingScaleTimerStop{};
  runtimeConfig = RuntimeConfig{};
  runtimeConfig.rinseEnabled = true;
  runtimeConfig.noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
  runtimeConfig.requireCupToStart = false;
  noScaleShotGuardArmed = false;
  noScaleShotGuardHold = false;
  cupStartGuardHold = false;
  virtualHoldOn = false;
  debugLog.clear();

  scale = EspressoScaleBLE(DEBUG);
  scale.connected = false;
  resetScaleWorkerRadioStateForHost();
  updateWorkerLinkState();

  rawActivatorOn = false;
  activatorOn = false;
  activatorTurnedOn = false;
  activatorTurnedOff = false;
  rawActivatorChangedAtMs = 0;
  machineEndCycle();
  rinseClear();
  circuitClosed = false;
  relaySafetyTripped = false;
  operationalLimitTripped = false;
  circuitClosedAtMs = 0;
  operationalLimitAtArmMs = HARD_MAX_CIRCUIT_CLOSED_MS;
  relaySafetyState = RelaySafetyState::OPEN;
  relaySafetyFault = RelaySafetyFault::NONE;
  relaySafetyGeneration = 0;
  relaySafetyTimersReady = false;
  taskWatchdogReady = configureTaskWatchdog() &&
                      subscribeCurrentTaskToWatchdog();
  criticalTaskWatchdogFault = false;
  platformClockReady = true;
  persistenceReady = true;
  bleStackReady = true;
  firmwareInitializationComplete = true;

  scaleCommandQueue =
      xQueueCreate(SCALE_COMMAND_QUEUE_LENGTH, sizeof(ScaleCommand));
  scaleEventQueue =
      xQueueCreate(SCALE_EVENT_QUEUE_LENGTH, sizeof(ScaleEvent));
  webCommandQueue =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  bleCompanionRequestQueue =
      xQueueCreate(BLE_COMPANION_REQUEST_QUEUE_LENGTH,
                   sizeof(BleCompanionRequest));
  bleCompanionResultQueue =
      xQueueCreate(BLE_COMPANION_RESULT_QUEUE_LENGTH,
                   sizeof(BleCompanionResult));
  CHECK(scaleCommandQueue != nullptr);
  CHECK(scaleEventQueue != nullptr);
  CHECK(webCommandQueue != nullptr);
  CHECK(bleCompanionRequestQueue != nullptr);
  CHECK(bleCompanionResultQueue != nullptr);
  CHECK(initializeRelaySafetyTimer());
  relaySafetyTimersReady = true;

  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  hostPinLevel[ACTIVATOR_GPIO] = !ACTIVATOR_ACTIVE_LEVEL;
  initializeActivatorInput();
  localBuzzer.begin(BUZZER_GPIO);
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
  seedDefaultShotPresetBank(presetBank);
  ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                       runtimeConfig.autoRetare);
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
  runtimeConfig.requireCupToStart = false;
  mutableActiveShotPreset(presetBank).requireCupToStart = false;
  publishRecipeState();
}

void runLoopAfter(uint32_t deltaMs) {
  hostMillis += deltaMs;
  hostServiceEspTimer(relaySafetyTimer);
  hostServiceEspTimer(operationalLimitTimer);
  hostServiceEspTimer(localBuzzer.phaseTimer);
  independentSafetyTimer.serviceForHost();
  loop();
}

void setRawPaddle(bool on) {
  hostPinLevel[ACTIVATOR_GPIO] = on ? ACTIVATOR_ACTIVE_LEVEL
                                    : !ACTIVATOR_ACTIVE_LEVEL;
  loop();
}

void reachReadyFromBoot() {
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
}

WebCommand webControlCommand(WebCommandType type) {
  WebCommand command;
  command.type = type;
  command.requestId = 1;
  command.unsafeWebUiOverride = true;
  return command;
}

void remote_on_does_not_close_circuit() {
  CHECK(!REMOTE_MACHINE_CONTROL_ENABLED);
  resetHarness();
  reachReadyFromBoot();
  const size_t closedBefore = hostRelayClosedWrites;
  processWebCommand(webControlCommand(WebCommandType::REMOTE_ON));
  CHECK(!session.active);
  CHECK(!virtualHoldOn);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedBefore);
  CHECK(stopperState == StopperState::READY);
}

void web_rinse_does_not_close_circuit() {
  resetHarness();
  reachReadyFromBoot();
  CHECK(machineSupportsRinse());
  const size_t closedBefore = hostRelayClosedWrites;
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedBefore);
  CHECK(stopperState == StopperState::READY);
}

void physical_paddle_still_closes_circuit() {
  resetHarness();
  reachReadyFromBoot();
  setRawPaddle(true);
  CHECK(stopperState == StopperState::READY);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(session.active);
  CHECK(session.source == ControlSource::PHYSICAL);
  CHECK(stopperState == StopperState::BREW ||
        stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
}

using TestFunction = void (*)();

struct TestCase {
  const char *id;
  TestFunction function;
};

const TestCase testCases[] = {
    {"RL01", remote_on_does_not_close_circuit},
    {"RL02", web_rinse_does_not_close_circuit},
    {"RL03", physical_paddle_still_closes_circuit},
};

}  // namespace

int main() {
  for (const TestCase &test : testCases) {
    const int failuresBefore = failures;
    test.function();
    ++testsRun;
    std::cout << test.id << (failures == failuresBefore ? " PASS" : " FAIL")
              << "\n";
  }

  deleteHostResources();
  std::cout << testsRun << " tests, " << failures << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
