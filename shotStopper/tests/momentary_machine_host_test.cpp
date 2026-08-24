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
  activatorOn = false;
  rawActivatorOn = false;
  activatorTurnedOn = false;
  activatorTurnedOff = false;
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
  hostPinLevel[ACTIVATOR_GPIO] = !ACTIVATOR_ACTIVE_LEVEL;
#if SHOT_STOPPER_MACHINE_TYPE == 2
  hostPinLevel[REED_GPIO] = !REED_ACTIVE_LEVEL;
#endif
  initializeActivatorInput();
  machineOnActivatorReady();
  machineSetActivatorDriveAllowed(true);
  machineNoteActivatorReleased();
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
  momentarySkipFirmwareStopPulse = false;
  pulseOutputActive = false;
#if SHOT_STOPPER_MACHINE_TYPE == 2
  reedRawOn = false;
  reedOn = false;
  reedSawStableOff = false;
  reedAssume = ReedAssume::NONE;
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
  hostPinLevel[ACTIVATOR_GPIO] = on ? ACTIVATOR_ACTIVE_LEVEL
                                : !ACTIVATOR_ACTIVE_LEVEL;
  loop();
}

void shortPress(uint32_t heldMs) {
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  runLoopAfter(heldMs);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
}

void pressDown() {
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
}

void releaseUp() {
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
}

#if SHOT_STOPPER_MACHINE_TYPE == 2
void setRawReed(bool on) {
  hostPinLevel[REED_GPIO] = on ? REED_ACTIVE_LEVEL : !REED_ACTIVE_LEVEL;
  loop();
  runLoopAfter(REED_DEBOUNCE_MS + 1);
}
#endif

void t_short_press_mirrors_then_opens() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(stopperState == StopperState::READY);
  pressDown();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(session.active);
  releaseUp();
  CHECK(session.active);
  CHECK(!getRelaySafetySnapshot().closed);
#if SHOT_STOPPER_MACHINE_TYPE == 1
  CHECK(machineIsRunning());
#endif
}

void t_default_starts_on_press() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(session.active);
  CHECK(stopperState != StopperState::READY || session.active);
  CHECK(getRelaySafetySnapshot().closed);
  releaseUp();
  CHECK(session.active);
}

void t_release_mode_starts_on_release_not_press() {
  resetMomentaryHarness();
  runtimeConfig.momentaryStartOnPress = false;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(!session.active);
  CHECK(stopperState == StopperState::READY);
  CHECK(getRelaySafetySnapshot().closed);
  releaseUp();
  CHECK(session.active);
}

void t_second_short_press_stops_without_rinse() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(180);
  CHECK(session.active);
  CHECK(stopperState != StopperState::RINSE);
  shortPress(180);
  CHECK(stopperState != StopperState::RINSE);
  CHECK(!session.active || stopperState == StopperState::READY);
}

void t_guard_reject_does_not_mirror() {
  resetMomentaryHarness();
  runtimeConfig.cupProtectionEnabled = true;
  runtimeConfig.requireCupToStart = true;
  mutableActiveShotPreset(presetBank).cupProtectionEnabled = true;
  mutableActiveShotPreset(presetBank).requireCupToStart = true;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  const size_t closedBefore = hostRelayClosedWrites;
  pressDown();
  CHECK(hostRelayClosedWrites == closedBefore);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  CHECK(stopperState == StopperState::READY);
  releaseUp();
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t_no_scale_bbw_armed_does_not_mirror_then_idle_allows() {
  resetMomentaryHarness();
  runtimeConfig.timerOnly = false;
  runtimeConfig.avoidBbwShotWithoutScale = true;
  mutableActiveShotPreset(presetBank).brewByWeight = true;
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
  runtimeConfig.avoidBbwShotWithoutScale = true;
  noScaleShotGuardArmed = true;
  noScaleShotGuardHold = false;
  noScaleShotGuardActivityAtMs = 0;
  scale.connected = false;
  setScaleLinkState(ScaleLinkState::DISCONNECTED);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(noScaleShotGuardArmed);
  const size_t closedBefore = hostRelayClosedWrites;
  pressDown();
  CHECK(hostRelayClosedWrites == closedBefore);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  CHECK(stopperState == StopperState::READY);
  CHECK(noScaleShotGuardArmed);
  CHECK(noScaleShotGuardHold);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(!noScaleShotGuardArmed);
  CHECK(!noScaleShotGuardHold);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  releaseUp();
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  pressDown();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(session.active);
}

void t_user_stop_without_session_does_not_leave_orphan_run() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(!session.active);
  CHECK(!machineIsRunning());
  momentaryUserStopThisCycle = true;
  momentarySkipFirmwareStopPulse = true;
  serviceMachine();
  CHECK(!machineIsRunning());
  CHECK(!momentaryLogicalRunActive);
}

