#define SHOT_STOPPER_HOST_TEST
#define ARDUINO_ESP32C3_DEV
#define SHOT_STOPPER_ENABLE_REMOTE_CN9 1

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "../shotStopper.ino"

namespace {

int failures = 0;
int testsRun = 0;
bool hostAutoScaleWorkerProgress = true;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__ << ": check failed: "       \
                << #condition << "\n";                                         \
      ++failures;                                                              \
      return;                                                                  \
    }                                                                          \
  } while (false)

#define CHECK_VALUE(condition, fallback)                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__ << ": check failed: "       \
                << #condition << "\n";                                         \
      ++failures;                                                              \
      return fallback;                                                         \
    }                                                                          \
  } while (false)

void deleteHostResources() {
  delete scaleCommandQueue;
  delete scaleEventQueue;
  delete webCommandQueue;
  delete statusIndicatorQueue;
  delete relaySafetyTimer;
  delete operationalLimitTimer;
  scaleCommandQueue = nullptr;
  scaleEventQueue = nullptr;
  webCommandQueue = nullptr;
  statusIndicatorQueue = nullptr;
  statusIndicatorTaskHandle = nullptr;
  relaySafetyTimer = nullptr;
  operationalLimitTimer = nullptr;
  independentSafetyTimer.resetForHost();
}

void resetHarness(bool initialPaddleOn, bool scaleConnected) {
  deleteHostResources();

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
  hostCn9ArmBeforeCommitHook = nullptr;
  hostTaskWatchdogOperationsSucceed = true;
  hostTaskWatchdogConfigured = false;
  hostTaskWatchdogSubscriptions = 0;
  hostTaskWatchdogFeeds = 0;
  hostCpuFrequencySetSucceeds = true;
  EEPROM.beginSucceeds = true;
  BLE.beginSucceeds = true;
  resetSafetyResetGuardForHost();

  stopperState = StopperState::REQUIRES_OFF;
  shot = ShotTrajectory{};
  session = CycleSession{};
  pendingAnalysis = PendingShotAnalysis{};
  runtimeConfig = RuntimeConfig{};
  lastCycle = LastCycleSummary{};
  debugLog.clear();
  publishedControlStatus = ControlStatusSnapshot{};
  maintenanceLease = MaintenanceLease{};
  maintenanceCancellationCommand = WebCommand{};
  maintenanceCancellationPending = false;
  controlResultCommand = WebCommand{};
  controlResultPending = false;
  hostForwardAcceptedNetworkCommandSucceeds = true;
  hostForwardAcceptedNetworkCommandCalls = 0;
  hostLastForwardedNetworkCommand = WebCommand{};
  runtimePersistPending = false;
  runtimePersistCandidate = RuntimeConfig{};
  runtimePersistRequestId = 0;
  runtimePersistRetryAtMs = 0;
  nextInternalRequestId = 0x80000000UL;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = 0;
  currentWeightSequence = 0;
  nextCycleId = 1;
  scaleWorkerTaskHandle = nullptr;
  scale = AcaiaArduinoBLE(DEBUG);
  scale.connected = scaleConnected;
  scaleLinkState = ScaleLinkState::DISCONNECTED;
  scaleDisconnectSequence = 0;
  scaleWorkerProgressAtMs = hostMillis;
  scaleEventsDropped = 0;
  scaleWorkerStackMinWords = 0;
  scaleCriticalEvent = ScaleEvent{};
  scaleCriticalEventPending = false;
  scaleBeepPending = false;
  scaleBeepCycleId = 0;
  scalePaddleReturnReminderBeepPending = false;
  paddleReturnReminderActive = false;
  paddleReturnReminderLastAtMs = 0;
  hostAutoScaleWorkerProgress = true;
  setScaleLinkState(scaleConnected ? ScaleLinkState::CONNECTED
                                   : ScaleLinkState::DISCONNECTED);

  rawPaddleOn = false;
  paddleOn = false;
  paddleTurnedOn = false;
  paddleTurnedOff = false;
  rawPaddleChangedAtMs = 0;
  virtualPaddleOn = false;
  cn9Closed = false;
  relaySafetyTripped = false;
  operationalLimitTripped = false;
  cn9ClosedAtMs = 0;
  operationalLimitAtArmMs = HARD_MAX_CN9_CLOSED_MS;
  relaySafetyState = RelaySafetyState::OPEN;
  relaySafetyFault = RelaySafetyFault::NONE;
  relaySafetyGeneration = 0;
  relaySafetyTimersReady = false;
  taskWatchdogReady = configureTaskWatchdog() &&
                      subscribeCurrentTaskToWatchdog();
  criticalTaskWatchdogFault = false;
  feedbackTransitionPending = false;
  feedbackExpectedClosed = false;
  feedbackTransitionStartedAtMs = 0;
  safetyHeartbeatLevel = false;
  safetyHeartbeatToggledAtMs = 0;
  safeRestartRequested = false;
  platformClockReady = true;
  persistenceReady = true;
  bleStackReady = true;
  safetyResetStatus = SafetyResetSnapshot{};
  resetRecoverySawPaddleOn = false;
  resetRecoveryOffStartedAtMs = 0;
  firmwareInitializationComplete = true;
  statusIndicatorsReady = false;
  lastPublishedIndicatorFrame = StatusIndicatorFrame{};
  indicatorFramePublished = false;

  scaleCommandQueue =
      xQueueCreate(SCALE_COMMAND_QUEUE_LENGTH, sizeof(ScaleCommand));
  scaleEventQueue =
      xQueueCreate(SCALE_EVENT_QUEUE_LENGTH, sizeof(ScaleEvent));
  webCommandQueue =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  CHECK(scaleCommandQueue != nullptr);
  CHECK(scaleEventQueue != nullptr);
  CHECK(webCommandQueue != nullptr);
  CHECK(initializeRelaySafetyTimer());
  relaySafetyTimersReady = true;

  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  hostPinLevel[PADDLE_GPIO] = initialPaddleOn ? PADDLE_ACTIVE_LEVEL
                                              : !PADDLE_ACTIVE_LEVEL;
  initializePaddleInput();
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
}

void verifySafetyInvariants() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const bool stateMayCloseRelay =
      stopperState == StopperState::QUALIFYING_ON ||
      stopperState == StopperState::BREW ||
      stopperState == StopperState::RINSE ||
      stopperState == StopperState::MANUAL_NO_SCALE;

  if (relay.closed && (!stateMayCloseRelay || !session.active)) {
    std::cerr << "Safety invariant failed: CN9 closed in "
              << stateName(stopperState) << "\n";
    ++failures;
  }
  if ((stopperState == StopperState::READY ||
       stopperState == StopperState::REQUIRES_OFF) &&
      relay.closed) {
    std::cerr << "Safety invariant failed: safe state has CN9 closed\n";
    ++failures;
  }
  if (!paddleOn && !rawPaddleOn && stopperState != StopperState::RINSE &&
      session.source != ControlSource::WEB && relay.closed) {
    std::cerr << "Safety invariant failed: stable paddle OFF has CN9 closed\n";
    ++failures;
  }
}

void runLoopAfter(uint32_t deltaMs) {
  hostMillis += deltaMs;
  if (hostAutoScaleWorkerProgress && scale.connected) {
    markScaleWorkerProgress();
    if (session.active && session.automaticEnabled) {
      currentWeightReceivedAtMs = hostMillis;
      ++currentWeightSequence;
      session.receivedFreshWeightInCycle = true;
    }
  }
  hostServiceEspTimer(relaySafetyTimer);
  hostServiceEspTimer(operationalLimitTimer);
  independentSafetyTimer.serviceForHost();
  loop();
  verifySafetyInvariants();
}

void setRawPaddle(bool on) {
  hostPinLevel[PADDLE_GPIO] = on ? PADDLE_ACTIVE_LEVEL
                                : !PADDLE_ACTIVE_LEVEL;
  if (hostAutoScaleWorkerProgress && scale.connected) {
    markScaleWorkerProgress();
  }
  loop();
  verifySafetyInvariants();
}

void setScaleConnected(bool connected) {
  scale.connected = connected;
  setScaleLinkState(connected ? ScaleLinkState::CONNECTED
                              : ScaleLinkState::DISCONNECTED);
}

void reachReadyFromBoot() {
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
}

