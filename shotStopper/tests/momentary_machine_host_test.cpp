#define SHOT_STOPPER_HOST_TEST
#define ARDUINO_ESP32S3_DEV
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 1
#ifndef SHOT_STOPPER_ENABLE_BUZZER
#define SHOT_STOPPER_ENABLE_BUZZER 1
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
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

void resetMomentaryHarness() {
  deleteHostResources();
  Serial.reset();
  resetSerialCliState();
  hostMillis = 0;
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
  hostTaskWatchdogOperationsSucceed = true;
  EEPROM.beginSucceeds = true;
  BLE.beginSucceeds = true;
  resetSafetyResetGuardForHost();

  stopperState = StopperState::REQUIRES_OFF;
  shot = ShotTrajectory{};
  session = CycleSession{};
  resetCupPresence();
  runtimeConfig = RuntimeConfig{};
  runtimeConfig.requireCupToStart = false;
  runtimeConfig.avoidBbwShotWithoutScale = false;
  runtimeConfig.fastExtractionGuardEnabled = false;
  runtimeConfig.slowExtractionGuardEnabled = false;
  platformClockReady = true;
  persistenceReady = true;
  bleStackReady = true;
  firmwareInitializationComplete = true;
  circuitClosed = false;
  relaySafetyTripped = false;
  operationalLimitTripped = false;
  circuitClosedAtMs = 0;
  operationalLimitAtArmMs = HARD_MAX_CIRCUIT_CLOSED_MS;
  relaySafetyState = RelaySafetyState::OPEN;
  relaySafetyFault = RelaySafetyFault::NONE;
  relaySafetyGeneration = 0;
  criticalTaskWatchdogFault = false;
  paddleOn = false;
  rawPaddleOn = false;
  paddleTurnedOn = false;
  paddleTurnedOff = false;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = 0;
  currentWeightSequence = 0;
  scale.connected = true;
  setScaleLinkState(ScaleLinkState::CONNECTED);

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
  CHECK(initializeRelaySafetyTimer());
  relaySafetyTimersReady = true;
  taskWatchdogReady = configureTaskWatchdog() &&
                      subscribeCurrentTaskToWatchdog();
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  hostPinLevel[PADDLE_GPIO] = !PADDLE_ACTIVE_LEVEL;
#if SHOT_STOPPER_MACHINE_TYPE == 2
  hostPinLevel[REED_GPIO] = !REED_ACTIVE_LEVEL;
#endif
  initializePaddleInput();
  machineReleasePhysicalSwitchToBrew();
#if SHOT_STOPPER_MACHINE_TYPE == 1
  momentaryInferredState = MachineRunState::CONFIRMED_OFF;
  momentarySawScale = false;
  momentaryEspressoConfirmed = false;
  momentaryStopAwaitingAck = false;
  momentaryStopRetryPending = false;
  momentaryStartAwaitingAck = false;
  momentaryStartBaselineSinceMs = 0;
  momentaryOrphanRun = false;
  momentaryStaleSinceMs = 0;
  momentaryLowFlowSinceMs = 0;
  momentaryRisingSinceMs = 0;
  momentaryQuietSinceMs = 0;
  momentaryLogicalRunActive = false;
  momentaryLogicalRunStartedAtMs = 0;
#endif
#if SHOT_STOPPER_MACHINE_TYPE == 2
  reedRawOn = false;
  reedOn = false;
  reedSawStableOff = false;
  reedChangedAtMs = millis();
  momentaryFirmwareStopIssued = false;
#endif
  localBuzzer.begin(BUZZER_GPIO);
  seedDefaultShotPresetBank(presetBank);
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
  runtimeConfig.requireCupToStart = false;
  runtimeConfig.avoidBbwShotWithoutScale = false;
  mutableActiveShotPreset(presetBank).requireCupToStart = false;
  hostRelayClosedWrites = 0;
  hostRelayOpenWrites = 0;
}

void runLoopAfter(uint32_t deltaMs) {
  hostMillis += deltaMs;
  hostServiceEspTimer(relaySafetyTimer);
  hostServiceEspTimer(operationalLimitTimer);
  independentSafetyTimer.serviceForHost();
  loop();
}

void setRawPaddle(bool on) {
  hostPinLevel[PADDLE_GPIO] = on ? PADDLE_ACTIVE_LEVEL
                                : !PADDLE_ACTIVE_LEVEL;
  loop();
}