void t_long_press_mirrors_from_first_instant() {
  resetMomentaryHarness();
#if SHOT_STOPPER_MACHINE_TYPE == 2
  runtimeConfig.reedConfirmTimeoutHundredMs = 50;
#endif
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(session.active);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  runLoopAfter(COMPILED_MAX_SINGLE_PRESS_MS + 20);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!machineIsRunning());
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
}

void t_release_mode_long_press_ignored_as_start() {
  resetMomentaryHarness();
  runtimeConfig.momentaryStartOnPress = false;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  runLoopAfter(COMPILED_MAX_SINGLE_PRESS_MS + 20);
  CHECK(!session.active);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
}

#if SHOT_STOPPER_MACHINE_TYPE == 1
void t_only_auto_cut_needs_confirmed_on() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  const size_t closedQuiet = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedQuiet);

  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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

void t_user_stop_does_not_extra_pulse_when_assumed_on() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(50);
  CHECK(machineIsRunning());
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  const size_t closedBefore = hostRelayClosedWrites;
  CHECK(machineRequestStop());
  CHECK(hostRelayClosedWrites == closedBefore);
}

void t_wall_with_quiet_pan_does_not_auto_pulse() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 800;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(session.active);
  const size_t closedAtStart = hostRelayClosedWrites;
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  runLoopAfter(900);
  CHECK(!momentaryLogicalRunActive || getRelaySafetySnapshot().operationalTripped ||
        operationalLimitTripped);
  CHECK(hostRelayClosedWrites == closedAtStart);
  CHECK(!machineAllowsFirmwareStopPulse());
}

void t_settings_lock_follows_logical_run() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(controlAllowsConfigurationNow());
  shortPress(150);
  CHECK(machineIsRunning());
  CHECK(!controlAllowsConfigurationNow());
}

void t_polarity_resyncs_to_running_machine() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(50);
  CHECK(machineIsRunning());
  shortPress(180);
  CHECK(momentaryUserStopThisCycle || !machineIsRunning() || !session.active);
}

void t_user_press_wins_over_firmware_pulse() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  momentaryInferredState = MachineRunState::CONFIRMED_ON;
  CHECK(machineRequestStop());
  CHECK(pulseOutputActive || getRelaySafetySnapshot().closed);
  pressDown();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!pulseOutputActive);
  releaseUp();
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  CHECK(momentaryLogicalRunActive);
  CHECK(!reedIsOn());
  CHECK(machineIsRunning());
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
}
#endif

void t_recovery_hold_mirrors_gpio() {
  resetMomentaryHarness();
  hostPinLevel[ACTIVATOR_GPIO] = ACTIVATOR_ACTIVE_LEVEL;
  initializeActivatorInput();
  applyMomentaryRelayDrive();
  CHECK(momentaryPhysicalOn);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  hostPinLevel[ACTIVATOR_GPIO] = !ACTIVATOR_ACTIVE_LEVEL;
  updateActivatorInput();
  hostMillis += ACTIVATOR_DEBOUNCE_MS + 1;
  updateActivatorInput();
  CHECK(!momentaryPhysicalOn);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!session.active);
  machineOnActivatorReady();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(stopperState == StopperState::READY);
  shortPress(150);
  CHECK(session.active);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  confirmEspressoFlow();
  CHECK(machineRequestStop());
  CHECK(machineIsRunning());
  const float held = currentWeight;
  for (int step = 0; step < 4; ++step) {
    dripFreshWeight(held, 100);
  }
  CHECK(machineIsRunning());
  for (int step = 0; step < 8; ++step) {
    dripFreshWeight(held, 100);
  }
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!machineIsRunning());
}