uint32_t startCycle() {
  CHECK_VALUE(stopperState == StopperState::READY, hostMillis);
  if (scale.connected) {
    currentWeightReceivedAtMs = hostMillis;
    ++currentWeightSequence;
  }
  const uint32_t rawOnAtMs = hostMillis;
  setRawPaddle(true);
  CHECK_VALUE(stopperState == StopperState::READY, rawOnAtMs);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK_VALUE(stopperState == StopperState::QUALIFYING_ON, rawOnAtMs);
  CHECK_VALUE(getRelaySafetySnapshot().closed, rawOnAtMs);
  if (session.automaticEnabled) {
    ScaleEvent fresh;
    fresh.type = ScaleEventType::WEIGHT;
    fresh.receivedAtMs = hostMillis;
    fresh.weightG = currentWeight;
    CHECK_VALUE(publishScaleEvent(fresh, false), rawOnAtMs);
    processScaleWorkerEvents();
  }
  return rawOnAtMs;
}

void releaseAtPhysicalDuration(uint32_t rawOnAtMs, uint32_t durationMs) {
  const uint32_t rawOffAtMs = rawOnAtMs + durationMs;
  CHECK(hostMillis <= rawOffAtMs);
  runLoopAfter(rawOffAtMs - hostMillis);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
}

void reachSessionElapsed(uint32_t elapsed) {
  const uint32_t current = elapsedMs(session.startedAtMs);
  CHECK(current <= elapsed);
  runLoopAfter(elapsed - current);
}

size_t commandCount(ScaleCommandType type) {
  size_t count = 0;
  for (const std::vector<uint8_t> &bytes : scaleCommandQueue->items) {
    ScaleCommand command;
    std::memcpy(&command, bytes.data(), sizeof(command));
    if (command.type == type) {
      ++count;
    }
  }
  return count;
}

bool executeNextScaleCommand() {
  ScaleCommand command;
  if (xQueueReceive(scaleCommandQueue, &command, 0) != pdTRUE) {
    return false;
  }
  markScaleWorkerProgress();
  executeScaleCommand(command);
  processScaleWorkerEvents();
  return true;
}

bool executePendingScaleBrewBeep() {
  uint32_t cycleId = 0;
  if (!takeScaleBrewBeep(cycleId)) {
    return false;
  }
  CHECK_VALUE(cycleId == session.id, false);
  markScaleWorkerProgress();
  executeScaleBeepCommand(DebugCode::SCALE_BEEP_OK,
                          DebugCode::SCALE_BEEP_FAILED,
                          DebugCode::SCALE_BEEP_UNSUPPORTED);
  return true;
}

bool executePendingScalePaddleReturnReminderBeep() {
  if (!takeScalePaddleReturnReminderBeep()) {
    return false;
  }
  markScaleWorkerProgress();
  executeScaleBeepCommand(DebugCode::SCALE_PADDLE_REMINDER_BEEP_OK,
                          DebugCode::SCALE_PADDLE_REMINDER_BEEP_FAILED,
                          DebugCode::SCALE_PADDLE_REMINDER_BEEP_UNSUPPORTED);
  return true;
}

WebCommand webControlCommand(WebCommandType type) {
  WebCommand command;
  command.type = type;
  command.requestId = 1;
  command.webSessionId = 11;
  command.controlLeaseId = 17;
  return command;
}

void finishHostMaintenance() {
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS + 1);
  CHECK(!maintenanceLease.active);
}

void t01_boot_with_paddle_off() {
  resetHarness(false, false);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  reachReadyFromBoot();
}

void t02_boot_with_paddle_on() {
  resetHarness(true, true);
  runLoopAfter(PADDLE_DEBOUNCE_MS * 4);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  startCycle();
}

void t03_sustained_on_confirms_brew_once() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.resetTimerCalls == 0);
  CHECK(scale.startTimerCalls == 0);
  CHECK(scale.tareCalls == 0);
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "tareStartTimer");
  CHECK(session.remoteTimerStarted);
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  CHECK(shot.confirmedBrew);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  CHECK(scaleBeepPending);
  CHECK(executePendingScaleBrewBeep());
  CHECK(scale.beepCalls == 1);
  CHECK(scale.commandLog.size() == 2);
  CHECK(scale.commandLog[1] == "beepWithoutStateChange");
  runLoopAfter(100);
  CHECK(!scaleBeepPending);
  CHECK(scale.beepCalls == 1);
  CHECK(scale.startTimerCalls == 0);
}

void t04_exact_rinse_boundary_and_duration() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  const uint32_t remaining =
      runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs);
  runLoopAfter(remaining - 1);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t05_release_between_rinse_and_brew_is_short_shot() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::SHORT_SHOT);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t06_paddle_off_during_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::PADDLE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t07_scale_prediction_requires_release_after_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  shot.expectedEndS = 1.0f;
  reachSessionElapsed(runtimeConfig.minAutoStopMs);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_PREDICTION);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t08_on_during_rinse_is_ignored() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 500);
  CHECK(stopperState == StopperState::RINSE);
  const uint32_t rinseStarted = session.rinseStartedAtMs;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(session.rinseStartedAtMs == rinseStarted);
}

void t09_rinse_ending_on_requires_off() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 500);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  runLoopAfter(runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs));
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t10_paddle_bounce_does_not_start_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS - 1);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == 0);
}

void t11_ble_loss_degrades_brew_without_late_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  setScaleConnected(false);
  loop();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.timerStopResult == TimerStopResult::NOT_ATTEMPTED);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void t12_global_limit_opens_manual_and_brew_cycles() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  reachSessionElapsed(HARD_MAX_CN9_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
  CHECK(!getRelaySafetySnapshot().closed);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  reachSessionElapsed(HARD_MAX_CN9_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t13_reset_path_starts_with_relay_open() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);

  delete relaySafetyTimer;
  relaySafetyTimer = nullptr;
  delete operationalLimitTimer;
  operationalLimitTimer = nullptr;
  independentSafetyTimer.resetForHost();
  delete scaleCommandQueue;
  delete scaleEventQueue;
  delete webCommandQueue;
  scaleCommandQueue = nullptr;
  scaleEventQueue = nullptr;
  webCommandQueue = nullptr;
  cn9Closed = false;
  relaySafetyTripped = false;
  stopperState = StopperState::REQUIRES_OFF;
  hostRelayOpenWrites = 0;
  setup();
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
  CHECK(hostRelayOpenWrites >= 2);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::LOCKOUT);
  CHECK(getRelaySafetySnapshot().fault ==
        RelaySafetyFault::RESET_DURING_CLOSE);
}

void t14_automatic_stop_stays_open_while_paddle_on() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  shot.expectedEndS = 1.0f;
  reachSessionElapsed(runtimeConfig.minAutoStopMs);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  runLoopAfter(HARD_MAX_CN9_CLOSED_MS * 2);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t15_repeated_rinse_and_brew_reset_session_state() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t firstCycle = startCycle();
  const uint32_t firstId = session.id;
  releaseAtPhysicalDuration(firstCycle, 500);
  runLoopAfter(runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs));
  CHECK(stopperState == StopperState::READY);

  startCycle();
  CHECK(session.id != firstId);
  CHECK(!session.stopTimerRequested);
  CHECK(session.timerStopResult == TimerStopResult::NOT_REQUIRED);
  CHECK(session.endReason == EndReason::NONE);
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 2);
}

void t16_only_micra_states_are_compiled() {
  CHECK(StopperState::REQUIRES_OFF != StopperState::READY);
  CHECK(PADDLE_ACTIVE_LEVEL == LOW);
  CHECK(RELAY_OPEN_LEVEL != RELAY_CLOSED_LEVEL);
}

void t17_simultaneous_global_limit_and_paddle_off_is_idempotent() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  reachSessionElapsed(HARD_MAX_CN9_CLOSED_MS - PADDLE_DEBOUNCE_MS);
  setRawPaddle(false);
  hostRelayOpenWrites = 0;
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
  CHECK(hostRelayOpenWrites == 1);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t18_rinse_and_short_shot_each_request_one_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 500);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  resetHarness(false, true);
  reachReadyFromBoot();
  rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 100);
  CHECK(session.endReason == EndReason::SHORT_SHOT);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t19_manual_cycle_without_scale_has_no_timer_commands() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void t20_rinse_without_scale() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 500);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void t21_global_limit_without_scale_rearms_after_release() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(HARD_MAX_CN9_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
}