void shortPress(uint32_t heldMs) {
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  runLoopAfter(heldMs);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
}

void pressDown() {
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
}

void releaseUp() {
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
}

#if SHOT_STOPPER_MACHINE_TYPE == 2
void setRawReed(bool on) {
  hostPinLevel[REED_GPIO] = on ? REED_ACTIVE_LEVEL : !REED_ACTIVE_LEVEL;
  loop();
  runLoopAfter(REED_DEBOUNCE_MS + 1);
}
#endif

void t_short_press_starts_without_latching_relay() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  CHECK(stopperState == StopperState::READY);
  const size_t closedBefore = hostRelayClosedWrites;
  shortPress(200);
  CHECK(session.active);
  CHECK(hostRelayClosedWrites > closedBefore);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  CHECK(!getRelaySafetySnapshot().closed);
#if SHOT_STOPPER_MACHINE_TYPE == 1
  CHECK(machineIsRunning());
#else
  CHECK(session.active);
#endif
}

void t_default_starts_on_release_not_press() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  CHECK(!runtimeConfig.momentaryStartOnPress);
  pressDown();
  CHECK(!session.active);
  CHECK(stopperState == StopperState::READY);
  releaseUp();
  CHECK(session.active);
}

void t_press_edge_starts_on_press_and_release_does_not_stop() {
  resetMomentaryHarness();
  runtimeConfig.momentaryStartOnPress = true;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(session.active);
  CHECK(stopperState != StopperState::RINSE);
  releaseUp();
  CHECK(session.active);
  CHECK(paddleOn);
  CHECK(stopperState != StopperState::RINSE);
}

void t_press_edge_later_click_stops() {
  resetMomentaryHarness();
  runtimeConfig.momentaryStartOnPress = true;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(session.active);
  releaseUp();
  runLoopAfter(runtimeConfig.rinseGestureMs + 20);
  shortPress(180);
  CHECK(!paddleOn);
}

void t_guard_reject_does_not_pulse() {
  resetMomentaryHarness();
  runtimeConfig.cupProtectionEnabled = true;
  runtimeConfig.requireCupToStart = true;
  mutableActiveShotPreset(presetBank).cupProtectionEnabled = true;
  mutableActiveShotPreset(presetBank).requireCupToStart = true;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  const size_t closedBefore = hostRelayClosedWrites;
  shortPress(180);
  CHECK(!session.active);
  CHECK(!paddleOn);
  CHECK(hostRelayClosedWrites == closedBefore);
}

void t_long_press_mirrors_without_brew() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  runLoopAfter(COMPILED_MAX_SINGLE_PRESS_MS + 20);
  CHECK(!session.active);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
}

#if SHOT_STOPPER_MACHINE_TYPE == 1
void t_only_auto_cut_needs_confirmed_on() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  const size_t closedQuiet = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedQuiet);

  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    currentWeight += 0.45f;
    currentWeightReceivedAtMs = hostMillis;
    ++currentWeightSequence;
    runLoopAfter(100);
  }
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  const size_t closedAtConfirm = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites > closedAtConfirm);
}

void t_quiet_pan_does_not_force_cut_when_unknown() {
  resetMomentaryHarness();
  runtimeConfig.avoidBbwShotWithoutScale = false;
  noScaleShotGuardArmed = false;
  scale.connected = false;
  setScaleLinkState(ScaleLinkState::DISCONNECTED);
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  const size_t closedAtStart = hostRelayClosedWrites;
  runLoopAfter(1200);
  CHECK(machineRequestStop());
  (void)closedAtStart;
}
#endif

#if SHOT_STOPPER_MACHINE_TYPE == 2
void t_reed_off_blocks_firmware_cut() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  CHECK(!reedIsOn());
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedBefore);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t_reed_on_allows_firmware_cut() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  setRawReed(true);
  CHECK(reedIsOn());
  CHECK(machineIsRunning());
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites > closedBefore);
}
#endif

#if SHOT_STOPPER_MACHINE_TYPE == 1
void t_tare_rebases_flow_signature() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    currentWeight += 0.45f;
    currentWeightReceivedAtMs = hostMillis;
    ++currentWeightSequence;
    runLoopAfter(100);
  }
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  ++currentWeightSequence;
  runLoopAfter(50);
  CHECK(machineRunState() != MachineRunState::CONFIRMED_ON);
  const size_t closedQuiet = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedQuiet);
}