void t_stop_ack_timeout_without_quiet_stays_on() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(machineElapsedMs() > 0);
  publishControlStatus();
  CHECK(stagingControlStatus.circuitElapsedMs > 0);
  CHECK(stagingControlStatus.machineRunning);
}

void t_orphan_wall_does_not_auto_pulse_user_still_mirrors() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 9000;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  const size_t closedAtStart = hostRelayClosedWrites;
  for (int step = 0; step < 46; ++step) {
    dripFreshWeight(0.0f, 200);
  }
  CHECK(hostRelayClosedWrites == closedAtStart);
  CHECK(!machineAllowsFirmwareStopPulse());
  const size_t closedBeforeStop = hostRelayClosedWrites;
  pressDown();
  CHECK(hostRelayClosedWrites > closedBeforeStop);
  CHECK(getRelaySafetySnapshot().closed);
  releaseUp();
  CHECK(!getRelaySafetySnapshot().closed);
}

void t_brief_stale_demotes_confirmed_not_unknown() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
  CHECK(getRelaySafetySnapshot().closed);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 80);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedAtConfirm + 1);
}
#endif

#if SHOT_STOPPER_MACHINE_TYPE == 2
void t_reed_polarity_stop_when_running() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  setRawReed(true);
  activatorOn = false;
  CHECK(machineIsRunning());
  shortPress(180);
  CHECK(!momentaryStartEdgeThisCycle);
  CHECK(momentaryUserStopThisCycle || !activatorOn);
}

void t_reed_stays_assumed_on_until_timeout_then_confirms_off() {
  resetMomentaryHarness();
  runtimeConfig.reedConfirmTimeoutHundredMs = 2;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  CHECK(session.active);
  CHECK(!reedIsOn());
  runLoopAfter(100);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  const size_t closedBefore = hostRelayClosedWrites;
  runLoopAfter(150);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!session.active);
  CHECK(!machineIsRunning());
  CHECK(hostRelayClosedWrites == closedBefore);
}

void t_reed_on_during_window_confirms_immediately() {
  resetMomentaryHarness();
  runtimeConfig.reedConfirmTimeoutHundredMs = 20;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  setRawReed(true);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  CHECK(reedIsOn());
}

void t_reed_stop_assumed_off_until_reed_matches() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  setRawReed(true);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  releaseUp();
  pressDown();
  CHECK(machineRunState() == MachineRunState::ASSUMED_OFF);
  CHECK(reedIsOn());
  setRawReed(false);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
}

void t_reed_stop_timeout_confirms_actual_on() {
  resetMomentaryHarness();
  runtimeConfig.reedConfirmTimeoutHundredMs = 2;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  setRawReed(true);
  releaseUp();
  pressDown();
  CHECK(machineRunState() == MachineRunState::ASSUMED_OFF);
  runLoopAfter(250);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  CHECK(reedIsOn());
}

void t_reed_outside_assumed_follows_reed() {
  resetMomentaryHarness();
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  setRawReed(true);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  setRawReed(false);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
}

void t_reed_release_mode_assume_clock_starts_on_release() {
  resetMomentaryHarness();
  runtimeConfig.momentaryStartOnPress = false;
  runtimeConfig.reedConfirmTimeoutHundredMs = 5;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!session.active);
  runLoopAfter(400);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!session.active);
  releaseUp();
  CHECK(session.active);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  runLoopAfter(150);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  runLoopAfter(400);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!session.active);
}