void t22_scale_connection_does_not_promote_manual_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  setScaleConnected(true);
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(!session.automaticEnabled);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
}

void t23_prediction_before_minimum_is_reevaluated_at_minimum() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  shot.expectedEndS = 1.0f;
  reachSessionElapsed(runtimeConfig.minAutoStopMs - 1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_PREDICTION);
}

void t24_paddle_off_before_minimum_is_immediate() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(elapsedMs(session.startedAtMs) < runtimeConfig.minAutoStopMs);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t25_ble_loss_while_qualifying_preserves_classification() {
  resetHarness(false, true);
  reachReadyFromBoot();
  uint32_t rawOnAt = startCycle();
  setScaleConnected(false);
  loop();
  releaseAtPhysicalDuration(rawOnAt, 800);
  CHECK(stopperState == StopperState::RINSE);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  setScaleConnected(false);
  loop();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
}

void t26_reconnected_degraded_cycle_sends_one_stop_on_release() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  setScaleConnected(false);
  loop();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  setScaleConnected(true);
  loop();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t27_configuration_is_rejected_while_cycle_is_active() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.config.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.goalWeightG = DEFAULT_GOAL_WEIGHT_G + 6;
  CHECK(enqueueWebCommand(update));
  loop();
  CHECK(runtimeConfig.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(session.config.goalWeightG == DEFAULT_GOAL_WEIGHT_G);

  releaseAtPhysicalDuration(rawPaddleChangedAtMs,
                            runtimeConfig.rinseGestureMs + 100);
  CHECK(stopperState == StopperState::READY);
  CHECK(enqueueWebCommand(update));
  loop();
  finishHostMaintenance();
  CHECK(runtimeConfig.goalWeightG == DEFAULT_GOAL_WEIGHT_G + 6);
  startCycle();
  CHECK(session.config.goalWeightG == DEFAULT_GOAL_WEIGHT_G + 6);
}

void t28_paddle_motion_cannot_cancel_or_extend_rinse() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 400);
  const uint32_t rinseDeadline = session.rinseStartedAtMs + runtimeConfig.rinseDurationMs;

  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(session.rinseStartedAtMs + runtimeConfig.rinseDurationMs == rinseDeadline);

  runLoopAfter(rinseDeadline - hostMillis - 1);
  CHECK(stopperState == StopperState::RINSE);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void r01_transient_disconnect_is_latched_for_cycle() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);

  // Reconnect before the control loop gets a chance to observe DISCONNECTED.
  setScaleConnected(false);
  setScaleConnected(true);
  loop();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(session.scaleWasLost);
  CHECK(!session.automaticEnabled);

  shot.expectedEndS = 0.0f;
  reachSessionElapsed(runtimeConfig.minAutoStopMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
}

void r02_stalled_scale_worker_degrades_cycle_permanently() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);

  hostAutoScaleWorkerProgress = false;
  runLoopAfter(SCALE_WORKER_STALE_MS + 1);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);

  hostAutoScaleWorkerProgress = true;
  markScaleWorkerProgress();
  loop();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(!session.automaticEnabled);
}

void r03_non_finite_weights_cannot_corrupt_state_or_offset() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = 0;
  currentWeightSequence = 0;
  resetShotTrajectory(session.startedAtMs);
  session.receivedFreshWeightInCycle = false;
  const float originalOffset = runtimeConfig.weightOffsetG;

  ScaleEvent event;
  event.type = ScaleEventType::WEIGHT;
  event.receivedAtMs = hostMillis;
  event.weightG = std::numeric_limits<float>::quiet_NaN();
  publishScaleEvent(event, true);
  processScaleWorkerEvents();
  CHECK(currentWeightSequence == 0);
  CHECK(currentWeight == 0.0f);
  CHECK(shot.datapoints == 0);

  recordWeightSample(std::numeric_limits<float>::infinity(), hostMillis);
  CHECK(shot.datapoints == 0);

  pendingAnalysis.pending = true;
  pendingAnalysis.endedAtMs = hostMillis;
  pendingAnalysis.endedWeightSequence = 0;
  pendingAnalysis.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingAnalysis.weightOffsetG = originalOffset;
  currentWeight = std::numeric_limits<float>::quiet_NaN();
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(DRIP_DELAY_MS);
  CHECK(!pendingAnalysis.pending);
  CHECK(isfinite(runtimeConfig.weightOffsetG));
  CHECK(runtimeConfig.weightOffsetG == originalOffset);
}

void r04_scale_commands_execute_once_and_report_results() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.resetTimerCalls == 0);
  CHECK(scale.startTimerCalls == 0);
  CHECK(scale.tareCalls == 0);
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(scale.stopTimerCalls == 1);
  CHECK(session.timerStopResult == TimerStopResult::WRITE_SUCCEEDED);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  scale.tareStartTimerSucceeds = false;
  CHECK(executeNextScaleCommand());
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.resetTimerCalls == 0);
  CHECK(scale.startTimerCalls == 0);
  CHECK(scale.tareCalls == 0);
  CHECK(!session.remoteTimerStarted);
  loop();
  CHECK(!session.automaticEnabled);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  scale.stopTimerSucceeds = false;
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(executeNextScaleCommand());
  CHECK(scale.stopTimerCalls == 1);
  CHECK(session.timerStopResult == TimerStopResult::WRITE_FAILED);
}

void r05_regression_uses_last_ten_valid_samples() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  resetShotTrajectory(session.startedAtMs);
  session.receivedFreshWeightInCycle = false;

  for (size_t i = 1; i <= TREND_POINT_COUNT; ++i) {
    recordWeightSample(static_cast<float>(i) * 2.0f,
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  CHECK(shot.datapoints == TREND_POINT_COUNT);
  CHECK(fabsf(shot.expectedEndS - 17.25f) < 0.001f);

  resetShotTrajectory(session.startedAtMs);
  for (size_t i = 0; i < TREND_POINT_COUNT; ++i) {
    recordWeightSample(20.0f - static_cast<float>(i),
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  CHECK(shot.expectedEndS == HARD_MAX_CN9_CLOSED_MS / 1000.0f);
}

void r06_hard_timer_opens_cn9_without_control_loop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);

  hostMillis = cn9ClosedAtMs + HARD_MAX_CN9_CLOSED_MS;
  hostServiceEspTimer(relaySafetyTimer);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().tripped);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
  CHECK(stopperState == StopperState::QUALIFYING_ON);

  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
}

void r07_timing_remains_correct_across_millis_wrap() {
  resetHarness(false, false);
  hostMillis = UINT32_MAX - 100;
  rawPaddleChangedAtMs = hostMillis;
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(elapsedMs(session.startedAtMs) == runtimeConfig.brewConfirmMs);
  reachSessionElapsed(HARD_MAX_CN9_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
}

void r08_full_command_queue_forces_safe_manual_cycle() {
  resetHarness(false, true);
  reachReadyFromBoot();
  ScaleCommand filler;
  filler.type = ScaleCommandType::STOP_TIMER;
  for (size_t i = 0; i < SCALE_COMMAND_QUEUE_LENGTH; ++i) {
    CHECK(enqueueScaleCommand(filler));
  }
  CHECK(scaleCommandQueue->items.size() == SCALE_COMMAND_QUEUE_LENGTH);

  startCycle();
  CHECK(!session.timerStartCommandQueued);
  CHECK(!session.automaticEnabled);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
}

void r09_stop_is_not_retried_after_disconnect_before_execution() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  // Link-state publication can lag the library's own connection flag.
  scale.connected = false;
  CHECK(executeNextScaleCommand());
  CHECK(scale.stopTimerCalls == 0);
  CHECK(session.timerStopResult == TimerStopResult::NOT_ATTEMPTED);

  setScaleConnected(true);
  loop();
  CHECK(scale.stopTimerCalls == 0);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void r10_relay_cannot_close_when_hard_timer_cannot_arm() {
  resetHarness(false, false);
  reachReadyFromBoot();
  delete relaySafetyTimer;
  relaySafetyTimer = nullptr;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::RELAY_SAFETY_FAILURE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == 0);

  resetHarness(false, false);
  reachReadyFromBoot();
  hostEspTimerStartSucceeds = false;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::RELAY_SAFETY_FAILURE);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
  CHECK(hostRelayClosedWrites == 0);
}