void t_user_stop_pulses_when_assumed_on() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  CHECK(machineIsRunning());
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  momentaryUserStopThisCycle = true;
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites > closedBefore);
}

void t_wall_with_quiet_pan_does_not_auto_pulse() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 800;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  const size_t closedAtStart = hostRelayClosedWrites;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  runLoopAfter(900);
  CHECK(!momentaryLogicalRunActive || getRelaySafetySnapshot().operationalTripped ||
        operationalLimitTripped);
  CHECK(hostRelayClosedWrites == closedAtStart);
  CHECK(machineIsRunning());
  CHECK(!controlAllowsConfigurationNow());
}

void t_settings_lock_follows_logical_run() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  CHECK(controlAllowsConfigurationNow());
  shortPress(150);
  CHECK(machineIsRunning());
  CHECK(!controlAllowsConfigurationNow());
}

void t_polarity_resyncs_to_running_machine() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  CHECK(paddleOn);
  paddleOn = false;
  CHECK(machineIsRunning());
  shortPress(180);
  CHECK(!momentaryStartEdgeThisCycle);
  CHECK(momentaryUserStopThisCycle || !paddleOn);
}

void t_coalesce_aborts_start_pulse() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(getRelaySafetySnapshot().closed);
  momentaryUserStopThisCycle = true;
  CHECK(machineRequestStop());
  CHECK(!getRelaySafetySnapshot().closed);
}
#endif

#if SHOT_STOPPER_MACHINE_TYPE == 2
void t_reed_boot_on_is_unknown() {
  resetMomentaryHarness();
  hostPinLevel[REED_GPIO] = REED_ACTIVE_LEVEL;
  reedRawOn = false;
  reedOn = false;
  reedSawStableOff = false;
  reedChangedAtMs = millis();
  loop();
  runLoopAfter(REED_DEBOUNCE_MS + 1);
  CHECK(machineRunState() == MachineRunState::UNKNOWN);
  CHECK(machineIsRunning());
  momentaryUserStopThisCycle = true;
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedBefore);
}

void t_reed_off_wins_over_rising_weight() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    currentWeight += 0.45f;
    currentWeightReceivedAtMs = hostMillis;
    ++currentWeightSequence;
    runLoopAfter(100);
  }
  CHECK(!reedIsOn());
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedBefore);
}

void t_assumed_on_offers_web_stop() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(momentaryLogicalRunActive);
  CHECK(!reedIsOn());
  CHECK(machineIsRunning());
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
}
#endif

void t_recovery_hold_copies_raw_gpio() {
  resetMomentaryHarness();
  brewSeesPhysicalSwitchEdges = true;
  const size_t closedBefore = hostRelayClosedWrites;
  hostPinLevel[PADDLE_GPIO] = PADDLE_ACTIVE_LEVEL;
  updatePaddleInput();
  hostMillis += PADDLE_DEBOUNCE_MS + 1;
  updatePaddleInput();
  CHECK(paddleOn);
  CHECK(paddleTurnedOn);
  CHECK(!session.active);
  CHECK(hostRelayClosedWrites == closedBefore);
  CHECK(!getRelaySafetySnapshot().closed);
  hostPinLevel[PADDLE_GPIO] = !PADDLE_ACTIVE_LEVEL;
  updatePaddleInput();
  hostMillis += PADDLE_DEBOUNCE_MS + 1;
  updatePaddleInput();
  CHECK(!paddleOn);
  CHECK(paddleTurnedOff);
  machineReleasePhysicalSwitchToBrew();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  CHECK(stopperState == StopperState::READY);
  shortPress(150);
  CHECK(session.active);
  CHECK(hostRelayClosedWrites > closedBefore);
}

#if SHOT_STOPPER_MACHINE_TYPE == 1
void dripFreshWeight(float weight, uint32_t dtMs) {
  currentWeight = weight;
  currentWeightReceivedAtMs = hostMillis;
  ++currentWeightSequence;
  runLoopAfter(dtMs);
}

void confirmEspressoFlow() {
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    dripFreshWeight(currentWeight + 0.45f, 100);
  }
}