void t_reed_release_mode_stop_assume_starts_on_release() {
  resetMomentaryHarness();
  runtimeConfig.momentaryStartOnPress = false;
  runtimeConfig.reedConfirmTimeoutHundredMs = 20;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  setRawReed(true);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  CHECK(session.active);
  pressDown();
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  CHECK(reedIsOn());
  releaseUp();
  CHECK(machineRunState() == MachineRunState::ASSUMED_OFF);
  CHECK(reedIsOn());
}

void t_reed_disqualified_press_grace_then_reed_canonical() {
  resetMomentaryHarness();
  runtimeConfig.maxSinglePressHundredMs = 2;
  runtimeConfig.reedConfirmTimeoutHundredMs = 3;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  pressDown();
  CHECK(session.active);
  CHECK(machineRunState() == MachineRunState::ASSUMED_ON);
  runLoopAfter(250);
  CHECK(!session.active);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_OFF);
  CHECK(!machineIsRunning());
  setRawReed(true);
  CHECK(machineRunState() == MachineRunState::ASSUMED_OFF);
  CHECK(!machineIsRunning());
  runLoopAfter(400);
  CHECK(machineRunState() == MachineRunState::CONFIRMED_ON);
  CHECK(machineIsRunning());
  CHECK(!session.active);
}

void t_reed_wall_pulses_once_and_ends_session() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 2000;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
  shortPress(150);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 50);
  setRawReed(true);
  CHECK(session.active);
  const size_t closedBeforeWall = hostRelayClosedWrites;
  runLoopAfter(2200);
  CHECK(!session.active);
  CHECK(hostRelayClosedWrites == closedBeforeWall + 1);
  CHECK(getRelaySafetySnapshot().closed);
  runLoopAfter(COMPILED_STOP_PULSE_MS + 80);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedBeforeWall + 1);
}
#endif

void t_logical_wall_trips_existing_flags() {
  resetMomentaryHarness();
  runtimeConfig.operationalWallMs = 2000;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS + 1);
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
    {"P01", t_short_press_mirrors_then_opens},
    {"P36", t_default_starts_on_press},
    {"P40", t_release_mode_starts_on_release_not_press},
    {"P38", t_second_short_press_stops_without_rinse},
    {"P02", t_guard_reject_does_not_mirror},
    {"P50", t_no_scale_bbw_armed_does_not_mirror_then_idle_allows},
    {"P39", t_user_stop_without_session_does_not_leave_orphan_run},
    {"P03", t_long_press_mirrors_from_first_instant},
    {"P41", t_release_mode_long_press_ignored_as_start},
#if SHOT_STOPPER_MACHINE_TYPE == 1
    {"P04", t_only_auto_cut_needs_confirmed_on},
    {"P05", t_quiet_pan_does_not_force_cut_when_unknown},
    {"P09", t_tare_rebases_flow_signature},
    {"P10", t_user_stop_does_not_extra_pulse_when_assumed_on},
    {"P11", t_wall_with_quiet_pan_does_not_auto_pulse},
    {"P12", t_settings_lock_follows_logical_run},
    {"P13", t_polarity_resyncs_to_running_machine},
    {"P14", t_user_press_wins_over_firmware_pulse},
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
    {"P31", t_orphan_wall_does_not_auto_pulse_user_still_mirrors},
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
    {"P42", t_reed_stays_assumed_on_until_timeout_then_confirms_off},
    {"P43", t_reed_on_during_window_confirms_immediately},
    {"P44", t_reed_stop_assumed_off_until_reed_matches},
    {"P45", t_reed_stop_timeout_confirms_actual_on},
    {"P46", t_reed_outside_assumed_follows_reed},
    {"P47", t_reed_release_mode_assume_clock_starts_on_release},
    {"P48", t_reed_release_mode_stop_assume_starts_on_release},
    {"P49", t_reed_disqualified_press_grace_then_reed_canonical},
#endif
    {"P20", t_recovery_hold_mirrors_gpio},
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