void r11_final_shot_analysis_updates_only_valid_offset() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const float originalOffset = runtimeConfig.weightOffsetG;
  pendingAnalysis.pending = true;
  pendingAnalysis.endedAtMs = hostMillis;
  pendingAnalysis.endedWeightSequence = 0;
  pendingAnalysis.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingAnalysis.weightOffsetG = originalOffset;
  currentWeight = DEFAULT_GOAL_WEIGHT_G + 1.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(DRIP_DELAY_MS);
  finishHostMaintenance();
  CHECK(fabsf(runtimeConfig.weightOffsetG - 2.5f) < 0.001f);

  const float validOffset = runtimeConfig.weightOffsetG;
  pendingAnalysis.pending = true;
  pendingAnalysis.endedAtMs = hostMillis;
  pendingAnalysis.endedWeightSequence = currentWeightSequence;
  pendingAnalysis.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingAnalysis.weightOffsetG = validOffset;
  currentWeight = DEFAULT_GOAL_WEIGHT_G + MAX_OFFSET_G + 1.0f;
  ++currentWeightSequence;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(DRIP_DELAY_MS);
  CHECK(runtimeConfig.weightOffsetG == validOffset);
}

void r12_scale_worker_service_publishes_weight_and_detects_failure() {
  resetHarness(false, true);
  scale.weight = 12.34f;
  scale.newWeightAvailableValue = true;
  serviceScaleWorkerLink();
  processScaleWorkerEvents();
  CHECK(scale.newWeightAvailableCalls == 1);
  CHECK(currentWeightSequence == 1);
  CHECK(fabsf(currentWeight - 12.34f) < 0.001f);
  CHECK(scaleAvailable());

  scale.heartbeatRequiredValue = true;
  scale.heartbeatSucceeds = false;
  serviceScaleWorkerLink();
  CHECK(scale.heartbeatCalls == 1);
  CHECK(!scaleAvailable());
  CHECK(getScaleLinkSnapshot().disconnectSequence == 1);
}

void r13_full_queue_prevents_stop_without_delaying_relay_open() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  reachSessionElapsed(runtimeConfig.brewConfirmMs);

  ScaleCommand filler;
  filler.type = ScaleCommandType::START_TIMER_AND_TARE;
  for (size_t i = 0; i < SCALE_COMMAND_QUEUE_LENGTH; ++i) {
    CHECK(enqueueScaleCommand(filler));
  }
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(session.stopTimerRequested);
  CHECK(session.timerStopResult == TimerStopResult::NOT_ATTEMPTED);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void r14_invalid_runtime_configuration_is_transactionally_rejected() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const RuntimeConfig original = runtimeConfig;
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.goalWeightG = MIN_GOAL_WEIGHT_G - 1;
  CHECK(validateRuntimeConfig(update.config) ==
        ConfigValidationError::GOAL_WEIGHT);
  CHECK(enqueueWebCommand(update));
  loop();
  CHECK(runtimeConfig.goalWeightG == original.goalWeightG);
  CHECK(runtimeConfig.revision == original.revision);

  update.config = runtimeConfig;
  update.config.operationalWallMs = HARD_MAX_CN9_CLOSED_MS + 1;
  CHECK(validateRuntimeConfig(update.config) ==
        ConfigValidationError::OPERATIONAL_WALL);
  CHECK(enqueueWebCommand(update));
  loop();
  CHECK(runtimeConfig.operationalWallMs == original.operationalWallMs);
}

void r15_gptimer_opens_cn9_without_arduino_or_esp_timer_tasks() {
  resetHarness(false, false);
  CHECK(setCn9Closed(true, 5000));
  hostMillis += 5000;
  independentSafetyTimer.serviceForHost();

  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.operationalTripped);
  CHECK(relay.state == RelaySafetyState::TRIPPED);
  CHECK(relay.fault == RelaySafetyFault::OPERATIONAL_LIMIT);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

void r16_timeout_during_arm_transaction_can_never_close_cn9() {
  resetHarness(false, false);
  hostCn9ArmBeforeCommitHook = []() {
    ++hostMillis;
    independentSafetyTimer.serviceForHost();
  };

  CHECK(!setCn9Closed(true, 1));
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.operationalTripped);
  CHECK(relay.state == RelaySafetyState::TRIPPED);
  CHECK(hostRelayClosedWrites == 0);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

void r17_gptimer_arm_failure_prevents_relay_energization() {
  resetHarness(false, false);
  hostGptimerArmSucceeds = false;

  CHECK(!setCn9Closed(true, 5000));
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::TIMER_ARM_FAILED);
  CHECK(hostRelayClosedWrites == 0);
}

void r18_watchdog_fault_opens_cn9_and_requests_safe_restart() {
  resetHarness(false, false);
  CHECK(setCn9Closed(true, 5000));

  reportTaskWatchdogFault();
  serviceRelaySafety();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::TASK_WATCHDOG_FAILURE);
  CHECK(safeRestartRequested);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

void r19_reset_during_close_requires_local_on_off_recovery() {
  resetHarness(false, false);
  recordRelayCommandedClosed(true);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  safetyResetStatus = beginSafetyResetGuard();
  relaySafetyState = RelaySafetyState::LOCKOUT;
  relaySafetyFault = RelaySafetyFault::RESET_DURING_CLOSE;

  CHECK(safetyResetStatus.resetDuringClose);
  CHECK(safetyResetStatus.recoveryRequired);
  serviceRelaySafety();
  CHECK(relaySafetyState == RelaySafetyState::LOCKOUT);

  hostPinLevel[PADDLE_GPIO] = PADDLE_ACTIVE_LEVEL;
  serviceRelaySafety();
  hostPinLevel[PADDLE_GPIO] = !PADDLE_ACTIVE_LEVEL;
  ++hostMillis;
  serviceRelaySafety();
  hostMillis += RESET_RECOVERY_OFF_DWELL_MS;
  serviceRelaySafety();

  CHECK(relaySafetyState == RelaySafetyState::OPEN);
  CHECK(relaySafetyFault == RelaySafetyFault::NONE);
  CHECK(!safetyResetStatus.recoveryRequired);
  CHECK(safetyResetStatus.unsafeResetCount == 0);
}

void r20_three_unsafe_resets_are_latched_as_a_boot_loop() {
  resetHarness(false, false);
  hostSafetyResetReasonCode = 6;
  hostSafetyResetReasonUnsafe = true;

  SafetyResetSnapshot reset;
  for (uint32_t count = 1; count <= SAFETY_BOOT_LOOP_THRESHOLD; ++count) {
    reset = beginSafetyResetGuard();
    CHECK(reset.unsafeResetCount == count);
  }
  CHECK(reset.bootLoopDetected);
  CHECK(reset.recoveryRequired);
}

void w01_default_runtime_configuration_is_valid() {
  const RuntimeConfig config;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  CHECK(config.operationalWallMs == HARD_MAX_CN9_CLOSED_MS);
  CHECK(config.rinseGestureMs == 1500);
  CHECK(config.canTareStartTimer);
  CHECK(config.brewConfirmationBeep);
  CHECK(config.paddleReturnReminderBeep);
}

void w02_each_runtime_field_is_validated() {
  RuntimeConfig config;
  config.goalWeightG = 9;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::GOAL_WEIGHT);
  config = RuntimeConfig{};
  config.weightOffsetG = std::numeric_limits<float>::quiet_NaN();
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::WEIGHT_OFFSET);
  config = RuntimeConfig{};
  config.rinseGestureMs = 99;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::RINSE_GESTURE);
  config = RuntimeConfig{};
  config.rinseDurationMs = 499;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::RINSE_DURATION);
  config = RuntimeConfig{};
  config.brewConfirmMs = 10001;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::BREW_CONFIRM);
  config = RuntimeConfig{};
  config.minAutoStopMs = 30001;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::MIN_AUTO_STOP);
}

void w03_runtime_timing_relations_are_transactional() {
  RuntimeConfig config;
  config.rinseGestureMs = config.brewConfirmMs;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::TIMING_RELATION);
  config = RuntimeConfig{};
  config.canTareStartTimer = true;
  config.autoTare = false;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE);
}