void t_start_ack_stays_assumed_without_flow() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  currentWeightReceivedAtMs = hostMillis;
  ++currentWeightSequence;
  runLoopAfter(800);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  CHECK(!machineAllowsFirmwareStopPulse());
}

void t_confirmed_on_expires_without_flow() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  confirmEspressoFlow();
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  const float held = currentWeight;
  for (int step = 0; step < 6; ++step) {
    dripFreshWeight(held, 100);
  }
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  const size_t closedQuiet = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedQuiet);
}

void t_stop_ack_quiet_confirms_off() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  confirmEspressoFlow();
  CHECK(machineRequestStop());
  CHECK(machineIsRunning());
  const float held = currentWeight;
  for (int step = 0; step < 5; ++step) {
    dripFreshWeight(held, 100);
  }
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!machineIsRunning());
}

void t_stop_ack_timeout_without_quiet_stays_on() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  confirmEspressoFlow();
  CHECK(machineRequestStop());
  CHECK(machineIsRunning());
  for (int step = 0; step < 16; ++step) {
    dripFreshWeight(currentWeight + 0.45f, 100);
    CHECK(machineIsRunning());
  }
  CHECK(machineRunState() != MachineRunState::CONFIRMED_OFF);
  CHECK(machineIsRunning());
}

void t_start_preinfusion_quiet_stays_assumed() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  for (int step = 0; step < 40; ++step) {
    dripFreshWeight(0.0f, 200);
  }
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  CHECK(machineIsRunning());
}

void t_start_nack_long_baseline_confirms_off() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  for (int step = 0; step < 61; ++step) {
    dripFreshWeight(0.0f, 200);
  }
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!machineIsRunning());
}

void t_user_stop_after_preinfusion_does_not_pulse() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  for (int step = 0; step < 41; ++step) {
    dripFreshWeight(0.0f, 200);
  }
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  momentaryUserStopThisCycle = true;
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedBefore);
}

void t_accidental_touch_does_not_confirm_on() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  session.accidentalTouchHolding = true;
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    dripFreshWeight(currentWeight + 0.45f, 100);
  }
  CHECK(machineRunState() != MachineRunState::CONFIRMED_ON);
}

void t_gap_skip_does_not_confirm_from_step() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  dripFreshWeight(2.2f, 600);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
}

void t_accepted_weight_drives_confirm_on() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 0.2f;
  currentWeight = 40.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    session.lastAcceptedWeightG += 0.45f;
    currentWeightReceivedAtMs = hostMillis;
    ++currentWeightSequence;
    runLoopAfter(100);
  }
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
}

void t_logical_elapsed_after_start_pulse_opens() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(machineElapsedMs() > 0);
  publishControlStatus();
  CHECK(stagingControlStatus.circuitElapsedMs > 0);
  CHECK(stagingControlStatus.machineRunning);
}

void t_orphan_wall_keeps_running_and_user_stop_pulses() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 9000;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  const size_t closedAtStart = hostRelayClosedWrites;
  for (int step = 0; step < 46; ++step) {
    dripFreshWeight(0.0f, 200);
  }
  CHECK(momentaryOrphanRun || !momentaryLogicalRunActive);
  CHECK(hostRelayClosedWrites == closedAtStart);
  CHECK(machineIsRunning());
  CHECK(!controlAllowsConfigurationNow());
  momentaryUserStopThisCycle = true;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites > closedAtStart);
}

void t_brief_stale_demotes_confirmed_not_unknown() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  confirmEspressoFlow();
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  runLoopAfter(1200);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  CHECK(machineRunState() != MachineRunState::UNKNOWN);
  runLoopAfter(1600);
  CHECK(machineRunState() == MachineRunState::UNKNOWN);
}

void t_small_delta_does_not_confirm_on() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.2f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  for (int step = 0; step < 8; ++step) {
    dripFreshWeight(currentWeight + 0.08f, 100);
  }
  CHECK(machineRunState() != MachineRunState::CONFIRMED_ON);
}

void t_confirmed_wall_pulses_once_and_ends_session() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 2000;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  confirmEspressoFlow();
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  CHECK(session.active);
  const size_t closedAtConfirm = hostRelayClosedWrites;
  for (int step = 0; step < 14; ++step) {
    if (!session.active) {
      break;
    }
    dripFreshWeight(currentWeight + 0.45f, 100);
  }
  CHECK(!session.active);
  CHECK(hostRelayClosedWrites == closedAtConfirm + 1);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 80);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedAtConfirm + 1);
}
#endif