void w04_wifi_credentials_have_strict_bounds() {
  CHECK(validWifiSsid("Micra"));
  CHECK(!validWifiSsid(""));
  CHECK(validWifiPassword("12345678", false));
  CHECK(!validWifiPassword("1234", false));
  CHECK(validWifiPassword("", true));
  CHECK(validAccessPointPassword("Micra1234"));
}

void attemptActiveConfigUpdate() {
  const RuntimeConfig before = runtimeConfig;
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = before;
  update.config.goalWeightG = before.goalWeightG + 1;
  processWebCommand(update);
  CHECK(runtimeConfig.goalWeightG == before.goalWeightG);
  CHECK(runtimeConfig.revision == before.revision);
}

void w05_config_is_blocked_while_qualifying() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(stopperState == StopperState::QUALIFYING_ON);
  attemptActiveConfigUpdate();
}

void w06_config_is_blocked_while_brewing() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  attemptActiveConfigUpdate();
}

void w07_config_is_blocked_while_rinsing() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 400);
  CHECK(stopperState == StopperState::RINSE);
  attemptActiveConfigUpdate();
}

void w08_config_is_blocked_during_manual_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  attemptActiveConfigUpdate();
}

void w09_valid_config_applies_only_from_ready() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.weightOffsetG = 2.25f;
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.goalWeightG = 42;
  update.config.weightOffsetG = 4.5f;  // Not a Web-editable field.
  const uint32_t oldRevision = runtimeConfig.revision;
  processWebCommand(update);
  finishHostMaintenance();
  CHECK(runtimeConfig.goalWeightG == 42);
  CHECK(fabsf(runtimeConfig.weightOffsetG - 2.25f) < 0.001f);
  CHECK(runtimeConfig.revision == oldRevision + 1);
}

void w10_cycle_configuration_snapshot_is_immutable() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  const CycleConfigSnapshot frozen = session.config;
  runtimeConfig.goalWeightG = frozen.goalWeightG + 10;
  runtimeConfig.operationalWallMs = 10000;
  CHECK(session.config.goalWeightG == frozen.goalWeightG);
  CHECK(session.config.operationalWallMs == frozen.operationalWallMs);
  runtimeConfig.brewConfirmationBeep = !frozen.brewConfirmationBeep;
  CHECK(session.config.brewConfirmationBeep == frozen.brewConfirmationBeep);
}

void w11_operational_timer_opens_without_control_loop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.operationalWallMs = 6000;
  CHECK(validateRuntimeConfig(runtimeConfig) == ConfigValidationError::NONE);
  startCycle();
  hostMillis = cn9ClosedAtMs + runtimeConfig.operationalWallMs;
  hostServiceEspTimer(operationalLimitTimer);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().operationalTripped);
  CHECK(stopperState == StopperState::QUALIFYING_ON);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::CONFIGURED_WALL_LIMIT);
}

void w12_hard_limit_cannot_be_configured_above_sixty_seconds() {
  RuntimeConfig config;
  config.operationalWallMs = HARD_MAX_CN9_CLOSED_MS + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::OPERATIONAL_WALL);
}

void w13_virtual_paddle_uses_normal_state_machine() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::PADDLE_ON);
  processWebCommand(on);
  CHECK(session.active);
  CHECK(session.source == ControlSource::WEB);
  CHECK(virtualPaddleOn);
  CHECK(stopperState == StopperState::QUALIFYING_ON);
  CHECK(getRelaySafetySnapshot().closed);
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  WebCommand off = webControlCommand(WebCommandType::PADDLE_OFF);
  processWebCommand(off);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w14_physical_motion_overrides_web_control() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::PADDLE_ON);
  processWebCommand(on);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::PHYSICAL_OVERRIDE);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w15_web_rinse_skips_scale_timer() {
  resetHarness(false, true);
  reachReadyFromBoot();
  WebCommand rinse = webControlCommand(WebCommandType::RINSE);
  processWebCommand(rinse);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(session.source == ControlSource::WEB);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  runLoopAfter(runtimeConfig.rinseDurationMs);
  CHECK(stopperState == StopperState::READY);

  processWebCommand(rinse);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::PHYSICAL_OVERRIDE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
}

void w16_web_stop_during_rinse_preserves_rearm() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 400);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  WebCommand stop;
  stop.type = WebCommandType::STOP;
  processWebCommand(stop);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::WEB_STOP);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w17_web_heartbeat_timeout_is_a_safe_stop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::PADDLE_ON);
  processWebCommand(on);
  WebCommand timeout;
  timeout.type = WebCommandType::STOP_HEARTBEAT;
  processWebCommand(timeout);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::WEB_HEARTBEAT_TIMEOUT);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w18_web_stop_can_end_a_physical_brew_only_by_opening() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  WebCommand stop;
  stop.type = WebCommandType::STOP;
  processWebCommand(stop);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::WEB_STOP);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w19_web_start_is_rejected_outside_ready() {
  resetHarness(true, false);
  WebCommand on = webControlCommand(WebCommandType::PADDLE_ON);
  processWebCommand(on);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w20_restart_is_rejected_while_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  const RelaySafetySnapshot before = getRelaySafetySnapshot();
  WebCommand restart;
  restart.type = WebCommandType::RESTART;
  processWebCommand(restart);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed == before.closed);
  CHECK(stopperState == StopperState::QUALIFYING_ON);
}

void w21_network_change_is_rejected_while_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  WebCommand network;
  network.type = WebCommandType::SAVE_NETWORK;
  strcpy(network.ssid, "test");
  strcpy(network.password, "password");
  processWebCommand(network);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
}

void w22_timer_only_disables_predictive_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.timerOnly = true;
  startCycle();
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(!scaleBeepPending);
  shot.expectedEndS = 0.0f;
  reachSessionElapsed(runtimeConfig.minAutoStopMs);
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
}

void w23_combined_tare_command_uses_cycle_snapshot() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoTare = true;
  runtimeConfig.canTareStartTimer = true;
  startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.resetTimerCalls == 0);
  CHECK(scale.startTimerCalls == 0);
}

void w24_debug_ring_is_bounded_and_ordered() {
  DebugRingBuffer ring;
  for (size_t index = 0; index < DEBUG_EVENT_CAPACITY + 5; ++index) {
    ring.add(static_cast<uint32_t>(index), DebugCategory::WEB,
             DebugCode::WEB_COMMAND_ACCEPTED, static_cast<int32_t>(index));
  }
  CHECK(ring.overwritten() == 5);
  DebugEvent events[DEBUG_EVENT_CAPACITY] = {};
  const size_t copied = ring.copyAfter(0, events, DEBUG_EVENT_CAPACITY);
  CHECK(copied == DEBUG_EVENT_CAPACITY);
  CHECK(events[0].argument1 == 5);
  CHECK(events[copied - 1].argument1 ==
        static_cast<int32_t>(DEBUG_EVENT_CAPACITY + 4));
}

void w25_weight_samples_do_not_fill_debug_log() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  DebugEvent existing[DEBUG_EVENT_CAPACITY] = {};
  const size_t count = copyDebugEvents(0, existing, DEBUG_EVENT_CAPACITY);
  const uint32_t lastSequence = count == 0 ? 0 : existing[count - 1].sequence;
  recordWeightSample(12.0f, session.startedAtMs + 1000);
  DebugEvent newEvents[2] = {};
  CHECK(copyDebugEvents(lastSequence, newEvents, 2) == 0);
}

void w26_status_is_a_copied_snapshot() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.activeCycle);
  CHECK(status.relayClosed);
  CHECK(status.source == ControlSource::PHYSICAL);
  CHECK(status.config.revision == session.config.revision);
}

void w27_stale_weight_is_not_presented_as_current() {
  resetHarness(false, true);
  currentWeight = 12.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = 0;
  hostMillis = SCALE_WORKER_STALE_MS + 1;
  scaleWorkerProgressAtMs = hostMillis;
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(!status.currentWeightValid);
}

void w28_web_command_queue_is_bounded() {
  resetHarness(false, false);
  WebCommand command;
  command.type = WebCommandType::STOP;
  for (size_t index = 0; index < WEB_COMMAND_QUEUE_LENGTH; ++index) {
    CHECK(enqueueWebCommand(command));
  }
  CHECK(!enqueueWebCommand(command));
  CHECK(!getRelaySafetySnapshot().closed);
}