#if SHOT_STOPPER_MACHINE_TYPE == 2
void t_reed_polarity_stop_when_running() {
  resetMomentaryHarness();
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  setRawReed(true);
  paddleOn = false;
  CHECK(machineIsRunning());
  shortPress(180);
  CHECK(!momentaryStartEdgeThisCycle);
  CHECK(momentaryUserStopThisCycle || !paddleOn);
}

void t_reed_wall_pulses_once_and_ends_session() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 2000;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  setRawReed(true);
  CHECK(session.active);
  const size_t closedBeforeWall = hostRelayClosedWrites;
  runLoopAfter(2200);
  CHECK(!session.active);
  CHECK(hostRelayClosedWrites == closedBeforeWall + 1);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 80);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedBeforeWall + 1);
}
#endif

void t_logical_wall_trips_existing_flags() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 2000;
  runLoopAfter(PADDLE_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  CHECK(momentaryLogicalRunActive);
#if SHOT_STOPPER_MACHINE_TYPE == 2
  setRawReed(true);
  CHECK(momentaryLogicalRunActive);
#endif
  runLoopAfter(2200);
  CHECK(getRelaySafetySnapshot().operationalTripped ||
        operationalLimitTripped || !momentaryLogicalRunActive);
}

struct TestCase {
  const char *id;
  void (*function)();
};

const TestCase kTests[] = {
    {"P01", t_short_press_starts_without_latching_relay},
    {"P36", t_default_starts_on_release_not_press},
    {"P37", t_press_edge_starts_on_press_and_release_does_not_stop},
    {"P38", t_press_edge_later_click_stops},
    {"P02", t_guard_reject_does_not_pulse},
    {"P03", t_long_press_mirrors_without_brew},
#if SHOT_STOPPER_MACHINE_TYPE == 1
    {"P04", t_only_auto_cut_needs_confirmed_on},
    {"P05", t_quiet_pan_does_not_force_cut_when_unknown},
    {"P09", t_tare_rebases_flow_signature},
    {"P10", t_user_stop_pulses_when_assumed_on},
    {"P11", t_wall_with_quiet_pan_does_not_auto_pulse},
    {"P12", t_settings_lock_follows_logical_run},
    {"P13", t_polarity_resyncs_to_running_machine},
    {"P14", t_coalesce_aborts_start_pulse},
    {"P18", t_start_ack_stays_assumed_without_flow},
    {"P21", t_confirmed_on_expires_without_flow},
    {"P22", t_stop_ack_quiet_confirms_off},
    {"P23", t_stop_ack_timeout_without_quiet_stays_on},
    {"P24", t_start_preinfusion_quiet_stays_assumed},
    {"P25", t_start_nack_long_baseline_confirms_off},
    {"P26", t_user_stop_after_preinfusion_does_not_pulse},
    {"P27", t_accidental_touch_does_not_confirm_on},
    {"P28", t_gap_skip_does_not_confirm_from_step},
    {"P29", t_accepted_weight_drives_confirm_on},
    {"P30", t_logical_elapsed_after_start_pulse_opens},
    {"P31", t_orphan_wall_keeps_running_and_user_stop_pulses},
    {"P32", t_brief_stale_demotes_confirmed_not_unknown},
    {"P33", t_small_delta_does_not_confirm_on},
    {"P34", t_confirmed_wall_pulses_once_and_ends_session},
#endif
#if SHOT_STOPPER_MACHINE_TYPE == 2
    {"P06", t_reed_off_blocks_firmware_cut},
    {"P07", t_reed_on_allows_firmware_cut},
    {"P15", t_reed_boot_on_is_unknown},
    {"P16", t_reed_off_wins_over_rising_weight},
    {"P17", t_assumed_on_offers_web_stop},
    {"P19", t_reed_polarity_stop_when_running},
    {"P35", t_reed_wall_pulses_once_and_ends_session},
#endif
    {"P20", t_recovery_hold_copies_raw_gpio},
    {"P08", t_logical_wall_trips_existing_flags},
};

}  // namespace

int main() {
  static_assert(SHOT_STOPPER_MACHINE_TYPE != 0,
                "momentary host tests require SHOT_STOPPER_MACHINE_TYPE 1 or 2");
  for (const TestCase &test : kTests) {
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