void w29_operational_wall_of_60001_is_rejected() {
  RuntimeConfig config;
  config.operationalWallMs = 60001;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::OPERATIONAL_WALL);
}

void w30_last_cycle_weight_must_belong_to_that_cycle() {
  resetHarness(false, false);
  currentWeight = 99.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis;
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 100);
  CHECK(lastCycle.valid);
  CHECK(!lastCycle.weightValid);

  startCycle();
  ScaleEvent event;
  event.type = ScaleEventType::WEIGHT;
  event.receivedAtMs = hostMillis + 1;
  event.weightG = 7.5f;
  publishScaleEvent(event, true);
  processScaleWorkerEvents();
  releaseAtPhysicalDuration(rawPaddleChangedAtMs,
                            runtimeConfig.rinseGestureMs + 100);
  CHECK(lastCycle.weightValid);
  CHECK(fabsf(lastCycle.lastWeightG - 7.5f) < 0.001f);
}

void w31_unsupported_scale_never_uses_tare_as_a_beep() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  scale.independentBeepSupported = false;
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  CHECK(scaleBeepPending);
  CHECK(executePendingScaleBrewBeep());
  CHECK(scale.beepCalls == 0);
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.tareCalls == 0);
  CHECK(scale.connected);
  CHECK(session.automaticEnabled);
}

void w32_full_scale_queue_cannot_block_brew_confirmation() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  ScaleCommand filler;
  filler.type = ScaleCommandType::STOP_TIMER;
  for (size_t index = 0; index < SCALE_COMMAND_QUEUE_LENGTH; ++index) {
    CHECK(xQueueSend(scaleCommandQueue, &filler, 0) == pdTRUE);
  }
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  CHECK(shot.confirmedBrew);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(scaleBeepPending);
  CHECK(scale.beepCalls == 0);
}

void w33_brew_confirmation_beep_can_be_disabled() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.brewConfirmationBeep = false;
  startCycle();
  CHECK(executeNextScaleCommand());
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::BREW);
  CHECK(!scaleBeepPending);
  CHECK(scale.beepCalls == 0);
}

void w34_calibration_reset_restores_default_and_cancels_analysis() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.weightOffsetG = 3.2f;
  const uint32_t previousRevision = runtimeConfig.revision;
  pendingAnalysis.pending = true;
  WebCommand reset;
  reset.type = WebCommandType::RESET_WEIGHT_OFFSET;
  processWebCommand(reset);
  finishHostMaintenance();
  CHECK(fabsf(runtimeConfig.weightOffsetG - DEFAULT_WEIGHT_OFFSET_G) <
        0.001f);
  CHECK(runtimeConfig.revision == previousRevision + 1);
  CHECK(!pendingAnalysis.pending);
}

void w35_status_reports_the_live_physical_paddle_gpio() {
  resetHarness(false, false);
  reachReadyFromBoot();
  hostPinLevel[PADDLE_GPIO] = PADDLE_ACTIVE_LEVEL;
  CHECK(!rawPaddleOn);
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.physicalPaddleOn);
}

void w36_paddle_return_reminder_beeps_every_fifteen_seconds_only_while_open() {
  resetHarness(true, true);
  runLoopAfter(0);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!scalePaddleReturnReminderBeepPending);
  runLoopAfter(PADDLE_RETURN_REMINDER_BEEP_INTERVAL_MS - 1);
  CHECK(!scalePaddleReturnReminderBeepPending);
  runLoopAfter(1);
  CHECK(scalePaddleReturnReminderBeepPending);
  CHECK(executePendingScalePaddleReturnReminderBeep());
  CHECK(scale.beepCalls == 1);

  setRawPaddle(false);
  CHECK(!scalePaddleReturnReminderBeepPending);
  runLoopAfter(PADDLE_RETURN_REMINDER_BEEP_INTERVAL_MS);
  CHECK(!scalePaddleReturnReminderBeepPending);
}

void w37_factory_reset_is_rejected_while_control_is_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  DebugEvent before[DEBUG_EVENT_CAPACITY] = {};
  const size_t beforeCount =
      copyDebugEvents(0, before, DEBUG_EVENT_CAPACITY);
  const uint32_t afterSequence =
      beforeCount == 0 ? 0 : before[beforeCount - 1].sequence;

  WebCommand reset;
  reset.type = WebCommandType::FACTORY_RESET;
  processWebCommand(reset);

  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(stopperState == StopperState::QUALIFYING_ON);
  DebugEvent events[4] = {};
  const size_t count = copyDebugEvents(afterSequence, events, 4);
  bool rejected = false;
  for (size_t index = 0; index < count; ++index) {
    rejected |= events[index].code == DebugCode::WEB_COMMAND_REJECTED &&
                events[index].argument1 ==
                    static_cast<int32_t>(WebCommandType::FACTORY_RESET);
  }
  CHECK(rejected);
}

void r21_automatic_control_requires_fresh_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();

  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(session.active);
  CHECK(!session.startedWithScale);
  CHECK(!session.automaticEnabled);
  CHECK(getRelaySafetySnapshot().closed);

  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 0.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis;
  markScaleWorkerProgress();
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(session.startedWithScale);
  CHECK(!session.receivedFreshWeightInCycle);
  hostAutoScaleWorkerProgress = false;
  reachSessionElapsed(runtimeConfig.brewConfirmMs);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(!session.automaticEnabled);
  CHECK(getRelaySafetySnapshot().closed);
}

void r22_implausible_weight_slew_degrades_to_manual() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.automaticEnabled);
  recordWeightSample(500.0f, session.lastAcceptedWeightAtMs + 1);
  CHECK(!session.automaticEnabled);
  CHECK(session.scaleWasLost);
  CHECK(getRelaySafetySnapshot().closed);
}

void r23_maintenance_is_canceled_fail_open_by_physical_paddle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const RuntimeConfig original = runtimeConfig;
  WebCommand command;
  command.type = WebCommandType::APPLY_CONFIG;
  command.requestId = 77;
  command.config = runtimeConfig;
  command.config.goalWeightG = runtimeConfig.goalWeightG + 1;
  processWebCommand(command);
  CHECK(maintenanceLease.active);
  CHECK(!maintenanceLease.forwarded);
  CHECK(!getRelaySafetySnapshot().closed);

  setRawPaddle(true);
  CHECK(!maintenanceLease.active);
  CHECK(!maintenanceCancellationPending);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(runtimeConfig.goalWeightG == original.goalWeightG);
  CHECK(!getRelaySafetySnapshot().closed);
}

void r24_web_control_lease_owner_is_enforced() {
  resetHarness(false, false);
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::PADDLE_ON));
  CHECK(session.active);
  CHECK(session.source == ControlSource::WEB);
  CHECK(getRelaySafetySnapshot().closed);

  WebCommand wrongOwner = webControlCommand(WebCommandType::PADDLE_OFF);
  wrongOwner.webSessionId += 1;
  processWebCommand(wrongOwner);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);

  WebCommand emergencyStop;
  emergencyStop.type = WebCommandType::STOP;
  processWebCommand(emergencyStop);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
}

void r25_critical_scale_mailbox_never_blocks_and_keeps_latest() {
  resetHarness(false, true);
  ScaleEvent first;
  first.type = ScaleEventType::TIMER_STOP_RESULT;
  first.cycleId = 1;
  first.commandAttempted = true;
  ScaleEvent latest = first;
  latest.cycleId = 2;
  CHECK(publishScaleEvent(first, true));
  CHECK(publishScaleEvent(latest, true));
  CHECK(scaleCriticalEventPending);
  CHECK(scaleEventsDropped == 1);
  CHECK(scaleCriticalEvent.cycleId == 2);
  processScaleWorkerEvents();
  CHECK(!scaleCriticalEventPending);
}

void r26_remote_timer_stop_retries_after_full_queue() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());

  ScaleCommand filler;
  filler.type = ScaleCommandType::STOP_TIMER;
  filler.cycleId = UINT32_MAX;
  for (size_t index = 0; index < SCALE_COMMAND_QUEUE_LENGTH; ++index) {
    CHECK(xQueueSend(scaleCommandQueue, &filler, 0) == pdTRUE);
  }
  WebCommand stop;
  stop.type = WebCommandType::STOP;
  processWebCommand(stop);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(session.stopTimerRequested);
  CHECK(!session.stopTimerCommandQueued);

  ScaleCommand discarded;
  while (xQueueReceive(scaleCommandQueue, &discarded, 0) == pdTRUE) {
  }
  runLoopAfter(SCALE_STOP_RETRY_INTERVAL_MS);
  CHECK(session.stopTimerCommandQueued);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void r27_platform_clock_failure_prevents_cn9_close() {
  resetHarness(false, false);
  reachReadyFromBoot();
  platformClockReady = false;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::LOCKOUT);
  CHECK(getRelaySafetySnapshot().fault ==
        RelaySafetyFault::INITIALIZATION_FAILED);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
}

void r28_terminal_control_result_is_retained_until_forwarded() {
  resetHarness(false, false);
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::PADDLE_ON));
  CHECK(session.active);
  hostForwardAcceptedNetworkCommandSucceeds = false;

  WebCommand stop;
  stop.type = WebCommandType::STOP;
  stop.requestId = 91;
  processWebCommand(stop);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(controlResultPending);
  CHECK(controlResultCommand.requestId == 91);
  CHECK(controlResultCommand.resultState == CommandResultState::APPLIED);

  hostForwardAcceptedNetworkCommandSucceeds = true;
  serviceControlCommandResult();
  CHECK(!controlResultPending);
  CHECK(hostLastForwardedNetworkCommand.requestId == 91);
  CHECK(hostLastForwardedNetworkCommand.resultState ==
        CommandResultState::APPLIED);
}

void w38_scale_indicator_states_are_unambiguous() {
  const IndicatorSignal starting =
      scaleIndicatorSignal(ScaleIndicatorCondition::STARTING);
  const IndicatorSignal available =
      scaleIndicatorSignal(ScaleIndicatorCondition::AVAILABLE);
  const IndicatorSignal disconnected =
      scaleIndicatorSignal(ScaleIndicatorCondition::DISCONNECTED);
  const IndicatorSignal stale =
      scaleIndicatorSignal(ScaleIndicatorCondition::STALE);
  const IndicatorSignal fault =
      scaleIndicatorSignal(ScaleIndicatorCondition::FAULT);

  CHECK(starting.color == INDICATOR_BLUE);
  CHECK(starting.pattern == IndicatorPattern::SLOW_BLINK);
  CHECK(available.color == INDICATOR_GREEN);
  CHECK(available.pattern == IndicatorPattern::SOLID);
  CHECK(disconnected.color == INDICATOR_RED);
  CHECK(disconnected.pattern == IndicatorPattern::SOLID);
  CHECK(stale.color == INDICATOR_YELLOW);
  CHECK(stale.pattern == IndicatorPattern::SLOW_BLINK);
  CHECK(fault.color == INDICATOR_RED);
  CHECK(fault.pattern == IndicatorPattern::FAST_BLINK);
}

void w39_automatic_stopper_palette_encodes_workflow() {
  const auto signalFor = [](StopperState state) {
    return stopperIndicatorSignal(state, RelaySafetyState::OPEN, true, false,
                                  false);
  };

  CHECK(signalFor(StopperState::READY).color == INDICATOR_GREEN);
  CHECK(signalFor(StopperState::READY).pattern == IndicatorPattern::SOLID);
  CHECK(signalFor(StopperState::QUALIFYING_ON).pattern ==
        IndicatorPattern::MEDIUM_BLINK);
  CHECK(signalFor(StopperState::BREW).pattern ==
        IndicatorPattern::SLOW_BLINK);
  CHECK(signalFor(StopperState::RINSE).pattern ==
        IndicatorPattern::FAST_BLINK);
}

void w40_manual_and_timer_only_palette_is_salmon() {
  const auto signalFor = [](StopperState state) {
    return stopperIndicatorSignal(state, RelaySafetyState::OPEN, true, false,
                                  true);
  };

  CHECK(signalFor(StopperState::READY).color == INDICATOR_SALMON);
  CHECK(signalFor(StopperState::READY).pattern == IndicatorPattern::SOLID);
  CHECK(signalFor(StopperState::QUALIFYING_ON).color == INDICATOR_SALMON);
  CHECK(signalFor(StopperState::QUALIFYING_ON).pattern ==
        IndicatorPattern::MEDIUM_BLINK);
  CHECK(signalFor(StopperState::BREW).color == INDICATOR_SALMON);
  CHECK(signalFor(StopperState::BREW).pattern ==
        IndicatorPattern::SLOW_BLINK);
  CHECK(signalFor(StopperState::RINSE).color == INDICATOR_SALMON);
  CHECK(signalFor(StopperState::RINSE).pattern ==
        IndicatorPattern::FAST_BLINK);

  const IndicatorSignal noScale = stopperIndicatorSignal(
      StopperState::MANUAL_NO_SCALE, RelaySafetyState::OPEN, true, false,
      false);
  CHECK(noScale.color == INDICATOR_SALMON);
  CHECK(noScale.pattern == IndicatorPattern::SLOW_BLINK);
}

void w41_safety_and_action_states_override_operating_palette() {
  const IndicatorSignal boot = stopperIndicatorSignal(
      StopperState::REQUIRES_OFF, RelaySafetyState::BOOT_SAFE, false, false,
      false);
  CHECK(boot.color == INDICATOR_BLUE);
  CHECK(boot.pattern == IndicatorPattern::SLOW_BLINK);

  const IndicatorSignal unavailable = stopperIndicatorSignal(
      StopperState::READY, RelaySafetyState::OPEN, false, false, false);
  CHECK(unavailable.color == INDICATOR_RED);
  CHECK(unavailable.pattern == IndicatorPattern::FAST_BLINK);

  const IndicatorSignal tripped = stopperIndicatorSignal(
      StopperState::READY, RelaySafetyState::TRIPPED, true, false, false);
  CHECK(tripped.color == INDICATOR_RED);
  CHECK(tripped.pattern == IndicatorPattern::FAST_BLINK);

  const IndicatorSignal locked = stopperIndicatorSignal(
      StopperState::READY, RelaySafetyState::LOCKOUT, true, false, false);
  CHECK(locked.color == INDICATOR_RED);
  CHECK(locked.pattern == IndicatorPattern::FAST_BLINK);

  const IndicatorSignal maintenance = stopperIndicatorSignal(
      StopperState::READY, RelaySafetyState::OPEN, true, true, false);
  CHECK(maintenance.color == INDICATOR_BLUE);
  CHECK(maintenance.pattern == IndicatorPattern::SLOW_BLINK);

  const IndicatorSignal requiresOff = stopperIndicatorSignal(
      StopperState::REQUIRES_OFF, RelaySafetyState::OPEN, true, false, true);
  CHECK(requiresOff.color == INDICATOR_AMBER);
  CHECK(requiresOff.pattern == IndicatorPattern::SLOW_BLINK);
}

void w42_indicator_blink_periods_are_deterministic() {
  const IndicatorSignal slow = {INDICATOR_GREEN,
                                IndicatorPattern::SLOW_BLINK};
  CHECK(renderIndicatorSignal(slow, 0) == INDICATOR_GREEN);
  CHECK(renderIndicatorSignal(slow, INDICATOR_SLOW_HALF_PERIOD_MS - 1) ==
        INDICATOR_GREEN);
  CHECK(renderIndicatorSignal(slow, INDICATOR_SLOW_HALF_PERIOD_MS) ==
        INDICATOR_OFF);
  CHECK(renderIndicatorSignal(slow, INDICATOR_SLOW_HALF_PERIOD_MS * 2) ==
        INDICATOR_GREEN);

  const IndicatorSignal medium = {INDICATOR_GREEN,
                                  IndicatorPattern::MEDIUM_BLINK};
  CHECK(renderIndicatorSignal(medium, INDICATOR_MEDIUM_HALF_PERIOD_MS) ==
        INDICATOR_OFF);
  const IndicatorSignal fast = {INDICATOR_GREEN,
                                IndicatorPattern::FAST_BLINK};
  CHECK(renderIndicatorSignal(fast, INDICATOR_FAST_HALF_PERIOD_MS) ==
        INDICATOR_OFF);
  CHECK(renderIndicatorSignal({INDICATOR_GREEN, IndicatorPattern::SOLID},
                              std::numeric_limits<uint32_t>::max()) ==
        INDICATOR_GREEN);
}

void w43_manual_palette_tracks_timer_only_and_scale_loss() {
  resetHarness(false, true);
  stopperState = StopperState::READY;
  runtimeConfig.timerOnly = false;
  CHECK(!stopperUsesManualIndicatorPalette());

  runtimeConfig.timerOnly = true;
  CHECK(stopperUsesManualIndicatorPalette());

  runtimeConfig.timerOnly = false;
  setScaleLinkState(ScaleLinkState::DISCONNECTED);
  CHECK(stopperUsesManualIndicatorPalette());

  session.active = true;
  session.config.timerOnly = false;
  session.startedWithScale = true;
  session.scaleWasLost = false;
  CHECK(!stopperUsesManualIndicatorPalette());
  session.scaleWasLost = true;
  CHECK(stopperUsesManualIndicatorPalette());
}

using TestFunction = void (*)();

struct TestCase {
  const char *id;
  TestFunction function;
};

const TestCase testCases[] = {
    {"T01", t01_boot_with_paddle_off},
    {"T02", t02_boot_with_paddle_on},
    {"T03", t03_sustained_on_confirms_brew_once},
    {"T04", t04_exact_rinse_boundary_and_duration},
    {"T05", t05_release_between_rinse_and_brew_is_short_shot},
    {"T06", t06_paddle_off_during_brew},
    {"T07", t07_scale_prediction_requires_release_after_stop},
    {"T08", t08_on_during_rinse_is_ignored},
    {"T09", t09_rinse_ending_on_requires_off},
    {"T10", t10_paddle_bounce_does_not_start_cycle},
    {"T11", t11_ble_loss_degrades_brew_without_late_stop},
    {"T12", t12_global_limit_opens_manual_and_brew_cycles},
    {"T13", t13_reset_path_starts_with_relay_open},
    {"T14", t14_automatic_stop_stays_open_while_paddle_on},
    {"T15", t15_repeated_rinse_and_brew_reset_session_state},
    {"T16", t16_only_micra_states_are_compiled},
    {"T17", t17_simultaneous_global_limit_and_paddle_off_is_idempotent},
    {"T18", t18_rinse_and_short_shot_each_request_one_stop},
    {"T19", t19_manual_cycle_without_scale_has_no_timer_commands},
    {"T20", t20_rinse_without_scale},
    {"T21", t21_global_limit_without_scale_rearms_after_release},
    {"T22", t22_scale_connection_does_not_promote_manual_cycle},
    {"T23", t23_prediction_before_minimum_is_reevaluated_at_minimum},
    {"T24", t24_paddle_off_before_minimum_is_immediate},
    {"T25", t25_ble_loss_while_qualifying_preserves_classification},
    {"T26", t26_reconnected_degraded_cycle_sends_one_stop_on_release},
    {"T27", t27_configuration_is_rejected_while_cycle_is_active},
    {"T28", t28_paddle_motion_cannot_cancel_or_extend_rinse},
    {"R01", r01_transient_disconnect_is_latched_for_cycle},
    {"R02", r02_stalled_scale_worker_degrades_cycle_permanently},
    {"R03", r03_non_finite_weights_cannot_corrupt_state_or_offset},
    {"R04", r04_scale_commands_execute_once_and_report_results},
    {"R05", r05_regression_uses_last_ten_valid_samples},
    {"R06", r06_hard_timer_opens_cn9_without_control_loop},
    {"R07", r07_timing_remains_correct_across_millis_wrap},
    {"R08", r08_full_command_queue_forces_safe_manual_cycle},
    {"R09", r09_stop_is_not_retried_after_disconnect_before_execution},
    {"R10", r10_relay_cannot_close_when_hard_timer_cannot_arm},
    {"R11", r11_final_shot_analysis_updates_only_valid_offset},
    {"R12", r12_scale_worker_service_publishes_weight_and_detects_failure},
    {"R13", r13_full_queue_prevents_stop_without_delaying_relay_open},
    {"R14", r14_invalid_runtime_configuration_is_transactionally_rejected},
    {"R15", r15_gptimer_opens_cn9_without_arduino_or_esp_timer_tasks},
    {"R16", r16_timeout_during_arm_transaction_can_never_close_cn9},
    {"R17", r17_gptimer_arm_failure_prevents_relay_energization},
    {"R18", r18_watchdog_fault_opens_cn9_and_requests_safe_restart},
    {"R19", r19_reset_during_close_requires_local_on_off_recovery},
    {"R20", r20_three_unsafe_resets_are_latched_as_a_boot_loop},
    {"R21", r21_automatic_control_requires_fresh_weight},
    {"R22", r22_implausible_weight_slew_degrades_to_manual},
    {"R23", r23_maintenance_is_canceled_fail_open_by_physical_paddle},
    {"R24", r24_web_control_lease_owner_is_enforced},
    {"R25", r25_critical_scale_mailbox_never_blocks_and_keeps_latest},
    {"R26", r26_remote_timer_stop_retries_after_full_queue},
    {"R27", r27_platform_clock_failure_prevents_cn9_close},
    {"R28", r28_terminal_control_result_is_retained_until_forwarded},
    {"W01", w01_default_runtime_configuration_is_valid},
    {"W02", w02_each_runtime_field_is_validated},
    {"W03", w03_runtime_timing_relations_are_transactional},
    {"W04", w04_wifi_credentials_have_strict_bounds},
    {"W05", w05_config_is_blocked_while_qualifying},
    {"W06", w06_config_is_blocked_while_brewing},
    {"W07", w07_config_is_blocked_while_rinsing},
    {"W08", w08_config_is_blocked_during_manual_cycle},
    {"W09", w09_valid_config_applies_only_from_ready},
    {"W10", w10_cycle_configuration_snapshot_is_immutable},
    {"W11", w11_operational_timer_opens_without_control_loop},
    {"W12", w12_hard_limit_cannot_be_configured_above_sixty_seconds},
    {"W13", w13_virtual_paddle_uses_normal_state_machine},
    {"W14", w14_physical_motion_overrides_web_control},
    {"W15", w15_web_rinse_skips_scale_timer},
    {"W16", w16_web_stop_during_rinse_preserves_rearm},
    {"W17", w17_web_heartbeat_timeout_is_a_safe_stop},
    {"W18", w18_web_stop_can_end_a_physical_brew_only_by_opening},
    {"W19", w19_web_start_is_rejected_outside_ready},
    {"W20", w20_restart_is_rejected_while_active},
    {"W21", w21_network_change_is_rejected_while_active},
    {"W22", w22_timer_only_disables_predictive_stop},
    {"W23", w23_combined_tare_command_uses_cycle_snapshot},
    {"W24", w24_debug_ring_is_bounded_and_ordered},
    {"W25", w25_weight_samples_do_not_fill_debug_log},
    {"W26", w26_status_is_a_copied_snapshot},
    {"W27", w27_stale_weight_is_not_presented_as_current},
    {"W28", w28_web_command_queue_is_bounded},
    {"W29", w29_operational_wall_of_60001_is_rejected},
    {"W30", w30_last_cycle_weight_must_belong_to_that_cycle},
    {"W31", w31_unsupported_scale_never_uses_tare_as_a_beep},
    {"W32", w32_full_scale_queue_cannot_block_brew_confirmation},
    {"W33", w33_brew_confirmation_beep_can_be_disabled},
    {"W34", w34_calibration_reset_restores_default_and_cancels_analysis},
    {"W35", w35_status_reports_the_live_physical_paddle_gpio},
    {"W36", w36_paddle_return_reminder_beeps_every_fifteen_seconds_only_while_open},
    {"W37", w37_factory_reset_is_rejected_while_control_is_active},
    {"W38", w38_scale_indicator_states_are_unambiguous},
    {"W39", w39_automatic_stopper_palette_encodes_workflow},
    {"W40", w40_manual_and_timer_only_palette_is_salmon},
    {"W41", w41_safety_and_action_states_override_operating_palette},
    {"W42", w42_indicator_blink_periods_are_deterministic},
    {"W43", w43_manual_palette_tracks_timer_only_and_scale_loss},
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
