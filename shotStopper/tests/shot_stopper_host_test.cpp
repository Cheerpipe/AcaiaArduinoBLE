#define SHOT_STOPPER_HOST_TEST
#define ARDUINO_ESP32_DEV
#define SHOT_STOPPER_ENABLE_REMOTE_CN9 1
#ifndef SHOT_STOPPER_ENABLE_BUZZER
#define SHOT_STOPPER_ENABLE_BUZZER 1
#endif
#define SHOT_STOPPER_ENABLE_ALED 1

#include <cstdint>
#include <cstdlib>
#include <cstring>
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

  Serial.reset();
  resetSerialCliState();

  hostMillis = 0;
  bootStartedAtMs = 0;
  hostPinLevel.fill(HIGH);
  hostPinMode.fill(0);
  hostTrackedRelayPin = RELAY_GPIO;
  hostTrackedRelayOpenLevel = RELAY_OPEN_LEVEL;
  hostTrackedRelayClosedLevel = RELAY_CLOSED_LEVEL;
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
  hostLedcAttachCalls = 0;
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
  pendingFinalize = PendingShotFinalize{};
  pendingScaleTimerStop = PendingScaleTimerStop{};
  runtimeConfig = RuntimeConfig{};
  // Host scenarios cover scale-path alerts unless a test sets the channel.
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  runtimeConfig.autoRetare = false;
  runtimeConfig.bbwProtectionMs = 3000;
  runtimeConfig.fastExtractionGuardEnabled = false;
  runtimeConfig.avoidBbwShotWithoutScale = false;
  // Host scenarios assert immediate scale timer stop; delayed stop is covered by ST02.
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  lastCycle = LastCycleSummary{};
  persistedLastShot = PersistedLastShot{};
  lastShotStore.clear();
  noScaleShotGuardArmed = true;
  noScaleShotGuardActivityAtMs = 0;
  noScaleShotGuardScaleWasAvailable = false;
  noScaleShotGuardHold = false;
  noScaleShotGuardHoldAtMs = 0;
  debugLog.clear();
  lastReportedLogOverwritten = 0;
  serialLogLevel = LogLevel::NONE;
  ringRetainLogLevel = LogLevel::INFO;
  publishedControlStatus = ControlStatusSnapshot{};
  maintenanceLease = MaintenanceLease{};
  maintenanceCancellationCommand = WebCommand{};
  maintenanceCancellationPending = false;
  controlResultCommand = WebCommand{};
  controlResultPending = false;
  hostForwardAcceptedNetworkCommandSucceeds = true;
  hostForwardAcceptedNetworkCommandCalls = 0;
  hostLastForwardedNetworkCommand = WebCommand{};
  hostNtpSyncRequestCount = 0;
  hostRuntimePersistSucceeds = true;
  hostRuntimePersistAttempts = 0;
  hostLastFlushedRuntime = RuntimeConfig{};
  hostLastFlushedPresets = ShotPresetBank{};
  hostLastFlushIncludedLive = false;
  g_wallClock.reset();
  runtimePersistPending = false;
  runtimePersistFailed = false;
  runtimePersistCandidate = RuntimeConfig{};
  runtimePersistRequestId = 0;
  runtimePersistRetryAtMs = 0;
  runtimePersistReasonBits = 0;
  nextInternalRequestId = 0x80000000UL;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = 0;
  currentWeightSequence = 0;
  currentWeightConnectionGeneration = 0;
  observedWeight = 0.0f;
  observedWeightReceivedAtMs = 0;
  observedWeightSequence = 0;
  observedWeightConnectionGeneration = 0;
  weightStreamState = WeightStreamState::NO_SAMPLE;
  nextCycleId = 1;
  scaleWorkerTaskHandle = nullptr;
  scale = AcaiaArduinoBLE(DEBUG);
  scale.connected = scaleConnected;
  scalePreferredMac[0] = '\0';
  scalePreferredName[0] = '\0';
  scalePreferredMacDirty = false;
  scaleDiscoveryPausedUntilMs = 0;
  scalePreferredDirectedResetGeneration = 0;
  scaleLinkState = ScaleLinkState::DISCONNECTED;
  scaleDisconnectSequence = 0;
  scaleConnectionGeneration = 0;
  scalePacketSequence = 0;
  scalePacketGaps = 0;
  scaleRejectedPackets = 0;
  scaleReconnects = 0;
  scaleLastDisconnectReason = 0;
  scaleTimerValid = false;
  scaleTimerMs = 0;
  scaleTimerAgeMs = 0;
  scaleWorkerProgressAtMs = hostMillis;
  scaleEventsDropped = 0;
  scaleWorkerStackMinWords = 0;
  freeHeapBytes = 0;
  minimumFreeHeapBytes = 0;
  largestFreeHeapBlockBytes = 0;
  loopStackMinWords = 0;
  loopMaxGapMs = 0;
  healthIntervalMaxGapMs = 0;
  healthHeapAlertLatched = false;
  healthStackAlertLatched = false;
  healthLoopGapAlertLatched = false;
  scaleCriticalEvent = ScaleEvent{};
  scaleCriticalEventPending = false;
  scaleTimerStartEvent = ScaleEvent{};
  scaleTimerStartEventPending = false;
  scaleWeightEvent = ScaleEvent{};
  scaleWeightEventPending = false;
  scaleBeepPending = false;
  scaleBeepCycleId = 0;
  scaleDebugPending = false;
  scaleDebugAction = BookooDebugAction::START;
  scaleDebugBeepLevel = 0;
  scalePaddleReturnReminderBeepPending = false;
  paddleReturnReminderActive = false;
  paddleReturnReminderLastAtMs = 0;
  paddleReturnReminderStartedAtMs = 0;
  scaleCompletionBeepPending = false;
  scaleCompletionBeepScheduled = false;
  scaleCompletionBeepDueAtMs = 0;
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
  localBuzzer.begin(BUZZER_GPIO);
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
  seedDefaultShotPresetBank(presetBank);
  ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                       runtimeConfig.autoRetare);
  {
    ShotPreset &preset = mutableActiveShotPreset(presetBank);
    copyUserRecipeFromConfig(runtimeConfig, preset);
    preset.weightOffsetG = runtimeConfig.weightOffsetG;
    memcpy(preset.autoToManualGuardSamplesDs,
           runtimeConfig.autoToManualGuardSamplesDs,
           sizeof(preset.autoToManualGuardSamplesDs));
  }
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
}

void verifySafetyInvariants() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const bool stateMayCloseRelay =
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
  hostServiceEspTimer(localBuzzer.phaseTimer);
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

void publishWeight(float weight, uint32_t receivedAtMs = UINT32_MAX,
                   uint32_t generation = 0, uint32_t sequence = 0) {
  ScaleEvent event;
  event.type = ScaleEventType::WEIGHT;
  event.receivedAtMs = receivedAtMs == UINT32_MAX ? hostMillis : receivedAtMs;
  event.connectionGeneration = generation;
  event.packetSequence = sequence;
  event.weightG = weight;
  CHECK(publishScaleEvent(event, false));
  processScaleWorkerEvents();
}

void establishPostTareBaseline(float weight = 0.0f) {
  publishWeight(weight, hostMillis + 1);
}

void simulateFirstDrops(float baselineG = 0.0f, uint32_t baseSequence = 20) {
  publishWeight(baselineG + 0.35f, hostMillis + 50, 1, baseSequence);
  publishWeight(baselineG + 0.40f, hostMillis + 150, 1, baseSequence + 1);
  publishWeight(baselineG + 0.45f, hostMillis + 250, 1, baseSequence + 2);
}

void publishStableCupWeight(float weight, uint32_t baseSequence = 10) {
  const uint8_t sampleCount = runtimeConfig.retareStabilitySamples;
  const uint32_t minDurationMs = runtimeConfig.retareStabilityMinDurationMs;
  const uint32_t stepMs =
      sampleCount > 1U && minDurationMs > 0U
          ? minDurationMs / static_cast<uint32_t>(sampleCount - 1U)
          : 100U;
  const uint32_t intervalMs = stepMs > 0U ? stepMs : 100U;
  for (uint8_t index = 0; index < sampleCount; ++index) {
    publishWeight(weight + (index == 1 ? 0.1f : 0.0f),
                  hostMillis + index * intervalMs, 1, baseSequence + index);
  }
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
  CHECK_VALUE(stopperState == StopperState::BREW ||
                  stopperState == StopperState::MANUAL_NO_SCALE,
              rawOnAtMs);
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

void waitForRetareEnded(uint32_t maxWaitMs = 15000) {
  uint32_t waited = 0;
  while (!session.retareEnded && waited < maxWaitMs) {
    runLoopAfter(25);
    waited += 25;
  }
  CHECK(session.retareEnded);
}

void waitForBbwProtectionEnded(uint32_t maxWaitMs = 15000) {
  uint32_t waited = 0;
  while (!session.bbwProtectionEnded && waited < maxWaitMs) {
    runLoopAfter(25);
    waited += 25;
  }
  CHECK(session.bbwProtectionEnded);
}

void endBbwProtectionForTests() {
  if (session.bbwProtectionEnded) {
    return;
  }
  const uint32_t target = session.config.bbwProtectionMs;
  const uint32_t current = elapsedMs(session.startedAtMs);
  if (current < target) {
    runLoopAfter(target - current);
  } else {
    loop();
  }
  waitForBbwProtectionEnded();
}

void reachBrewState() {
  if (scale.connected && session.automaticEnabled && !runtimeConfig.timerOnly &&
      session.awaitingPostTareBaseline) {
    establishPostTareBaseline();
  }
  uint32_t waited = 0;
  while (stopperState != StopperState::BREW && waited < 5000) {
    runLoopAfter(25);
    waited += 25;
  }
  CHECK(stopperState == StopperState::BREW);
  if (!runtimeConfig.timerOnly) {
    CHECK(shot.automaticBrew);
  }
}

void advanceToBrew() {
  if (scale.connected && session.automaticEnabled && !runtimeConfig.timerOnly &&
      session.awaitingPostTareBaseline) {
    establishPostTareBaseline();
  }
  reachBrewState();
  // Past the rinse demotion window so paddle OFF ends the shot (not rinse).
  const uint32_t elapsed = elapsedMs(session.startedAtMs);
  if (elapsed <= runtimeConfig.rinseGestureMs) {
    runLoopAfter(runtimeConfig.rinseGestureMs - elapsed + 1);
  }
}

void reachManualNoScaleState() {
  uint32_t waited = 0;
  while (stopperState != StopperState::MANUAL_NO_SCALE && waited < 5000) {
    runLoopAfter(25);
    waited += 25;
  }
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
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

void t03_sustained_on_enters_brew_once() {
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
  establishPostTareBaseline();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(shot.automaticBrew);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  CHECK(!scaleBeepPending);
  simulateFirstDrops();
  CHECK(session.firstDropMs != 0);
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
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);

  const uint32_t remaining =
      runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs);
  runLoopAfter(remaining - 1);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
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
  CHECK(session.endReason == EndReason::PADDLE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t06_paddle_off_during_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
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
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
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

void t11_ble_loss_suspends_brew_without_late_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  setScaleConnected(false);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
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
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  reachSessionElapsed(HARD_MAX_CN9_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
  CHECK(!getRelaySafetySnapshot().closed);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
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
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
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
  advanceToBrew();
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
  advanceToBrew();
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
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
  runLoopAfter(runtimeConfig.rinseDurationMs);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  resetHarness(false, true);
  reachReadyFromBoot();
  rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 100);
  CHECK(session.endReason == EndReason::PADDLE);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t19_manual_cycle_without_scale_has_no_timer_commands() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
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
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(!session.automaticEnabled);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
}

void t23_prediction_triggers_after_bbw_protection_ends() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  shot.expectedEndS = 1.0f;
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_PREDICTION);
}

void t24_paddle_off_during_brew_is_immediate() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t25_ble_loss_during_rinse_window_preserves_classification() {
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
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
}

void t26_reconnected_suspended_cycle_sends_one_stop_on_release() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  setScaleConnected(false);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  setScaleConnected(true);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
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

void r01_transient_disconnect_suspends_weight_control() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);

  // Reconnect before the control loop gets a chance to observe DISCONNECTED.
  setScaleConnected(false);
  setScaleConnected(true);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.scaleWasLost);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);

  shot.expectedEndS = 0.0f;
  endBbwProtectionForTests();
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
}

void r02_stalled_scale_worker_suspends_until_validated() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);

  hostAutoScaleWorkerProgress = false;
  runLoopAfter(SCALE_WORKER_STALE_MS + 1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(getRelaySafetySnapshot().closed);

  hostAutoScaleWorkerProgress = true;
  markScaleWorkerProgress();
  loop();
  CHECK(stopperState == StopperState::BREW);
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

  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = true;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 0;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = originalOffset;
  currentWeight = std::numeric_limits<float>::quiet_NaN();
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(DRIP_DELAY_MS);
  CHECK(!pendingFinalize.pending);
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
  advanceToBrew();
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
  advanceToBrew();
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

  // Intercept already above target with a positive slope predicts a time in
  // the past of the sample window; fall back to the operational wall.
  resetShotTrajectory(session.startedAtMs);
  for (size_t i = 0; i < TREND_POINT_COUNT; ++i) {
    recordWeightSample(40.0f + static_cast<float>(i) * 0.1f,
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  CHECK(shot.expectedEndS == session.config.operationalWallMs / 1000.0f);
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
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);

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
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
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
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
}

void r09_stop_is_not_retried_after_disconnect_before_execution() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  advanceToBrew();
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
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = true;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 0;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = originalOffset;
  currentWeight = DEFAULT_GOAL_WEIGHT_G + 1.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(DRIP_DELAY_MS);
  finishHostMaintenance();
  CHECK(fabsf(runtimeConfig.weightOffsetG - 2.5f) < 0.001f);

  const float validOffset = runtimeConfig.weightOffsetG;
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = true;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = validOffset;
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
  advanceToBrew();

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
  update.requestId = 14;
  update.config = runtimeConfig;
  update.config.goalWeightG = MIN_GOAL_WEIGHT_G - 1;
  CHECK(validateRuntimeConfig(update.config) ==
        ConfigValidationError::GOAL_WEIGHT);
  CHECK(enqueueWebCommand(update));
  loop();
  CHECK(runtimeConfig.goalWeightG == original.goalWeightG);
  CHECK(runtimeConfig.revision == original.revision);
  CHECK(hostLastForwardedNetworkCommand.requestId == 14);
  CHECK(hostLastForwardedNetworkCommand.resultState ==
        CommandResultState::FAILED);

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
  CHECK(config.rinseGestureMs == 1000);
  CHECK(config.minBrewTimeMs == 28000);
  CHECK(config.canTareStartTimer);
  CHECK(config.scaleTimerStopExtraDelayMs ==
        DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS);
  CHECK(config.firstDropBeep);
  CHECK(config.paddleReturnReminderBeep);
  CHECK(config.buzzerScaleLostBeep);
  CHECK(config.buzzerAutoToManualGuardEndBeep);
  CHECK(config.buzzerManualNoScaleBeep);
  CHECK(config.buzzerExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(DEFAULT_EXTENDED_PULSE_RATE == ExtendedPulseRate::FAST);
  CHECK(buzzerPatternForExtendedPulseRate(config.buzzerExtendedPulseRate) ==
        BuzzerPattern::PULSE_4HZ);
  CHECK(config.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(DEFAULT_ALERT_OUTPUT_CHANNEL ==
        (BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY
                                : AlertOutputChannel::SCALE_ONLY));
  CHECK(config.bookooMuteOnBuzzerOnly);
  CHECK(config.bookooConnectBeepLevel == DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(config.fastExtractionGuardEnabled);
  CHECK(config.avoidBbwShotWithoutScale);
  CHECK(config.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
  CHECK(!config.serialDebugOutput);
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
  config.bbwProtectionMs = MIN_BBW_PROTECTION_MS - 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::BBW_PROTECTION_TIMEOUT);
  config = RuntimeConfig{};
  config.bbwProtectionMs = MAX_BBW_PROTECTION_MS + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::BBW_PROTECTION_TIMEOUT);
  config = RuntimeConfig{};
  config.fastExtractionGuardEnabled = true;
  config.maxRecoveryWeightG = 36.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION);
}

void w03_runtime_timing_relations_are_transactional() {
  RuntimeConfig config;
  config.operationalWallMs = 10000;
  config.bbwProtectionMs = 11000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::TIMING_RELATION);
  config = RuntimeConfig{};
  config.autoRetare = false;
  config.operationalWallMs = 5000;
  config.bbwProtectionMs = 6000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::TIMING_RELATION);
  config = RuntimeConfig{};
  config.canTareStartTimer = true;
  config.autoTare = false;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE);
  config = RuntimeConfig{};
  config.scaleTimerStopExtraDelayMs = MAX_SCALE_TIMER_STOP_EXTRA_DELAY_MS + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SCALE_TIMER_STOP_EXTRA_DELAY);
  config = RuntimeConfig{};
  config.bookooConnectBeepLevel = BOOKOO_BEEP_LEVEL_MAX + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::BOOKOO_CONNECT_BEEP_LEVEL);
  config = RuntimeConfig{};
  config.buzzerExtendedPulseRate = 9;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::EXTENDED_PULSE_RATE);
  config = RuntimeConfig{};
  config.lastShotCooldownMs = MIN_LAST_SHOT_COOLDOWN_MS - 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::LAST_SHOT_COOLDOWN);
  config = RuntimeConfig{};
  config.lastShotCooldownMs = MAX_LAST_SHOT_COOLDOWN_MS + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::LAST_SHOT_COOLDOWN);
}

void w04_wifi_credentials_have_strict_bounds() {
  CHECK(validWifiSsid("Micra"));
  CHECK(!validWifiSsid(""));
  CHECK(validWifiPassword("12345678", false));
  CHECK(!validWifiPassword("1234", false));
  CHECK(validWifiPassword("", true));
  CHECK(validAccessPointPassword("Micra1234"));
  CHECK(shouldReuseSavedWifiCredentials("CafeLAN", "", false, true, "CafeLAN",
                                        false));
  CHECK(shouldReuseSavedWifiCredentials("CafeLAN", "", true, true, "CafeLAN",
                                        true));
  CHECK(!shouldReuseSavedWifiCredentials("CafeLAN", "CafePass1", false, true,
                                         "CafeLAN", false));
  CHECK(!shouldReuseSavedWifiCredentials("OtherNet", "", false, true, "CafeLAN",
                                         false));
  CHECK(!shouldReuseSavedWifiCredentials("CafeLAN", "", false, false, "CafeLAN",
                                         false));
  CHECK(!shouldReuseSavedWifiCredentials("CafeLAN", "", true, true, "CafeLAN",
                                         false));
  CHECK(!shouldReuseSavedWifiCredentials("", "", false, true, "CafeLAN", false));
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

void w05_config_is_blocked_while_brewing_early() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  attemptActiveConfigUpdate();
}

void w06_config_is_blocked_while_brewing() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
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
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  attemptActiveConfigUpdate();
}

void w09_valid_config_applies_only_from_ready() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.weightOffsetG = 2.25f;
  mutableActiveShotPreset(presetBank).weightOffsetG = 2.25f;
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.goalWeightG = 42;
  update.config.weightOffsetG = 4.5f;  // Not a Web-editable field.
  const uint32_t oldRevision = runtimeConfig.revision;
  processWebCommand(update);
  CHECK(runtimeConfig.goalWeightG == 42);
  CHECK(fabsf(runtimeConfig.weightOffsetG - 2.25f) < 0.001f);
  CHECK(runtimeConfig.revision == oldRevision + 1);
  CHECK(!maintenanceLease.active);
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
  runtimeConfig.firstDropBeep = !frozen.firstDropBeep;
  CHECK(session.config.firstDropBeep == frozen.firstDropBeep);
}

void w11_operational_timer_opens_without_control_loop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.operationalWallMs = 20000;
  runtimeConfig.bbwProtectionMs = 7000;
  runtimeConfig.autoToManualGuardManualLimitMs = 15000;
  runtimeConfig.autoToManualGuardBaselineMs = 15000;
  CHECK(validateRuntimeConfig(runtimeConfig) == ConfigValidationError::NONE);
  startCycle();
  hostMillis = cn9ClosedAtMs + runtimeConfig.operationalWallMs;
  hostServiceEspTimer(operationalLimitTimer);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().operationalTripped);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
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

void w45_bbw_protection_retare_relation_is_validated() {
  RuntimeConfig config;
  config.retareWindowMs = 5000;
  config.bbwProtectionMs =
      config.retareWindowMs + MIN_BBW_PROTECTION_AFTER_RETARE_MS - 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::BBW_PROTECTION_RETARE_RELATION);
}

void w13_virtual_paddle_uses_normal_state_machine() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::PADDLE_ON);
  processWebCommand(on);
  CHECK(session.active);
  CHECK(session.source == ControlSource::WEB);
  CHECK(virtualPaddleOn);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
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

void w15_web_rinse_starts_scale_timer() {
  resetHarness(false, true);
  reachReadyFromBoot();
  WebCommand rinse = webControlCommand(WebCommandType::RINSE);
  processWebCommand(rinse);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(session.source == ControlSource::WEB);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  runLoopAfter(runtimeConfig.rinseDurationMs);
  CHECK(stopperState == StopperState::READY);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  processWebCommand(rinse);
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::PHYSICAL_OVERRIDE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 2);
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
  advanceToBrew();
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
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
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
  advanceToBrew();
  CHECK(!scaleBeepPending);
  shot.expectedEndS = 0.0f;
  loop();
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
    ring.add(static_cast<uint32_t>(index), 0, LogLevel::INFO, DebugCategory::WEB,
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

void w46_status_reports_uptime_since_boot() {
  resetHarness(false, false);
  bootStartedAtMs = 1000;
  hostMillis = 6500;
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.uptimeMs == 5500);
}

void w47_status_reports_live_scale_weight_and_timer() {
  resetHarness(false, true);
  reachReadyFromBoot();
  publishWeight(18.5f);
  scale.timerValid = true;
  scale.timerMs = 12340;
  scale.timerAgeMs = 40;
  updateWorkerLinkState();
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.observedWeightValid);
  CHECK(fabsf(status.observedWeightG - 18.5f) < 0.01f);
  CHECK(status.currentTimerValid);
  CHECK(status.currentTimerMs == 12340);
  CHECK(status.currentTimerAgeMs == 40);

  setScaleConnected(false);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(!status.currentTimerValid);
  CHECK(status.currentTimerMs == 0);
}

void r47_reset_reason_name_maps_known_codes() {
  CHECK(strcmp(safetyResetReasonName(1), "Power-on") == 0);
  CHECK(strcmp(safetyResetReasonName(3), "Software") == 0);
  CHECK(strcmp(safetyResetReasonName(6), "Task WDT") == 0);
  CHECK(strcmp(safetyResetReasonName(9), "Brownout") == 0);
  CHECK(strcmp(safetyResetReasonName(999), "Unknown") == 0);
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
  event.packetSequence = 2;
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
  establishPostTareBaseline();
  scale.independentBeepSupported = false;
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  simulateFirstDrops();
  CHECK(scaleBeepPending);
  CHECK(executePendingScaleBrewBeep());
  CHECK(scale.beepCalls == 0);
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.tareCalls == 0);
  CHECK(scale.connected);
  CHECK(session.automaticEnabled);
}

void w32_full_scale_queue_cannot_block_brew_start() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  ScaleCommand filler;
  filler.type = ScaleCommandType::STOP_TIMER;
  for (size_t index = 0; index < SCALE_COMMAND_QUEUE_LENGTH; ++index) {
    CHECK(xQueueSend(scaleCommandQueue, &filler, 0) == pdTRUE);
  }
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(shot.automaticBrew);
  CHECK(getRelaySafetySnapshot().closed);
  simulateFirstDrops();
  CHECK(scaleBeepPending);
  CHECK(scale.beepCalls == 0);
}

void w33_first_drop_beep_can_be_disabled() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.firstDropBeep = false;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  simulateFirstDrops();
  CHECK(!scaleBeepPending);
  CHECK(scale.beepCalls == 0);
}

void w34_calibration_reset_restores_baseline_and_cancels_analysis() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.weightOffsetBaselineG = 2.0f;
  runtimeConfig.weightOffsetG = 3.2f;
  {
    ShotPreset &preset = mutableActiveShotPreset(presetBank);
    preset.weightOffsetBaselineG = 2.0f;
    preset.weightOffsetG = 3.2f;
  }
  const uint32_t previousRevision = runtimeConfig.revision;
  pendingFinalize.pending = true;
  WebCommand reset;
  reset.type = WebCommandType::RESET_WEIGHT_OFFSET;
  processWebCommand(reset);
  CHECK(fabsf(runtimeConfig.weightOffsetG - 2.0f) < 0.001f);
  CHECK(fabsf(runtimeConfig.weightOffsetBaselineG - 2.0f) < 0.001f);
  CHECK(runtimeConfig.revision == previousRevision + 1);
  CHECK(!pendingFinalize.pending);
  CHECK(!maintenanceLease.active);
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

void w36_paddle_return_reminder_beeps_at_configured_interval_only_while_open() {
  resetHarness(true, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runLoopAfter(0);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!scalePaddleReturnReminderBeepPending);
  const uint32_t interval = runtimeConfig.paddleReturnReminderIntervalMs;
  const uint32_t before = localBuzzer.acceptedRequests;
  runLoopAfter(interval - 1);
  CHECK(localBuzzer.acceptedRequests == before);
  runLoopAfter(1);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.busy());
  CHECK(scale.beepCalls == 0);
  CHECK(!scalePaddleReturnReminderBeepPending);
  CHECK(!executePendingScalePaddleReturnReminderBeep());

  setRawPaddle(false);
  // Drain any in-flight pattern without counting a new request.
  for (uint32_t step = 0; step < 20 && localBuzzer.busy(); ++step) {
    runLoopAfter(50);
  }
  const uint32_t afterOff = localBuzzer.acceptedRequests;
  runLoopAfter(interval);
  CHECK(localBuzzer.acceptedRequests == afterOff);
  CHECK(!scalePaddleReturnReminderBeepPending);
}

void w44_paddle_return_reminder_stops_after_fifteen_minutes() {
  resetHarness(true, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runLoopAfter(0);
  runLoopAfter(runtimeConfig.paddleReturnReminderIntervalMs);
  CHECK(localBuzzer.acceptedRequests >= 1);
  for (uint32_t step = 0; step < 20 && localBuzzer.busy(); ++step) {
    runLoopAfter(50);
  }
  runLoopAfter(runtimeConfig.paddleReturnReminderMaxDurationMs -
               runtimeConfig.paddleReturnReminderIntervalMs);
  const uint32_t afterLimit = localBuzzer.acceptedRequests;
  runLoopAfter(runtimeConfig.paddleReturnReminderIntervalMs);
  CHECK(localBuzzer.acceptedRequests == afterLimit);
  CHECK(!scalePaddleReturnReminderBeepPending);
}

void w50_local_buzzer_plays_triple_pattern_non_blocking() {
  resetHarness(false, false);
  CHECK(localBuzzer.ready);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.busy());
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  // Control loop must keep running while the pattern plays.
  runLoopAfter(10);
  CHECK(getRelaySafetySnapshot().closed == false);
  for (uint32_t step = 0; step < 40 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  CHECK(!localBuzzer.busy());
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(!localBuzzer.request(BuzzerPattern::NONE));
}

void w50b_buzzer_phase_timer_holds_triple_rhythm_without_loop() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += BUZZER_BEEP_ON_MS;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(localBuzzer.busy());
  hostMillis += BUZZER_BEEP_GAP_MS;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += BUZZER_BEEP_ON_MS;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_BEEP_GAP_MS;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += BUZZER_BEEP_ON_MS;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(!localBuzzer.busy());
}

void w50c_buzzer_phase_timer_advances_despite_loop_stall() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += 150;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(localBuzzer.busy());
}

void w51_local_buzzer_triple_on_scale_lost_during_bbw() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  CHECK(session.automaticEnabled ||
        session.weightControlState == WeightControlState::ACTIVE ||
        session.weightControlState == WeightControlState::VALIDATING);
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  loop();
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w52_local_buzzer_triple_on_manual_bbw_without_scale() {
  resetHarness(false, false);
  CHECK(!runtimeConfig.timerOnly);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w53_local_buzzer_silent_when_bbw_off_without_scale() {
  resetHarness(false, false);
  runtimeConfig.timerOnly = true;
  ShotPreset &preset = mutableActiveShotPreset(presetBank);
  preset.brewByWeight = false;
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
  CHECK(runtimeConfig.timerOnly);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(localBuzzer.acceptedRequests == before);
}

bool debugEventExists(DebugCode code, int32_t argument1 = INT32_MIN,
                      int32_t argument2 = INT32_MIN);

void enableNoScaleShotGuardForTest() {
  runtimeConfig.avoidBbwShotWithoutScale = true;
  noScaleShotGuardArmed = true;
  noScaleShotGuardActivityAtMs = 0;
  noScaleShotGuardHold = false;
  noScaleShotGuardHoldAtMs = 0;
}

void attemptBlockedNoScaleStart() {
  CHECK(stopperState == StopperState::READY);
  CHECK(noScaleShotGuardArmed);
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(noScaleShotGuardArmed);
  CHECK(noScaleShotGuardHold);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!noScaleShotGuardArmed);
  CHECK(!noScaleShotGuardHold);
  CHECK(debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED));
  CHECK(debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED));
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
  setRawPaddle(false);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
}

void ns01_armed_blocks_first_bbw_no_scale_shot() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
}

void ns02_idle_allows_second_bbw_no_scale_shot() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!noScaleShotGuardArmed);
}

void ns03_bbw_off_does_not_block() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.timerOnly = true;
  ShotPreset &preset = mutableActiveShotPreset(presetBank);
  preset.brewByWeight = false;
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
  reachReadyFromBoot();
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(noScaleShotGuardArmed);
}

void ns04_scale_available_rearms() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  CHECK(!noScaleShotGuardArmed);
  setScaleConnected(true);
  markScaleWorkerProgress();
  loop();
  CHECK(noScaleShotGuardArmed);
  CHECK(debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_ARMED));
}

void ns05_cooldown_rearms_from_idle() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.lastShotCooldownMs = 1000;
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  CHECK(!noScaleShotGuardArmed);
  runLoopAfter(1000);
  CHECK(noScaleShotGuardArmed);
}

void ns06_finished_shot_extends_cooldown() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.lastShotCooldownMs = 3000;
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 50);
  CHECK(stopperState == StopperState::READY ||
        stopperState == StopperState::REQUIRES_OFF);
  CHECK(!noScaleShotGuardArmed);
  runLoopAfter(2000);
  CHECK(!noScaleShotGuardArmed);
  runLoopAfter(1500);
  CHECK(noScaleShotGuardArmed);
}

void ns07_web_rinse_does_not_consume_guard() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(stopperState == StopperState::RINSE);
  CHECK(noScaleShotGuardArmed);
  CHECK(getRelaySafetySnapshot().closed);
}

void ns08_blocked_beep_respects_alert_checkbox() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.buzzerManualNoScaleBeep = false;
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(localBuzzer.acceptedRequests == before);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(!noScaleShotGuardArmed);
  CHECK(localBuzzer.acceptedRequests == before);
}

void ns09_armed_rinse_gesture_runs_and_stays_armed() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  CHECK(noScaleShotGuardArmed);
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  const uint32_t rawOnAt = hostMillis;
  setRawPaddle(true);
  runLoopAfter(PADDLE_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(noScaleShotGuardArmed);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(noScaleShotGuardArmed);
  CHECK(debugEventExists(DebugCode::RINSE_CLASSIFIED));
  CHECK(!debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED));
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
}

void ns10_idle_rinse_gesture_does_not_rearm() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  CHECK(!noScaleShotGuardArmed);
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  const uint32_t rawOnAt = startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(!noScaleShotGuardArmed);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
}

void w54_local_buzzer_triple_on_auto_to_manual_guard_end() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  const uint32_t before = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::AUTO_TO_MANUAL_GUARD, StopperState::REQUIRES_OFF);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(session.endReason == EndReason::AUTO_TO_MANUAL_GUARD);
}

void w55_local_buzzer_queues_second_triple_while_busy() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.active == BuzzerPattern::TRIPLE);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.pending == BuzzerPattern::TRIPLE);
  CHECK(localBuzzer.acceptedRequests == 2);
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  CHECK(!localBuzzer.busy());
  CHECK(localBuzzer.active == BuzzerPattern::NONE);
  CHECK(localBuzzer.pending == BuzzerPattern::NONE);
}

void w56_atm_beep_queued_when_scale_lost_after_deadline() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = true;
  runtimeConfig.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
  runtimeConfig.autoToManualGuardManualLimitMs = 15000;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
  // Past the ATM deadline while scale is still up: guard is armed but not
  // enforced, so the cycle continues until weight control suspends.
  reachSessionElapsed(16000);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  loop();
  CHECK(session.endReason == EndReason::AUTO_TO_MANUAL_GUARD);
  // Scale-lost TRIPLE + ATM TRIPLE (completion SINGLE is delayed ~200 ms).
  CHECK(localBuzzer.acceptedRequests == before + 2);
  // Drain ATM/scale-lost triples so the delayed completion SINGLE can start.
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  runLoopAfter(SCALE_COMPLETION_BEEP_DELAY_MS + 60);
  CHECK(localBuzzer.acceptedRequests >= before + 3);
}

void w63_scale_priority_paddle_uses_scale_when_connected() {
  resetHarness(true, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  runLoopAfter(0);
  const uint32_t interval = runtimeConfig.paddleReturnReminderIntervalMs;
  const uint32_t before = localBuzzer.acceptedRequests;
  runLoopAfter(interval);
  CHECK(localBuzzer.acceptedRequests == before);
  CHECK(scalePaddleReturnReminderBeepPending);
  CHECK(executePendingScalePaddleReturnReminderBeep());
}

void w64_buzzer_only_first_drop_uses_local_buzzer() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runtimeConfig.firstDropBeep = true;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  const uint32_t before = localBuzzer.acceptedRequests;
  const uint32_t beforeScaleBeeps = scale.beepCalls;
  requestFirstDropBeep();
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(scale.beepCalls == beforeScaleBeeps);
  CHECK(!scaleBeepPending);
}

void w65_scale_only_mutes_scale_lost_triple() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  loop();
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(localBuzzer.acceptedRequests == before);
}

void w57_paddle_return_reminder_falls_back_to_scale_when_piezo_not_ready() {
  resetHarness(true, true);
  localBuzzer.ready = false;
  runLoopAfter(0);
  CHECK(!getRelaySafetySnapshot().closed);
  const uint32_t interval = runtimeConfig.paddleReturnReminderIntervalMs;
  const uint32_t beforeBuzzer = localBuzzer.acceptedRequests;
  runLoopAfter(interval);
  CHECK(localBuzzer.acceptedRequests == beforeBuzzer);
  CHECK(scalePaddleReturnReminderBeepPending);
  CHECK(executePendingScalePaddleReturnReminderBeep());
}

void w58_paddle_return_reminder_does_not_advance_interval_when_muted() {
  resetHarness(true, false);
  runtimeConfig.paddleReturnReminderIntervalMs = 200;
  runtimeConfig.paddleReturnReminderMaxDurationMs = 60000;
  runLoopAfter(0);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.pending == BuzzerPattern::TRIPLE);
  runLoopAfter(200);
  CHECK(localBuzzer.acceptedRequests == 2);
  CHECK(!scalePaddleReturnReminderBeepPending);
  // Interval must remain due so a later scale fallback can sound immediately.
  setScaleConnected(true);
  runLoopAfter(0);
  CHECK(scalePaddleReturnReminderBeepPending);
}

void drainLocalBuzzer() {
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  CHECK(!localBuzzer.busy());
}

void w59_local_buzzer_plays_short_long_and_double_patterns() {
  resetHarness(false, false);
  CHECK(localBuzzer.ready);
  CHECK(localBuzzer.request(BuzzerPattern::SINGLE));
  CHECK(localBuzzer.active == BuzzerPattern::SINGLE);
  CHECK(localBuzzer.beepCount == 1);
  CHECK(localBuzzer.onMs == BUZZER_SINGLE_ON_MS);
  drainLocalBuzzer();
  CHECK(localBuzzer.request(BuzzerPattern::LONG));
  CHECK(localBuzzer.active == BuzzerPattern::LONG);
  CHECK(localBuzzer.beepCount == 1);
  CHECK(localBuzzer.onMs == BUZZER_LONG_ON_MS);
  drainLocalBuzzer();
  CHECK(localBuzzer.request(BuzzerPattern::DOUBLE));
  CHECK(localBuzzer.active == BuzzerPattern::DOUBLE);
  CHECK(localBuzzer.beepCount == 2);
  CHECK(localBuzzer.onMs == BUZZER_BEEP_ON_MS);
  drainLocalBuzzer();
}

void w82_pulse_train_loops_until_deadline_or_stopIf() {
  resetHarness(false, false);
  CHECK(localBuzzer.ready);
  CHECK(startPulseTrain(BuzzerPattern::PULSE_TRAIN, 0));
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_TRAIN);
  CHECK(localBuzzer.looping);
  CHECK(localBuzzer.onMs == BUZZER_PULSE_TRAIN_ON_MS);
  CHECK(localBuzzer.gapMs == BUZZER_PULSE_TRAIN_GAP_MS);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += BUZZER_PULSE_TRAIN_ON_MS;
  localBuzzer.service(hostMillis);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_TRAIN);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_PULSE_TRAIN_GAP_MS;
  localBuzzer.service(hostMillis);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_TRAIN);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  localBuzzer.stopIf(BuzzerPattern::PULSE_TRAIN);
  CHECK(!localBuzzer.busy());
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
}

void w83_web_buzzer_test_pulse_uses_same_train_for_3s() {
  resetHarness(false, false);
  reachReadyFromBoot();
  BuzzerPattern parsed = BuzzerPattern::NONE;
  CHECK(parseBuzzerPatternId("pulse2", parsed));
  CHECK(parsed == BuzzerPattern::PULSE_TRAIN);
  CHECK(parseBuzzerPatternId("pulse", parsed));
  CHECK(parsed == BuzzerPattern::PULSE_TRAIN);
  WebCommand command = webControlCommand(WebCommandType::BUZZER_TEST);
  command.buzzerPattern = BuzzerPattern::PULSE_TRAIN;
  const uint32_t before = localBuzzer.acceptedRequests;
  processWebCommand(command);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_TRAIN);
  CHECK(localBuzzer.looping);
  CHECK(localBuzzer.onMs == BUZZER_PULSE_TRAIN_ON_MS);
  CHECK(localBuzzer.gapMs == BUZZER_PULSE_TRAIN_GAP_MS);
  runLoopAfter(BUZZER_PULSE_TRAIN_DEBUG_MS - 20);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_TRAIN);
  runLoopAfter(40);
  CHECK(!localBuzzer.busy());
}

void w84_pulse_train_yields_to_triple() {
  resetHarness(false, false);
  CHECK(startExtendedPulseTrain(0));
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_4HZ);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.active == BuzzerPattern::TRIPLE);
  CHECK(!buzzerPatternIsPulseTrain(localBuzzer.pending));
  drainLocalBuzzer();
}

void w85_debug_pulse_rates_use_same_on_ms_and_3s() {
  resetHarness(false, false);
  reachReadyFromBoot();
  BuzzerPattern parsed = BuzzerPattern::NONE;
  CHECK(parseBuzzerPatternId("pulse2", parsed));
  CHECK(parsed == BuzzerPattern::PULSE_TRAIN);
  CHECK(parseBuzzerPatternId("pulse3", parsed));
  CHECK(parsed == BuzzerPattern::PULSE_3HZ);
  CHECK(parseBuzzerPatternId("pulse4", parsed));
  CHECK(parsed == BuzzerPattern::PULSE_4HZ);
  CHECK(parseBuzzerPatternId("pulse5", parsed));
  CHECK(parsed == BuzzerPattern::PULSE_5HZ);
  CHECK(buzzerPatternForExtendedPulseRate(
            static_cast<uint8_t>(ExtendedPulseRate::OFF)) ==
        BuzzerPattern::NONE);
  CHECK(buzzerPatternForExtendedPulseRate(
            static_cast<uint8_t>(ExtendedPulseRate::SLOW)) ==
        BuzzerPattern::PULSE_TRAIN);
  CHECK(buzzerPatternForExtendedPulseRate(
            static_cast<uint8_t>(ExtendedPulseRate::MEDIUM)) ==
        BuzzerPattern::PULSE_3HZ);
  CHECK(buzzerPatternForExtendedPulseRate(
            static_cast<uint8_t>(ExtendedPulseRate::FAST)) ==
        BuzzerPattern::PULSE_4HZ);
  CHECK(buzzerPatternForExtendedPulseRate(
            static_cast<uint8_t>(ExtendedPulseRate::RAPID)) ==
        BuzzerPattern::PULSE_5HZ);

  WebCommand command = webControlCommand(WebCommandType::BUZZER_TEST);
  command.buzzerPattern = BuzzerPattern::PULSE_3HZ;
  processWebCommand(command);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_3HZ);
  CHECK(localBuzzer.looping);
  CHECK(localBuzzer.onMs == BUZZER_PULSE_TRAIN_ON_MS);
  CHECK(localBuzzer.gapMs == BUZZER_PULSE_TRAIN_3HZ_GAP_MS);
  runLoopAfter(BUZZER_PULSE_TRAIN_DEBUG_MS - 20);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_3HZ);
  runLoopAfter(40);
  CHECK(!localBuzzer.busy());

  command.buzzerPattern = BuzzerPattern::PULSE_4HZ;
  processWebCommand(command);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_4HZ);
  CHECK(localBuzzer.onMs == BUZZER_PULSE_TRAIN_ON_MS);
  CHECK(localBuzzer.gapMs == BUZZER_PULSE_TRAIN_4HZ_GAP_MS);
  runLoopAfter(BUZZER_PULSE_TRAIN_DEBUG_MS + 20);
  CHECK(!localBuzzer.busy());

  command.buzzerPattern = BuzzerPattern::PULSE_5HZ;
  processWebCommand(command);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_5HZ);
  CHECK(localBuzzer.onMs == BUZZER_PULSE_TRAIN_ON_MS);
  CHECK(localBuzzer.gapMs == BUZZER_PULSE_TRAIN_5HZ_GAP_MS);
  runLoopAfter(BUZZER_PULSE_TRAIN_DEBUG_MS + 20);
  CHECK(!localBuzzer.busy());
}

void w60_web_buzzer_test_plays_requested_pattern() {
  resetHarness(false, false);
  reachReadyFromBoot();
  BuzzerPattern parsed = BuzzerPattern::NONE;
  CHECK(parseBuzzerPatternId("triple", parsed));
  CHECK(parsed == BuzzerPattern::TRIPLE);
  WebCommand command = webControlCommand(WebCommandType::BUZZER_TEST);
  command.buzzerPattern = BuzzerPattern::TRIPLE;
  const uint32_t before = localBuzzer.acceptedRequests;
  processWebCommand(command);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.active == BuzzerPattern::TRIPLE);
}

void w61_web_buzzer_test_rejected_while_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  WebCommand command = webControlCommand(WebCommandType::BUZZER_TEST);
  command.buzzerPattern = BuzzerPattern::SINGLE;
  const uint32_t before = localBuzzer.acceptedRequests;
  processWebCommand(command);
  CHECK(localBuzzer.acceptedRequests == before);
  CHECK(session.active);
}

bool executePendingScaleDebugCommand() {
  BookooDebugAction action = BookooDebugAction::START;
  uint8_t level = 0;
  if (!takeScaleDebugCommand(action, level)) {
    return false;
  }
  executeScaleDebugCommand(action, level);
  return true;
}

void w66_web_bookoo_debug_dispatches_actions() {
  static const struct {
    const char *id;
    BookooDebugAction action;
    uint8_t level;
    const char *expected;
  } cases[] = {
      {"start", BookooDebugAction::START, 0, "startTimer"},
      {"stop", BookooDebugAction::STOP, 0, "stopTimer"},
      {"tare", BookooDebugAction::TARE, 0, "tare"},
      {"combined", BookooDebugAction::COMBINED, 0, "tareStartTimer"},
      {"beep", BookooDebugAction::BEEP, 0, "beepWithoutStateChange"},
      {"volume", BookooDebugAction::VOLUME, 0, "setBeepLevel:0"},
      {"volume", BookooDebugAction::VOLUME, 3, "setBeepLevel:3"},
      {"volume", BookooDebugAction::VOLUME, 5, "setBeepLevel:5"},
  };
  for (const auto &entry : cases) {
    BookooDebugAction parsed = BookooDebugAction::START;
    CHECK(parseBookooDebugActionId(entry.id, parsed));
    CHECK(parsed == entry.action);
    resetHarness(false, true);
    reachReadyFromBoot();
    scale.commandLog.clear();
    WebCommand command = webControlCommand(WebCommandType::BOOKOO_DEBUG);
    command.bookooDebugAction = entry.action;
    command.bookooBeepLevel = entry.level;
    processWebCommand(command);
    CHECK(scaleDebugPending);
    CHECK(executePendingScaleDebugCommand());
    CHECK(scale.commandLog.size() == 1);
    CHECK(scale.commandLog[0] == entry.expected);
  }
}

void w67_web_bookoo_debug_rejected_while_active() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  WebCommand command = webControlCommand(WebCommandType::BOOKOO_DEBUG);
  command.bookooDebugAction = BookooDebugAction::TARE;
  processWebCommand(command);
  CHECK(!scaleDebugPending);
  CHECK(session.active);
}

void w68_web_bookoo_debug_rejected_when_disconnected() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand command = webControlCommand(WebCommandType::BOOKOO_DEBUG);
  command.bookooDebugAction = BookooDebugAction::START;
  processWebCommand(command);
  CHECK(!scaleDebugPending);
  CHECK(scale.commandLog.empty());
}

void w69_web_bookoo_debug_rejected_when_not_generic() {
  resetHarness(false, true);
  reachReadyFromBoot();
  std::strncpy(scale.connectedProtocol, "acaia_new",
               sizeof(scale.connectedProtocol) - 1);
  WebCommand command = webControlCommand(WebCommandType::BOOKOO_DEBUG);
  command.bookooDebugAction = BookooDebugAction::TARE;
  processWebCommand(command);
  CHECK(!scaleDebugPending);
  CHECK(scale.commandLog.empty());
}

void w70_bookoo_connect_mutes_in_buzzer_only() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = true;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  scale.commandLog.clear();
  applyBookooConnectBeepPolicy();
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:0");
}

void w71_bookoo_connect_sets_volume_in_scale_priority() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = true;
  runtimeConfig.bookooConnectBeepLevel = DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  scale.commandLog.clear();
  applyBookooConnectBeepPolicy();
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:4");
}

void w72_bookoo_connect_skips_disabled_volume_and_non_bookoo() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooConnectBeepLevel = 0;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  scale.commandLog.clear();
  applyBookooConnectBeepPolicy();
  CHECK(scale.commandLog.empty());

  runtimeConfig.bookooConnectBeepLevel = 4;
  std::strncpy(scale.connectedProtocol, "acaia_new",
               sizeof(scale.connectedProtocol) - 1);
  applyBookooConnectBeepPolicy();
  CHECK(scale.commandLog.empty());
}

void w73_apply_config_buzzer_only_sends_bookoo_silence() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = true;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  scale.commandLog.clear();
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  processWebCommand(update);
  CHECK(scaleDebugPending);
  CHECK(executePendingScaleDebugCommand());
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:0");
}

void w74_apply_config_enabling_mute_sends_silence_only_in_buzzer_only() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = false;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  scale.commandLog.clear();
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.bookooMuteOnBuzzerOnly = true;
  processWebCommand(update);
  CHECK(scaleDebugPending);
  CHECK(executePendingScaleDebugCommand());
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:0");

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = false;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  scale.commandLog.clear();
  update = WebCommand{};
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.bookooMuteOnBuzzerOnly = true;
  processWebCommand(update);
  CHECK(!scaleDebugPending);
  CHECK(scale.commandLog.empty());
}

void w75_bookoo_discovery_connect_applies_beep_policy() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = true;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  scale.scanning = true;
  scale.connected = true;
  scale.commandLog.clear();
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = 1000;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:0");
}

void w76_buzzer_only_start_beeps_at_cn9_not_ble_result() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w77_scale_priority_disconnected_beeps_on_cn9_without_ble() {
  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  const uint32_t afterStart = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::PADDLE, StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == afterStart + 1);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void w78_scale_priority_connected_start_does_not_use_buzzer() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before);
}

void w79_buzzer_only_stop_beeps_before_timer_stop_result() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  const uint32_t before = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::PADDLE, StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w80_buzzer_only_retare_beeps_before_tare_result() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t before = localBuzzer.acceptedRequests;
  publishStableCupWeight(150.0f, 10);
  CHECK(session.retarePerformed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w81_scale_priority_failed_start_falls_back_after_disconnect() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  reachReadyFromBoot();
  startCycle();
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void setHostPreferredScaleMac(const char *mac) {
  strncpy(scalePreferredMac, mac, PREFERRED_SCALE_MAC_CAPACITY - 1);
  scalePreferredMac[PREFERRED_SCALE_MAC_CAPACITY - 1] = '\0';
}

void d01_idle_scan_stays_enabled_between_ticks() {
  resetHarness(false, false);
  reachReadyFromBoot();
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  hostMillis = SCALE_CONNECT_RETRY_MS;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.startScanCalls == 1);
  const size_t calls = scale.startScanCalls;
  hostMillis += SCALE_DISCOVERY_TICK_MS - 1;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.startScanCalls == calls);
}

void d02_full_empty_mac_uses_name_scan() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  scalePreferredMac[0] = '\0';
  hostMillis = SCALE_CONNECT_RETRY_MS;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
  CHECK(scale.lastStartScanMac[0] == '\0');
}

void d03_scan_start_failed_uses_backoff() {
  resetHarness(false, false);
  reachReadyFromBoot();
  scale.startScanSucceeds = false;
  hostMillis = SCALE_CONNECT_RETRY_MS;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(!scale.scanning);
  CHECK(connectRetryMs == SCALE_CONNECT_RETRY_MS * 2U);
  CHECK(scale.startScanCalls == 1);
  hostMillis += SCALE_CONNECT_RETRY_MS;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.startScanCalls == 1);
  hostMillis += SCALE_CONNECT_RETRY_MS;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.startScanCalls == 2);
  CHECK(connectRetryMs == SCALE_CONNECT_RETRY_MS * 4U);
}

void d04_full_cache_keeps_directed_scan() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  hostMillis = SCALE_CONNECT_RETRY_MS;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  for (uint8_t tick = 0; tick < 3; ++tick) {
    hostMillis += SCALE_DISCOVERY_TICK_MS;
    serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                connectRetryMs, connectAttemptSeriesActive,
                                scanSessionAtMs);
  }
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(strcmp(scale.lastStartScanMac, "AA:BB:CC:DD:EE:FF") == 0);
}

void d05_hci_watchdog_force_restarts_same_filter() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  hostMillis = SCALE_CONNECT_RETRY_MS;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.startScanCalls == 1);
  CHECK(!scale.lastForceRestart);
  const size_t callsBeforeRestart = scale.startScanCalls;
  size_t ticks = 0;
  while (scale.startScanCalls == callsBeforeRestart) {
    hostMillis += SCALE_DISCOVERY_TICK_MS;
    serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                connectRetryMs, connectAttemptSeriesActive,
                                scanSessionAtMs);
    ++ticks;
    CHECK(ticks < 40);
  }
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.lastForceRestart);
  CHECK(scale.startScanCalls == 2);
}

void d06_forget_pauses_discovery_for_30s() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  hostMillis = SCALE_CONNECT_RETRY_MS;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  const size_t callsBeforeForget = scale.startScanCalls;
  scale.connected = true;
  clearPreferredScaleCache();
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(!scale.connected);
  CHECK(!scale.scanning);
  CHECK(scale.startScanCalls == callsBeforeForget);
  hostMillis += SCALE_PAIRING_DISCOVERY_PAUSE_MS - 1;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(!scale.scanning);
  CHECK(scale.startScanCalls == callsBeforeForget);
  hostMillis += 2;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs, connectRetryMs,
                              connectAttemptSeriesActive, scanSessionAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
  CHECK(scale.lastStartScanMac[0] == '\0');
}

void w62_local_buzzer_drive_matches_compile_flag() {
  resetHarness(false, false);
  CHECK(localBuzzer.ready);
  CHECK(hostPinMode[BUZZER_GPIO] == OUTPUT);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
#if SHOT_STOPPER_ENABLE_BUZZER == 2
  CHECK(BUZZER_ACTIVE_DRIVE);
  CHECK(hostLedcAttachCalls == 0);
#else
  CHECK(!BUZZER_ACTIVE_DRIVE);
  CHECK(hostLedcAttachCalls == 1);
#endif
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
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
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
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
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
  CHECK(session.automaticEnabled);
  CHECK(stopperState == StopperState::BREW);
  CHECK(!session.receivedFreshWeightInCycle);
  CHECK(getRelaySafetySnapshot().closed);
}

void r22_confirmed_implausible_weight_stops_fail_safe() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(session.automaticEnabled);
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  endBbwProtectionForTests();
  publishWeight(900.0f, hostMillis, 1, 10);
  CHECK(session.weightControlState == WeightControlState::VALIDATING);
  CHECK(getRelaySafetySnapshot().closed);
  publishWeight(900.0f, hostMillis, 1, 11);
  publishWeight(900.0f, hostMillis, 1, 12);
  CHECK(session.directStopPending);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
  CHECK(!getRelaySafetySnapshot().closed);
}

void r23_maintenance_is_canceled_fail_open_by_physical_paddle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand command;
  command.type = WebCommandType::RESTART;
  command.requestId = 77;
  processWebCommand(command);
  CHECK(maintenanceLease.active);
  CHECK(!maintenanceLease.forwarded);
  CHECK(!getRelaySafetySnapshot().closed);

  setRawPaddle(true);
  CHECK(!maintenanceLease.active);
  CHECK(!maintenanceCancellationPending);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w86_config_applies_to_ram_immediately_and_coalesces() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t firstRevision = runtimeConfig.revision;
  const bool originalAvoid = runtimeConfig.avoidBbwShotWithoutScale;
  WebCommand first;
  first.type = WebCommandType::APPLY_CONFIG;
  first.config = runtimeConfig;
  first.config.avoidBbwShotWithoutScale = !originalAvoid;
  processWebCommand(first);
  CHECK(!maintenanceLease.active);
  CHECK(runtimeConfig.avoidBbwShotWithoutScale == !originalAvoid);
  CHECK(runtimeConfig.revision == firstRevision + 1);
  CHECK(runtimePersistPending);

  WebCommand second;
  second.type = WebCommandType::APPLY_CONFIG;
  second.config = runtimeConfig;
  second.config.lastShotCooldownMs = runtimeConfig.lastShotCooldownMs;
  second.config.avoidBbwShotWithoutScale = originalAvoid;
  processWebCommand(second);
  CHECK(runtimeConfig.avoidBbwShotWithoutScale == originalAvoid);
  CHECK(runtimeConfig.revision == firstRevision + 2);
  CHECK(runtimePersistPending);
  CHECK(!maintenanceLease.active);

  setRawPaddle(true);
  CHECK(runtimeConfig.avoidBbwShotWithoutScale == originalAvoid);
  CHECK(runtimeConfig.revision == firstRevision + 2);
}

void w87_nvs_fail_keeps_ram_and_requeues() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t firstRevision = runtimeConfig.revision;
  const bool originalAvoid = runtimeConfig.avoidBbwShotWithoutScale;
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.avoidBbwShotWithoutScale = !originalAvoid;
  processWebCommand(update);
  CHECK(runtimeConfig.avoidBbwShotWithoutScale == !originalAvoid);
  CHECK(runtimeConfig.revision == firstRevision + 1);
  CHECK(runtimePersistPending);

  hostRuntimePersistSucceeds = false;
  runLoopAfter(RUNTIME_PERSIST_DEBOUNCE_MS + 1);
  CHECK(runtimeConfig.avoidBbwShotWithoutScale == !originalAvoid);
  CHECK(runtimeConfig.revision == firstRevision + 1);
  CHECK(runtimePersistPending);
  CHECK(runtimePersistFailed);
  CHECK(hostRuntimePersistAttempts >= 1);
  CHECK(publishedControlStatus.configPersistFailed);

  hostRuntimePersistSucceeds = true;
  runLoopAfter(RUNTIME_PERSIST_RETRY_MS + 1);
  CHECK(!runtimePersistPending);
  CHECK(!runtimePersistFailed);
  CHECK(hostLastFlushIncludedLive);
  CHECK(hostLastFlushedRuntime.revision == runtimeConfig.revision);
  CHECK(hostLastFlushedRuntime.avoidBbwShotWithoutScale == !originalAvoid);
}

void w88_save_network_flush_includes_live_runtime() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t firstRevision = runtimeConfig.revision;
  WebCommand apply;
  apply.type = WebCommandType::APPLY_CONFIG;
  apply.config = runtimeConfig;
  apply.config.avoidBbwShotWithoutScale = !runtimeConfig.avoidBbwShotWithoutScale;
  processWebCommand(apply);
  CHECK(runtimePersistPending);
  CHECK(runtimeConfig.revision == firstRevision + 1);

  WebCommand network;
  network.type = WebCommandType::SAVE_NETWORK;
  strcpy(network.ssid, "CafeLAN");
  strcpy(network.password, "CafePass1");
  processWebCommand(network);
  finishHostMaintenance();
  CHECK(hostLastFlushIncludedLive);
  CHECK(hostLastFlushedRuntime.revision == runtimeConfig.revision);
  CHECK(hostLastFlushedRuntime.avoidBbwShotWithoutScale ==
        runtimeConfig.avoidBbwShotWithoutScale);
  CHECK(!runtimePersistPending);
}

void w89_restart_flush_includes_live_and_aborts_on_fail() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand apply;
  apply.type = WebCommandType::APPLY_CONFIG;
  apply.config = runtimeConfig;
  apply.config.lastShotCooldownMs = runtimeConfig.lastShotCooldownMs + 1000;
  processWebCommand(apply);
  const uint32_t liveRevision = runtimeConfig.revision;
  CHECK(runtimePersistPending);

  hostRuntimePersistSucceeds = false;
  WebCommand restart;
  restart.type = WebCommandType::RESTART;
  processWebCommand(restart);
  finishHostMaintenance();
  CHECK(runtimeConfig.revision == liveRevision);
  CHECK(runtimeConfig.lastShotCooldownMs == apply.config.lastShotCooldownMs);
  CHECK(runtimePersistPending);
  CHECK(runtimePersistFailed);
  CHECK(hostLastFlushedRuntime.revision == liveRevision);

  hostRuntimePersistSucceeds = true;
  processWebCommand(restart);
  finishHostMaintenance();
  CHECK(!runtimePersistPending);
  CHECK(!runtimePersistFailed);
  CHECK(hostLastFlushedRuntime.revision == liveRevision);
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
  ScaleEvent filler = first;
  filler.cycleId = UINT32_MAX;
  for (size_t index = 0; index < SCALE_EVENT_QUEUE_LENGTH; ++index) {
    CHECK(xQueueSend(scaleEventQueue, &filler, 0) == pdTRUE);
  }
  CHECK(publishScaleEvent(first, true));
  CHECK(publishScaleEvent(latest, true));
  CHECK(scaleCriticalEventPending);
  CHECK(scaleEventsDropped == 1);
  CHECK(scaleCriticalEvent.cycleId == 2);
  ScaleEvent startResult = first;
  startResult.type = ScaleEventType::TIMER_START_RESULT;
  startResult.cycleId = 3;
  CHECK(publishScaleEvent(startResult, true));
  CHECK(scaleTimerStartEventPending);
  CHECK(scaleCriticalEventPending);
  processScaleWorkerEvents();
  CHECK(!scaleCriticalEventPending);
  CHECK(!scaleTimerStartEventPending);
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

void r29_direct_threshold_stops_before_regression_is_ready() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  CHECK(shot.datapoints < TREND_POINT_COUNT);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  CHECK(getRelaySafetySnapshot().closed);
  publishWeight(threshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
  CHECK(!getRelaySafetySnapshot().closed);
}

void r30_first_abrupt_sample_uses_pre_shot_baseline() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runtimeConfig.autoTare = false;
  startCycle();
  resetWeightTrend();
  CHECK(session.hasWeightAnchor);
  CHECK(session.lastAcceptedWeightG == 0.0f);
  publishWeight(900.0f, hostMillis + 1);
  CHECK(shot.datapoints == 0);
  CHECK(session.weightControlState == WeightControlState::VALIDATING);
}

void r31_confirmed_overload_opens_without_learning() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(1500.0f);
  publishWeight(1501.0f, hostMillis + 1);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::WEIGHT_ANOMALY);
  CHECK(!session.calibrationEligible);
  CHECK(!pendingFinalize.pending);
}

void r32_old_connection_generation_cannot_update_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t generation = getScaleLinkSnapshot().connectionGeneration;
  CHECK(generation > 0);
  const uint32_t observedBefore = observedWeightSequence;
  publishWeight(12.0f, hostMillis, generation + 1, 77);
  CHECK(observedWeightSequence == observedBefore);
  CHECK(currentWeight != 12.0f);
}

void r33_weight_mailbox_keeps_latest_and_reports_gap() {
  resetHarness(false, true);
  reachReadyFromBoot();
  ScaleEvent first;
  first.type = ScaleEventType::WEIGHT;
  first.receivedAtMs = hostMillis;
  first.weightG = 1.0f;
  ScaleEvent latest = first;
  latest.weightG = 2.0f;
  CHECK(publishScaleEvent(first, false));
  CHECK(publishScaleEvent(latest, false));
  CHECK(scalePacketGaps == 1);
  processScaleWorkerEvents();
  CHECK(observedWeight == 2.0f);
}

void r34_suspended_control_recovers_after_three_attributed_samples() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  setScaleConnected(false);
  loop();
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  setScaleConnected(true);
  publishWeight(1.0f);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  publishWeight(2.0f, hostMillis + 1);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  publishWeight(3.0f, hostMillis + 2);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(session.automaticEnabled);
  loop();
  CHECK(stopperState == StopperState::BREW);

  // Also cover a disconnect/reconnect that occurs wholly between control-loop
  // iterations: the connection generation must suspend authority before the
  // new sample can enter the old trajectory.
  setScaleConnected(false);
  setScaleConnected(true);
  publishWeight(4.0f, hostMillis + 3);
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(shot.datapoints == 0);
  publishWeight(5.0f, hostMillis + 4);
  publishWeight(6.0f, hostMillis + 5);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
}

void r35_connected_without_weight_stream_is_not_available_indicator() {
  resetHarness(false, true);
  reachReadyFromBoot();
  observedWeightSequence = 0;
  scaleWorkerTaskHandle = reinterpret_cast<TaskHandle_t>(1);
  CHECK(getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED);
  CHECK(currentScaleIndicatorCondition() == ScaleIndicatorCondition::STALE);
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.scaleAvailable);
  CHECK(status.weightStreamState == WeightStreamState::NO_SAMPLE);
  CHECK(!status.observedWeightValid);

  publishWeight(1500.0f);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.observedWeightValid);
  CHECK(status.observedWeightG == 1500.0f);
  CHECK(status.weightStreamState == WeightStreamState::OVERLOAD);
  CHECK(!status.currentWeightValid);

  publishWeight(10.0f);
  CHECK(currentScaleIndicatorCondition() ==
        ScaleIndicatorCondition::AVAILABLE);
  setScaleConnected(false);
  setScaleConnected(true);
  CHECK(currentScaleIndicatorCondition() == ScaleIndicatorCondition::STALE);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(!status.currentWeightValid);
}

bool debugEventExists(DebugCode code, int32_t argument1,
                      int32_t argument2) {
  DebugEvent events[DEBUG_EVENT_CAPACITY] = {};
  const size_t count = copyDebugEvents(0, events, DEBUG_EVENT_CAPACITY);
  for (size_t index = 0; index < count; ++index) {
    const DebugEvent &event = events[index];
    if (event.code != code) {
      continue;
    }
    if (argument1 != INT32_MIN && event.argument1 != argument1) {
      continue;
    }
    if (argument2 != INT32_MIN && event.argument2 != argument2) {
      continue;
    }
    return true;
  }
  return false;
}

void w25b_log_levels_and_cycle_events_reach_ring() {
  resetHarness(false, true);
  reachReadyFromBoot();
  ringRetainLogLevel = LogLevel::INFO;
  serialLogLevel = LogLevel::NONE;
  startCycle();
  CHECK(debugEventExists(DebugCode::CYCLE_STARTED));
  CHECK(debugEventExists(DebugCode::BREW_STARTED) ||
        debugEventExists(DebugCode::MANUAL_CYCLE_STARTED) ||
        debugEventExists(DebugCode::TIMER_ONLY_BREW_STARTED));
  DebugEvent events[DEBUG_EVENT_CAPACITY] = {};
  const size_t count = copyDebugEvents(0, events, DEBUG_EVENT_CAPACITY);
  bool sawLevel = false;
  for (size_t index = 0; index < count; ++index) {
    if (events[index].code == DebugCode::CYCLE_STARTED) {
      CHECK(events[index].level == LogLevel::INFO);
      sawLevel = true;
    }
  }
  CHECK(sawLevel);
  char message[128] = {};
  DebugEvent cn9 = {};
  cn9.code = DebugCode::CN9_ARM_FAILED;
  cn9.argument1 = static_cast<int32_t>(Cn9ArmFailReason::SAFETY_LOCKOUT);
  CHECK(formatLifecycleDebugMessage(cn9, message, sizeof(message)));
  CHECK(std::string(message).find("safety lockout") != std::string::npos);
  CHECK(debugCodeDefaultLevel(DebugCode::CN9_ARM_FAILED) ==
        LogLevel::CRITICAL);
  CHECK(debugCodeDefaultLevel(DebugCode::SCALE_CONNECTING) == LogLevel::DEBUG);
  CHECK(!logLevelAtMost(LogLevel::CRITICAL, LogLevel::NONE));
  CHECK(!logLevelAtMost(LogLevel::DEBUG, LogLevel::NONE));
  CHECK(logLevelAtMost(LogLevel::WARNING, LogLevel::INFO));
  CHECK(logLevelAtMost(LogLevel::INFO, LogLevel::INFO));
  CHECK(!logLevelAtMost(LogLevel::DEBUG, LogLevel::INFO));
}

void r41_negative_weight_in_range_starts_automatic_cycle() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = -236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(session.startedWithScale);
  CHECK(session.awaitingPostTareBaseline);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
}

void r42_weight_below_automation_min_stays_manual() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = -520.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(!session.startedWithScale);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
}

void r43_post_tare_baseline_accepts_zero_after_pre_tare_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(session.awaitingPostTareBaseline);
  CHECK(executeNextScaleCommand());
  publishWeight(236.0f, hostMillis + 1);
  CHECK(session.awaitingPostTareBaseline);
  publishWeight(0.0f, hostMillis + 2);
  CHECK(session.receivedFreshWeightInCycle);
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(session.hasWeightAnchor);
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
}

void r54_post_tare_baseline_keeps_weight_control() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(executeNextScaleCommand());
  publishWeight(236.0f, hostMillis + 1);
  CHECK(session.awaitingPostTareBaseline);
  CHECK(stopperState == StopperState::BREW);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.awaitingPostTareBaseline);
  publishWeight(0.0f, hostMillis + 2);
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(session.automaticEnabled);
}

void r44_first_shot_after_reconnect_enters_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  setScaleConnected(false);
  setScaleConnected(true);
  const uint32_t generation = getScaleLinkSnapshot().connectionGeneration;
  CHECK(generation > 0);
  currentWeight = 236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  currentWeightConnectionGeneration = generation;
  startCycle();
  CHECK(executeNextScaleCommand());
  publishWeight(0.0f, hostMillis + 1, generation, 10);
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.receivedFreshWeightInCycle);
}

void st01_cycle_elapsed_follows_cn9_immediately() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(cycleShotElapsedMs() == 0U);
  runLoopAfter(200);
  CHECK(cycleShotElapsedMs() >= 200U);
  runLoopAfter(50);
  CHECK(cycleShotElapsedMs() >= 250U);
}

void st02_scale_timer_stop_waits_for_lag_plus_extra() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 100;
  startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(session.remoteTimerStarted);
  runLoopAfter(200);
  scaleTimerValid = true;
  scaleTimerMs = 500;
  runLoopAfter(0);
  CHECK(session.scaleStartLagCaptured);
  CHECK(session.scaleStartLagMs >= 200U);

  finalizeCycle(EndReason::PADDLE, StopperState::READY);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  const uint32_t dueAt = pendingScaleTimerStop.dueAtMs;
  runLoopAfter(dueAt - hostMillis - 1);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  runLoopAfter(1);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
}

void rt01_late_cup_triggers_single_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(retareWindowOpen());
  CHECK(!session.retareEnded);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  publishStableCupWeight(150.0f, 10);
  CHECK(session.retarePerformed);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(session.retareEnded);
  CHECK(!session.bbwProtectionEnded);
  CHECK(scale.tareCalls == 1);
  CHECK(scale.startTimerCalls == 0);
  const uint32_t timerAnchor = session.startedAtMs;
  reachBrewState();
  CHECK(session.startedAtMs == timerAnchor);
}

void rt02_sub_minimum_stable_cup_is_ignored() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.minimumCupWeightG = 10.0f;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  publishStableCupWeight(7.0f, 10);
  CHECK(!session.retarePerformed);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 0);
  waitForRetareEnded();
}

void rt03_spike_without_stable_cup_does_not_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.minimumCupWeightG = 10.0f;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  publishWeight(28.0f, hostMillis + 100, 1, 10);
  publishWeight(6.0f, hostMillis + 200, 1, 11);
  publishWeight(5.0f, hostMillis + 300, 1, 12);
  publishWeight(5.0f, hostMillis + 400, 1, 13);
  publishWeight(5.0f, hostMillis + 500, 1, 14);
  CHECK(!session.retarePerformed);
  waitForRetareEnded();
}

void rt04_heavy_cup_does_not_stop_during_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.goalWeightG = 36;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.bbwProtectionEnabled);
  runLoopAfter(1000);
  CHECK(retareWindowOpen());
  const uint32_t heavyAtMs = hostMillis + 50;
  publishWeight(200.0f, heavyAtMs, 1, 20);
  CHECK(bbwWeightStopInhibited());
  CHECK(!session.directStopPending);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(stopperState == StopperState::BREW);
  publishStableCupWeight(200.0f, 30);
  CHECK(session.retarePerformed);
  CHECK(executeNextScaleCommand());
  CHECK(scale.tareCalls >= 1);
}

void rt05_bbw_protection_timeout_enables_stop_without_beep() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  waitForRetareEnded();
  waitForBbwProtectionEnded();
  reachBrewState();
  CHECK(!scaleBeepPending);
  simulateFirstDrops();
  CHECK(scaleBeepPending);
}

void rt06_first_drops_beep_after_bbw_protection_timeout() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  waitForRetareEnded();
  endBbwProtectionForTests();
  CHECK(stopperState == StopperState::BREW);
  simulateFirstDrops();
  CHECK(session.firstDropMs != 0);
  CHECK(scaleBeepPending);
}

void rt07_auto_retare_off_skips_retare_window() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = false;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(session.retareEnded);
  CHECK(!session.bbwProtectionEnded);
  publishStableCupWeight(150.0f, 10);
  CHECK(!session.retarePerformed);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 0);
}

void rt09_coffee_during_retare_beep_on_first_drop_not_at_retare_end() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.firstDropBeep = true;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(retareWindowOpen());
  simulateFirstDrops(0.0f, 10);
  CHECK(session.flowDuringRetare);
  CHECK(session.retareFlowFirstDetectedAtMs != 0);
  CHECK(session.firstDropMs != 0);
  CHECK(session.firstDropMs == session.retareFlowFirstDetectedAtMs);
  CHECK(!session.retarePerformed);
  CHECK(scaleBeepPending);
  waitForRetareEnded();
  CHECK(!session.bbwProtectionEnded);
  CHECK(bbwWeightStopInhibited());
  publishStableCupWeight(150.0f, 20);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 0);
}

void rt10_first_drops_beep_during_bbw_protection() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  waitForRetareEnded();
  CHECK(!session.bbwProtectionEnded);
  simulateFirstDrops(0.0f, 10);
  CHECK(session.firstDropMs != 0);
  CHECK(scaleBeepPending);
  CHECK(!session.bbwProtectionEnded);
  CHECK(bbwWeightStopInhibited());
  waitForBbwProtectionEnded();
  CHECK(session.bbwProtectionEnded);
  CHECK(!bbwWeightStopInhibited());
}

void rs01_fast_samples_wait_for_min_duration() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.retareStabilityMinDurationMs = 300;
  runtimeConfig.retareStabilitySamples = 3;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(150.0f, baseMs, 1, 10);
  publishWeight(150.0f, baseMs + 40U, 1, 11);
  publishWeight(150.0f, baseMs + 80U, 1, 12);
  CHECK(!session.retarePerformed);
  publishWeight(150.0f, baseMs + 300U, 1, 13);
  CHECK(session.retarePerformed);
}

void rs02_slow_samples_meet_min_duration_at_third_sample() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.retareStabilityMinDurationMs = 300;
  runtimeConfig.retareStabilitySamples = 3;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(150.0f, baseMs, 1, 10);
  publishWeight(150.0f, baseMs + 400U, 1, 11);
  publishWeight(150.0f, baseMs + 800U, 1, 12);
  CHECK(session.retarePerformed);
}

void rs03_broken_streak_before_min_duration_does_not_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.retareStabilityMinDurationMs = 300;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(150.0f, baseMs, 1, 10);
  publishWeight(150.0f, baseMs + 40U, 1, 11);
  publishWeight(150.0f, baseMs + 80U, 1, 12);
  publishWeight(90.0f, baseMs + 120U, 1, 13);
  publishWeight(150.0f, baseMs + 500U, 1, 14);
  CHECK(!session.retarePerformed);
}

void rs04_zero_min_duration_retares_on_sample_count_only() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs =
      minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.retareStabilityMinDurationMs = 0;
  runtimeConfig.retareStabilitySamples = 3;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(150.0f, baseMs, 1, 10);
  publishWeight(150.0f, baseMs + 40U, 1, 11);
  publishWeight(150.0f, baseMs + 80U, 1, 12);
  CHECK(session.retarePerformed);
}

void rs05_coffee_during_min_duration_wait_skips_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.retareStabilityMinDurationMs = 300;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(150.0f, baseMs, 1, 10);
  publishWeight(150.0f, baseMs + 40U, 1, 11);
  simulateFirstDrops(0.0f, 20);
  publishWeight(150.0f, baseMs + 300U, 1, 21);
  CHECK(!session.retarePerformed);
  CHECK(session.flowDuringRetare);
  CHECK(session.firstDropMs != 0);
  CHECK(session.retareFlowFirstDetectedAtMs != 0);
}

void r45_slew_rejection_emits_specific_debug_code() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  runtimeConfig.autoTare = false;
  startCycle();
  publishWeight(900.0f, hostMillis + 1);
  CHECK(debugEventExists(DebugCode::SCALE_SAMPLE_REJECTED_SLEW, 90000, 0));
}

void r46_range_rejection_emits_specific_debug_code() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  publishWeight(-520.0f, hostMillis + 1);
  CHECK(debugEventExists(DebugCode::SCALE_SAMPLE_REJECTED_RANGE, -52000,
                        weightToCentigrams(MIN_AUTOMATION_WEIGHT_G)));
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
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis;
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
  session.weightControlState = WeightControlState::ACTIVE;
  CHECK(!stopperUsesManualIndicatorPalette());
  session.weightControlState = WeightControlState::SUSPENDED;
  CHECK(stopperUsesManualIndicatorPalette());
}

void s01_shot_log_filters_short_and_rinse() {
  CHECK(!shotLogEligible(EndReason::SHORT_SHOT, 15000));
  CHECK(!shotLogEligible(EndReason::RINSE_COMPLETE, 15000));
  CHECK(!shotLogEligible(EndReason::PADDLE, 9000));
  CHECK(shotLogEligible(EndReason::PADDLE, 10000));
}

void s02_shot_log_appends_after_drip_delay() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.logEligible = true;
  pendingFinalize.startedWithScale = false;
  pendingFinalize.finalState = StopperState::MANUAL_NO_SCALE;
  pendingFinalize.endReason = EndReason::PADDLE;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs = 120;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  pendingFinalize.endedAtMs = hostMillis;
  runLoopAfter(DRIP_DELAY_MS);
  CHECK(!pendingFinalize.pending);
  CHECK(shotLog.count() == 1);
  ShotLogRecord records[1] = {};
  CHECK(shotLog.copyNewestFirst(records, 1) == 1);
  CHECK(records[0].durationDs == 120);
  CHECK(records[0].shotType ==
        static_cast<uint8_t>(ShotLogType::MANUAL));
}

void s03_shot_log_clear_empties_records() {
  resetHarness(false, true);
  shotLog.clear();
  ShotLogRecord record = {};
  record.durationDs = 100;
  CHECK(shotLog.append(record));
  CHECK(shotLog.count() == 1);
  CHECK(shotLog.clear());
  CHECK(shotLog.count() == 0);
}

void s14_last_shot_persists_every_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  CHECK(!persistedLastShot.valid);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 100);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.cycleId == lastCycle.cycleId);
  CHECK(persistedLastShot.durationMs == lastCycle.durationMs);
  CHECK(persistedLastShot.shotType ==
        static_cast<uint8_t>(LastShotType::MANUAL));
}

void s15_last_shot_updates_weight_after_drip() {
  resetHarness(false, true);
  reachReadyFromBoot();
  persistedLastShot = PersistedLastShot{};
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 7;
  persistedLastShot.weightValid = true;
  persistedLastShot.currentWeightG = 10.0f;
  persistLastShotSnapshot(persistedLastShot);

  currentWeight = 36.4f;
  currentWeightSequence = 5;
  currentWeightReceivedAtMs = hostMillis + 1;
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.cycleId = 7;
  pendingFinalize.logEligible = false;
  pendingFinalize.startedWithScale = true;
  pendingFinalize.lastKnownWeightValid = true;
  pendingFinalize.lastKnownWeightG = 10.0f;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 4;
  runLoopAfter(DRIP_DELAY_MS);
  CHECK(!pendingFinalize.pending);
  CHECK(persistedLastShot.valid);
  CHECK(fabsf(persistedLastShot.currentWeightG - 36.4f) < 0.001f);
}

void s16_last_shot_clear_empties_snapshot() {
  resetHarness(false, false);
  persistedLastShot.valid = true;
  persistLastShotSnapshot(persistedLastShot);
  CHECK(clearLastShot());
  CHECK(!persistedLastShot.valid);
  CHECK(!lastShotStore.get().valid);
}

void n01_wall_clock_tracks_utc_from_anchor() {
  g_wallClock.reset();
  g_wallClock.setSyncing("pool.ntp.org", 1000);
  g_wallClock.queueSyncFromCallback(1'700'000'000U);
  CHECK(g_wallClock.applyPendingSync(1000));
  CHECK(g_wallClock.nowUtcSec(1000) == 1'700'000'000U);
  CHECK(g_wallClock.nowUtcSec(6000) == 1'700'000'005U);
}

void n02_ntp_hostname_validation() {
  CHECK(validNtpHostname("pool.ntp.org"));
  CHECK(validNtpHostname("time.google.com"));
  CHECK(!validNtpHostname(""));
  CHECK(!validNtpHostname("-bad.example"));
  CHECK(!validNtpHostname("bad space"));
}

void n03_unsynced_retry_is_one_minute() {
  CHECK(ntpRetryDelayMs(0) == NTP_UNSYNCED_RETRY_MS);
  CHECK(ntpRetryDelayMs(5) == NTP_UNSYNCED_RETRY_MS);
}

void n04_brew_start_requests_ntp_when_unsynced() {
  resetHarness(false, true);
  reachReadyFromBoot();
  CHECK(hostNtpSyncRequestCount == 0);
  startCycle();
  CHECK(hostNtpSyncRequestCount == 1);
}

void n05_rinse_start_requests_ntp_when_unsynced() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  CHECK(hostNtpSyncRequestCount == 1);
  hostNtpSyncRequestCount = 0;
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(hostNtpSyncRequestCount == 1);
}

void n06_synced_clock_skips_activity_ntp_request() {
  resetHarness(false, true);
  g_wallClock.setSyncing("pool.ntp.org", hostMillis);
  g_wallClock.queueSyncFromCallback(1'700'000'000U);
  CHECK(g_wallClock.applyPendingSync(hostMillis));
  reachReadyFromBoot();
  startCycle();
  CHECK(hostNtpSyncRequestCount == 0);

  resetHarness(false, true);
  g_wallClock.setSyncing("pool.ntp.org", hostMillis);
  g_wallClock.queueSyncFromCallback(1'700'000'000U);
  CHECK(g_wallClock.applyPendingSync(hostMillis));
  reachReadyFromBoot();
  WebCommand rinse = webControlCommand(WebCommandType::RINSE);
  processWebCommand(rinse);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(hostNtpSyncRequestCount == 0);
}

void n07_syncing_clock_skips_activity_ntp_request() {
  resetHarness(false, true);
  g_wallClock.setSyncing("pool.ntp.org", hostMillis);
  reachReadyFromBoot();
  startCycle();
  CHECK(hostNtpSyncRequestCount == 0);
}

void n08_web_rinse_requests_ntp_when_unsynced() {
  resetHarness(false, true);
  reachReadyFromBoot();
  WebCommand rinse = webControlCommand(WebCommandType::RINSE);
  processWebCommand(rinse);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(hostNtpSyncRequestCount == 1);
}

void n09_network_bringup_ignores_paddle() {
  ControlStatusSnapshot status = {};
  status.state = StopperState::BREW;
  status.activeCycle = true;
  status.relayClosed = true;
  status.physicalPaddleOn = true;
  CHECK(!controlAllowsConfiguration(status));
  CHECK(controlAllowsNetworkBringup(status));
}

void feedSerial(const char *text) {
  Serial.inject(text);
  size_t guard = 0;
  while (Serial.available() > 0 && guard < 40) {
    runLoopAfter(0);
    ++guard;
  }
  runLoopAfter(0);
}

bool serialTxContains(const char *text) {
  return Serial.tx.find(text) != std::string::npos;
}

void sc01_hello_replies_how_are_you() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("hello\n");
  CHECK(serialTxContains("how are you"));
}

void sc02_factory_reset_rejected_while_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  feedSerial("FACTORY_RESET\n");
  CHECK(serialTxContains("ERR not ready"));
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
}

void sc03_set_wifi_queues_save_network() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("SET_WIFI CafeLAN CafePass1\n");
  CHECK(serialTxContains("OK queued SET_WIFI"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type == WebCommandType::SAVE_NETWORK);
  CHECK(strcmp(hostLastForwardedNetworkCommand.ssid, "CafeLAN") == 0);
  CHECK(strcmp(hostLastForwardedNetworkCommand.password, "CafePass1") == 0);
  CHECK(!hostLastForwardedNetworkCommand.openNetwork);
}

void sc04_clear_shots_empties_log() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  ShotLogRecord record = {};
  record.durationDs = 100;
  CHECK(shotLog.append(record));
  CHECK(shotLog.count() == 1);
  feedSerial("CLEAR_SHOTS\n");
  CHECK(serialTxContains("OK shots cleared"));
  CHECK(shotLog.count() == 0);
}

void sc05_serial_cli_parser_covers_supported_commands() {
  SerialCliRequest request;
  CHECK(serialCliParseLine("HELLO", request));
  CHECK(request.verb == SerialCliVerb::HELLO);
  CHECK(serialCliParseLine("SET_AP_PASSWORD password1234", request));
  CHECK(request.verb == SerialCliVerb::SET_AP_PASSWORD);
  CHECK(strcmp(request.arg1, "password1234") == 0);
  CHECK(serialCliParseLine("SET_WIFI ssid_de_wifi pass_del_wifi", request));
  CHECK(request.verb == SerialCliVerb::SET_WIFI);
  CHECK(strcmp(request.arg1, "ssid_de_wifi") == 0);
  CHECK(strcmp(request.arg2, "pass_del_wifi") == 0);
  CHECK(!request.openNetwork);
  CHECK(serialCliParseLine("SET_WIFI \"Cafe LAN\" SecretPass1", request));
  CHECK(request.verb == SerialCliVerb::SET_WIFI);
  CHECK(strcmp(request.arg1, "Cafe LAN") == 0);
  CHECK(strcmp(request.arg2, "SecretPass1") == 0);
  CHECK(serialCliParseLine("SET_WIFI OpenNet", request));
  CHECK(request.verb == SerialCliVerb::SET_WIFI);
  CHECK(request.openNetwork);
  CHECK(serialCliParseLine("FACTORY_RESET", request));
  CHECK(request.verb == SerialCliVerb::FACTORY_RESET);
  CHECK(serialCliParseLine("RESET_AP_PASSWORD", request));
  CHECK(request.verb == SerialCliVerb::RESET_AP_PASSWORD);
  CHECK(serialCliParseLine("CLEAR_WIFI", request));
  CHECK(request.verb == SerialCliVerb::CLEAR_WIFI);
  CHECK(serialCliParseLine("RESET_NETWORK_UI", request));
  CHECK(request.verb == SerialCliVerb::RESET_NETWORK_UI);
  CHECK(serialCliParseLine("SERIAL_DEBUG_ON", request));
  CHECK(request.verb == SerialCliVerb::SERIAL_DEBUG_ON);
  CHECK(serialCliParseLine("SERIAL_DEBUG_OFF", request));
  CHECK(request.verb == SerialCliVerb::SERIAL_DEBUG_OFF);
  CHECK(!serialCliParseLine("SERIAL_DEBUG_ON extra", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(serialCliParseLine("SET_AP_PASSWORD Micra1234", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(serialCliParseLine("SET_WIFI Cafe short", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(serialCliParseLine("not_a_command", request));
  CHECK(request.verb == SerialCliVerb::UNKNOWN);
  CHECK(!serialCliParseLine("   ", request));
}

void sc06_serial_cli_feed_completes_on_crlf() {
  SerialCliParser parser;
  SerialCliRequest request;
  bool ready = false;
  const char *line = "HELLO\r\n";
  for (size_t index = 0; line[index] != '\0'; ++index) {
    if (serialCliFeed(parser, line[index], request)) {
      CHECK(!ready);
      CHECK(request.verb == SerialCliVerb::HELLO);
      ready = true;
    }
  }
  CHECK(ready);
}

void sc07_reset_ap_password_and_clear_wifi_queue() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("RESET_AP_PASSWORD\n");
  CHECK(serialTxContains("OK queued RESET_AP_PASSWORD"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type ==
        WebCommandType::RESET_AP_PASSWORD);

  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("CLEAR_WIFI\n");
  CHECK(serialTxContains("OK queued CLEAR_WIFI"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type ==
        WebCommandType::FORGET_NETWORK);
}

void sc08_set_ap_password_queues_change() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("SET_AP_PASSWORD password1234\n");
  CHECK(serialTxContains("OK queued SET_AP_PASSWORD"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type ==
        WebCommandType::CHANGE_AP_PASSWORD);
  CHECK(strcmp(hostLastForwardedNetworkCommand.password, "password1234") == 0);
}

void sc09_serial_debug_toggles_without_ready() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.active);
  CHECK(!runtimeConfig.serialDebugOutput);
  CHECK(serialLogLevel == LogLevel::NONE);
  Serial.tx.clear();
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED);
  serialTrace(LogLevel::WARNING, "Scale name scan: no advertisement");
  CHECK(!serialTxContains("configuration accepted"));
  CHECK(!serialTxContains("Scale name scan: no advertisement"));
  feedSerial("SERIAL_DEBUG_ON\n");
  CHECK(serialTxContains("OK serial debug on"));
  CHECK(runtimeConfig.serialDebugOutput);
  CHECK(serialLogLevel == LogLevel::INFO);
  CHECK(runtimePersistPending);
  Serial.tx.clear();
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED);
  serialTrace(LogLevel::WARNING, "Scale name scan: no advertisement");
  CHECK(serialTxContains("configuration accepted"));
  CHECK(serialTxContains("Scale name scan: no advertisement"));
  feedSerial("SERIAL_DEBUG_OFF\n");
  CHECK(serialTxContains("OK serial debug off"));
  CHECK(!runtimeConfig.serialDebugOutput);
  CHECK(serialLogLevel == LogLevel::NONE);
  Serial.tx.clear();
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED);
  serialTrace(LogLevel::WARNING, "Scale name scan: no advertisement");
  CHECK(!serialTxContains("configuration accepted"));
  CHECK(!serialTxContains("Scale name scan: no advertisement"));
}

void s04_shot_log_remove_by_id() {
  resetHarness(false, true);
  shotLog.clear();
  ShotLogRecord first = {};
  first.durationDs = 100;
  CHECK(shotLog.append(first));
  ShotLogRecord second = {};
  second.durationDs = 200;
  CHECK(shotLog.append(second));
  CHECK(shotLog.count() == 2);
  ShotLogRecord records[2] = {};
  CHECK(shotLog.copyNewestFirst(records, 2) == 2);
  CHECK(records[0].durationDs == 200);
  CHECK(records[1].durationDs == 100);
  const uint32_t deleteId = records[0].id;
  CHECK(deleteId != 0);
  CHECK(shotLog.removeById(deleteId));
  CHECK(shotLog.count() == 1);
  CHECK(shotLog.copyNewestFirst(records, 2) == 1);
  CHECK(records[0].durationDs == 100);
  CHECK(!shotLog.removeById(deleteId));
  CHECK(!shotLog.removeById(99999U));
}

void s05_shot_log_migrates_schema_v2() {
  ShotLogStoreV2 legacy = {};
  legacy.header.bootId = 3;
  legacy.header.nextRecordId = 2;
  legacy.header.count = 1;
  legacy.header.writeIndex = 1;
  legacy.records[0].id = 1;
  legacy.records[0].bootId = 3;
  legacy.records[0].endedAtMs = 45000;
  legacy.records[0].durationDs = 285;
  legacy.records[0].goalWeightG = 18;
  finalizeShotLogStoreV2(legacy);
  CHECK(validShotLogStoreV2(legacy));

  ShotLogStore migrated = {};
  migrateShotLogStoreV2(legacy, migrated);
  CHECK(validShotLogStore(migrated));
  CHECK(migrated.header.count == 1);
  CHECK(migrated.header.bootId == 3);
  CHECK(migrated.records[0].id == 1);
  CHECK(migrated.records[0].durationDs == 285);
  CHECK(migrated.records[0].hasWallTime == 0);
  CHECK(migrated.records[0].endedAtLocalSec == 0);
}

void s06_shot_log_local_sec_from_utc() {
  CHECK(shotLogLocalSecFromUtc(1'700'000'000U, -240) ==
        1'700'000'000U - 14400U);
  CHECK(shotLogLocalSecFromUtc(1'700'000'000U, 60) == 1'700'000'000U + 3600U);
}

void s07_shot_log_stores_fixed_wall_time() {
  resetHarness(false, true);
  shotLog.clear();
  ShotLogRecord record = {};
  record.durationDs = 120;
  record.hasWallTime = 1;
  record.endedAtUnixSec = 1'700'000'000U;
  record.endedAtLocalSec = shotLogLocalSecFromUtc(1'700'000'000U, -240);
  record.timezoneOffsetMinutesAtCommit = -240;
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(stored[0].hasWallTime == 1);
  CHECK(stored[0].endedAtLocalSec ==
        shotLogLocalSecFromUtc(1'700'000'000U, -240));
  CHECK(stored[0].timezoneOffsetMinutesAtCommit == -240);
}

void s08_shot_log_without_sync_has_no_wall_time() {
  resetHarness(false, true);
  shotLog.clear();
  ShotLogRecord record = {};
  record.durationDs = 120;
  record.bootId = shotLog.bootId();
  record.endedAtMs = 45000;
  record.hasWallTime = 0;
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(stored[0].hasWallTime == 0);
  CHECK(stored[0].endedAtLocalSec == 0);
  CHECK(stored[0].endedAtUnixSec == 0);
}

void s09_shot_log_migrates_schema_v3() {
  ShotLogStoreV3 legacy = {};
  legacy.header.bootId = 2;
  legacy.header.nextRecordId = 2;
  legacy.header.count = 1;
  legacy.header.writeIndex = 1;
  legacy.records[0].id = 1;
  legacy.records[0].bootId = 2;
  legacy.records[0].endedAtMs = 90000;
  legacy.records[0].endedAtUnixSec = 1'700'000'000U;
  legacy.records[0].durationDs = 150;
  finalizeShotLogStoreV3(legacy);
  CHECK(validShotLogStoreV3(legacy));

  ShotLogStore migrated = {};
  migrateShotLogStoreV3(legacy, migrated);
  CHECK(validShotLogStore(migrated));
  CHECK(migrated.records[0].endedAtUnixSec == 1'700'000'000U);
  CHECK(migrated.records[0].hasWallTime == 0);
  CHECK(migrated.records[0].endedAtLocalSec == 0);
}

void s10_shot_log_migrates_schema_v4() {
  ShotLogStoreV4 legacy = {};
  legacy.header.bootId = 4;
  legacy.header.nextRecordId = 2;
  legacy.header.count = 1;
  legacy.header.writeIndex = 1;
  legacy.records[0].id = 1;
  legacy.records[0].bootId = 4;
  legacy.records[0].durationDs = 260;
  legacy.records[0].goalWeightG = 36;
  legacy.header.magic = SHOT_LOG_MAGIC;
  legacy.header.schemaVersion = 4;
  legacy.header.recordSize = sizeof(ShotLogRecordV4);
  legacy.header.checksum = shotLogChecksumBytes(legacy.header);

  ShotLogStore migrated = {};
  migrateShotLogStoreV4(legacy, migrated);
  CHECK(validShotLogStore(migrated));
  CHECK(migrated.records[0].extractionGuardEnabled == 0);
  CHECK(migrated.records[0].stopDetail ==
        static_cast<uint8_t>(ShotLogStopDetail::NORMAL_TARGET));
}

void s11_shot_log_record_stays_v5_size() {
  CHECK(sizeof(ShotLogRecord) == 48);
  CHECK(sizeof(ShotLogRecord) == sizeof(ShotLogRecordV5));
  CHECK(sizeof(ShotLogStore) == sizeof(ShotLogStoreV5));
}

void s12_shot_log_migrates_schema_v5() {
  ShotLogStoreV5 legacy = {};
  legacy.header.bootId = 5;
  legacy.header.nextRecordId = 2;
  legacy.header.count = 1;
  legacy.header.writeIndex = 1;
  legacy.records[0].id = 1;
  legacy.records[0].bootId = 5;
  legacy.records[0].durationDs = 280;
  legacy.records[0].goalWeightG = 36;
  legacy.records[0].actualWeightCg = 3600;
  legacy.header.magic = SHOT_LOG_MAGIC;
  legacy.header.schemaVersion = 5;
  legacy.header.recordSize = sizeof(ShotLogRecordV5);
  legacy.header.checksum = shotLogChecksumBytes(legacy.header);
  CHECK(validShotLogStoreV5(legacy));

  ShotLogStore migrated = {};
  migrateShotLogStoreV5(legacy, migrated);
  CHECK(validShotLogStore(migrated));
  CHECK(migrated.records[0].actualWeightSource ==
        static_cast<uint8_t>(ActualWeightSource::POST_DRIP));
  compactShotLogStore(migrated);
  finalizeShotLogStore(migrated);
  CHECK(shotLogPersistedBytes(migrated) ==
        sizeof(ShotLogHeader) + sizeof(ShotLogRecord));
}

void s13_persist_debug_messages_identify_origin() {
  char message[128] = {};
  DebugEvent shotLogEvent = {};
  shotLogEvent.code = DebugCode::SHOT_LOG_PERSIST_FAILED;
  shotLogEvent.argument1 = 72;
  shotLogEvent.argument2 = 0;
  CHECK(formatPersistDebugMessage(shotLogEvent, message, sizeof(message)));
  CHECK(strstr(message, "shot history") != nullptr);
  CHECK(strstr(message, "shotlog/records") != nullptr);
  CHECK(strstr(message, "ShotLog.save") != nullptr);

  DebugEvent runtimeEvent = {};
  runtimeEvent.code = DebugCode::RUNTIME_PERSIST_FAILED;
  runtimeEvent.argument1 = 9;
  runtimeEvent.argument2 = RUNTIME_PERSIST_REASON_ATM_SAMPLES;
  CHECK(formatPersistDebugMessage(runtimeEvent, message, sizeof(message)));
  CHECK(strstr(message, "PERSIST_RUNTIME") != nullptr);
  CHECK(strstr(message, "settingsA/B") != nullptr);
  CHECK(strstr(message, "A->M duration samples") != nullptr);
  CHECK(strstr(message, "learned weight offset") == nullptr);

  runtimeEvent.argument2 =
      RUNTIME_PERSIST_REASON_ATM_SAMPLES | RUNTIME_PERSIST_REASON_OFFSET;
  CHECK(formatPersistDebugMessage(runtimeEvent, message, sizeof(message)));
  CHECK(strstr(message, "A->M samples+offset") != nullptr);
  CHECK(strlen(message) < sizeof(message));
}

void h01_health_threshold_alerts_fire_once_per_crossing() {
  resetHarness(false, true);
  freeHeapBytes = HEALTH_HEAP_FREE_ALERT_BYTES - 1;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_CLEAR_BYTES;
  loopStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  scaleWorkerStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  serviceHealthThresholdAlerts(0);
  CHECK(debugEventExists(DebugCode::HEALTH_HEAP_LOW,
                         static_cast<int32_t>(freeHeapBytes),
                         static_cast<int32_t>(largestFreeHeapBlockBytes)));
  debugLog.clear();
  serviceHealthThresholdAlerts(0);
  CHECK(!debugEventExists(DebugCode::HEALTH_HEAP_LOW));

  freeHeapBytes = HEALTH_HEAP_FREE_CLEAR_BYTES;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_CLEAR_BYTES;
  serviceHealthThresholdAlerts(0);
  CHECK(!healthHeapAlertLatched);

  loopStackMinWords = HEALTH_STACK_MIN_ALERT_WORDS - 1;
  serviceHealthThresholdAlerts(0);
  CHECK(debugEventExists(DebugCode::HEALTH_STACK_LOW));
  debugLog.clear();
  serviceHealthThresholdAlerts(0);
  CHECK(!debugEventExists(DebugCode::HEALTH_STACK_LOW));

  loopStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  serviceHealthThresholdAlerts(0);
  CHECK(!healthStackAlertLatched);

  serviceHealthThresholdAlerts(HEALTH_LOOP_GAP_ALERT_MS);
  CHECK(debugEventExists(DebugCode::HEALTH_LOOP_GAP,
                         static_cast<int32_t>(HEALTH_LOOP_GAP_ALERT_MS)));
  debugLog.clear();
  serviceHealthThresholdAlerts(HEALTH_LOOP_GAP_ALERT_MS);
  CHECK(!debugEventExists(DebugCode::HEALTH_LOOP_GAP));
  serviceHealthThresholdAlerts(HEALTH_LOOP_GAP_CLEAR_MS);
  CHECK(!healthLoopGapAlertLatched);
}

void r51_auto_to_manual_guard_fires_while_scale_lost() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = true;
  runtimeConfig.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
  runtimeConfig.autoToManualGuardManualLimitMs = 15000;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
  setScaleConnected(false);
  loop();
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(session.autoToManualGuardEnforced);
  reachSessionElapsed(15000);
  CHECK(session.endReason == EndReason::AUTO_TO_MANUAL_GUARD);
  CHECK(!getRelaySafetySnapshot().closed);
}

void r52_auto_to_manual_guard_clears_on_scale_recovery() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = true;
  runtimeConfig.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
  runtimeConfig.autoToManualGuardManualLimitMs = 20000;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  const uint32_t deadline = session.autoToManualGuardDeadlineAtMs;
  CHECK(session.autoToManualGuardArmed);
  setScaleConnected(false);
  loop();
  CHECK(session.autoToManualGuardEnforced);
  setScaleConnected(true);
  publishWeight(1.0f);
  publishWeight(2.0f, hostMillis + 1);
  publishWeight(3.0f, hostMillis + 2);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(!session.autoToManualGuardEnforced);
  CHECK(session.autoToManualGuardDeadlineAtMs == deadline);
  setScaleConnected(false);
  loop();
  CHECK(session.autoToManualGuardEnforced);
  CHECK(session.autoToManualGuardDeadlineAtMs == deadline);
}

void r53_auto_to_manual_guard_disabled_does_not_cut_early() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = false;
  runtimeConfig.autoToManualGuardManualLimitMs = 10000;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  CHECK(!session.autoToManualGuardArmed);
  setScaleConnected(false);
  loop();
  reachSessionElapsed(15000);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(session.endReason == EndReason::NONE);
}

void r48_guard_disabled_stops_at_target_normally() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = false;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
}

void r49_guard_extends_and_stops_at_max_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.active == BuzzerPattern::PULSE_4HZ);
  const float maxThreshold = effectiveMaxStopThreshold();
  publishWeight(maxThreshold + 0.1f);
  publishWeight(maxThreshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::FAST_EXTRACTION_MAX_WEIGHT);
  CHECK(localBuzzer.active != BuzzerPattern::PULSE_4HZ);
}

void r51_extended_pulse_respects_alert_flag_and_scale_only() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::OFF);
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(!buzzerPatternIsPulseTrain(localBuzzer.active));

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::FAST);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float scaleOnlyThreshold = effectiveStopThreshold();
  publishWeight(scaleOnlyThreshold + 0.1f);
  publishWeight(scaleOnlyThreshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(!buzzerPatternIsPulseTrain(localBuzzer.active));
}

void r50_guard_extends_and_stops_at_min_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  runLoopAfter(4000);
  session.lastAcceptedWeightG = threshold + 1.0f;
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::FAST_EXTRACTION_MIN_TIME);
}

using TestFunction = void (*)();

struct TestCase {
  const char *id;
  TestFunction function;
};

const TestCase testCases[] = {
    {"T01", t01_boot_with_paddle_off},
    {"T02", t02_boot_with_paddle_on},
    {"T03", t03_sustained_on_enters_brew_once},
    {"T04", t04_exact_rinse_boundary_and_duration},
    {"T05", t05_release_between_rinse_and_brew_is_short_shot},
    {"T06", t06_paddle_off_during_brew},
    {"T07", t07_scale_prediction_requires_release_after_stop},
    {"T08", t08_on_during_rinse_is_ignored},
    {"T09", t09_rinse_ending_on_requires_off},
    {"T10", t10_paddle_bounce_does_not_start_cycle},
    {"T11", t11_ble_loss_suspends_brew_without_late_stop},
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
    {"T23", t23_prediction_triggers_after_bbw_protection_ends},
    {"T24", t24_paddle_off_during_brew_is_immediate},
    {"T25", t25_ble_loss_during_rinse_window_preserves_classification},
    {"T26", t26_reconnected_suspended_cycle_sends_one_stop_on_release},
    {"T27", t27_configuration_is_rejected_while_cycle_is_active},
    {"T28", t28_paddle_motion_cannot_cancel_or_extend_rinse},
    {"R01", r01_transient_disconnect_suspends_weight_control},
    {"R02", r02_stalled_scale_worker_suspends_until_validated},
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
    {"R22", r22_confirmed_implausible_weight_stops_fail_safe},
    {"R23", r23_maintenance_is_canceled_fail_open_by_physical_paddle},
    {"R24", r24_web_control_lease_owner_is_enforced},
    {"R25", r25_critical_scale_mailbox_never_blocks_and_keeps_latest},
    {"R26", r26_remote_timer_stop_retries_after_full_queue},
    {"R27", r27_platform_clock_failure_prevents_cn9_close},
    {"R28", r28_terminal_control_result_is_retained_until_forwarded},
    {"R29", r29_direct_threshold_stops_before_regression_is_ready},
    {"R30", r30_first_abrupt_sample_uses_pre_shot_baseline},
    {"R31", r31_confirmed_overload_opens_without_learning},
    {"R48", r48_guard_disabled_stops_at_target_normally},
    {"R49", r49_guard_extends_and_stops_at_max_weight},
    {"R50", r50_guard_extends_and_stops_at_min_time},
    {"R51", r51_extended_pulse_respects_alert_flag_and_scale_only},
    {"R51", r51_auto_to_manual_guard_fires_while_scale_lost},
    {"R52", r52_auto_to_manual_guard_clears_on_scale_recovery},
    {"R53", r53_auto_to_manual_guard_disabled_does_not_cut_early},
    {"R32", r32_old_connection_generation_cannot_update_weight},
    {"R33", r33_weight_mailbox_keeps_latest_and_reports_gap},
    {"R34", r34_suspended_control_recovers_after_three_attributed_samples},
    {"R35", r35_connected_without_weight_stream_is_not_available_indicator},
    {"R41", r41_negative_weight_in_range_starts_automatic_cycle},
    {"R42", r42_weight_below_automation_min_stays_manual},
    {"R43", r43_post_tare_baseline_accepts_zero_after_pre_tare_weight},
    {"R54", r54_post_tare_baseline_keeps_weight_control},
    {"R44", r44_first_shot_after_reconnect_enters_brew},
    {"R45", r45_slew_rejection_emits_specific_debug_code},
    {"ST01", st01_cycle_elapsed_follows_cn9_immediately},
    {"ST02", st02_scale_timer_stop_waits_for_lag_plus_extra},
    {"RT01", rt01_late_cup_triggers_single_retare},
    {"RT02", rt02_sub_minimum_stable_cup_is_ignored},
    {"RT03", rt03_spike_without_stable_cup_does_not_retare},
    {"RT04", rt04_heavy_cup_does_not_stop_during_retare},
    {"RT05", rt05_bbw_protection_timeout_enables_stop_without_beep},
    {"RT06", rt06_first_drops_beep_after_bbw_protection_timeout},
    {"RT07", rt07_auto_retare_off_skips_retare_window},
    {"RT09", rt09_coffee_during_retare_beep_on_first_drop_not_at_retare_end},
    {"RT10", rt10_first_drops_beep_during_bbw_protection},
    {"RS01", rs01_fast_samples_wait_for_min_duration},
    {"RS02", rs02_slow_samples_meet_min_duration_at_third_sample},
    {"RS03", rs03_broken_streak_before_min_duration_does_not_retare},
    {"RS04", rs04_zero_min_duration_retares_on_sample_count_only},
    {"RS05", rs05_coffee_during_min_duration_wait_skips_retare},
    {"R46", r46_range_rejection_emits_specific_debug_code},
    {"R47", r47_reset_reason_name_maps_known_codes},
    {"W01", w01_default_runtime_configuration_is_valid},
    {"W02", w02_each_runtime_field_is_validated},
    {"W03", w03_runtime_timing_relations_are_transactional},
    {"W04", w04_wifi_credentials_have_strict_bounds},
    {"W05", w05_config_is_blocked_while_brewing_early},
    {"W06", w06_config_is_blocked_while_brewing},
    {"W07", w07_config_is_blocked_while_rinsing},
    {"W08", w08_config_is_blocked_during_manual_cycle},
    {"W09", w09_valid_config_applies_only_from_ready},
    {"W10", w10_cycle_configuration_snapshot_is_immutable},
    {"W11", w11_operational_timer_opens_without_control_loop},
    {"W12", w12_hard_limit_cannot_be_configured_above_sixty_seconds},
    {"W45", w45_bbw_protection_retare_relation_is_validated},
    {"W46", w46_status_reports_uptime_since_boot},
    {"W47", w47_status_reports_live_scale_weight_and_timer},
    {"W13", w13_virtual_paddle_uses_normal_state_machine},
    {"W14", w14_physical_motion_overrides_web_control},
    {"W15", w15_web_rinse_starts_scale_timer},
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
    {"W25B", w25b_log_levels_and_cycle_events_reach_ring},
    {"W26", w26_status_is_a_copied_snapshot},
    {"W27", w27_stale_weight_is_not_presented_as_current},
    {"W28", w28_web_command_queue_is_bounded},
    {"W29", w29_operational_wall_of_60001_is_rejected},
    {"W30", w30_last_cycle_weight_must_belong_to_that_cycle},
    {"W31", w31_unsupported_scale_never_uses_tare_as_a_beep},
    {"W32", w32_full_scale_queue_cannot_block_brew_start},
    {"W33", w33_first_drop_beep_can_be_disabled},
    {"W34", w34_calibration_reset_restores_baseline_and_cancels_analysis},
    {"W35", w35_status_reports_the_live_physical_paddle_gpio},
    {"W36", w36_paddle_return_reminder_beeps_at_configured_interval_only_while_open},
    {"W37", w37_factory_reset_is_rejected_while_control_is_active},
    {"W38", w38_scale_indicator_states_are_unambiguous},
    {"W39", w39_automatic_stopper_palette_encodes_workflow},
    {"W40", w40_manual_and_timer_only_palette_is_salmon},
    {"W41", w41_safety_and_action_states_override_operating_palette},
    {"W42", w42_indicator_blink_periods_are_deterministic},
    {"W43", w43_manual_palette_tracks_timer_only_and_scale_loss},
    {"W44", w44_paddle_return_reminder_stops_after_fifteen_minutes},
    {"W50", w50_local_buzzer_plays_triple_pattern_non_blocking},
    {"W50b", w50b_buzzer_phase_timer_holds_triple_rhythm_without_loop},
    {"W50c", w50c_buzzer_phase_timer_advances_despite_loop_stall},
    {"W51", w51_local_buzzer_triple_on_scale_lost_during_bbw},
    {"W52", w52_local_buzzer_triple_on_manual_bbw_without_scale},
    {"W53", w53_local_buzzer_silent_when_bbw_off_without_scale},
    {"NS01", ns01_armed_blocks_first_bbw_no_scale_shot},
    {"NS02", ns02_idle_allows_second_bbw_no_scale_shot},
    {"NS03", ns03_bbw_off_does_not_block},
    {"NS04", ns04_scale_available_rearms},
    {"NS05", ns05_cooldown_rearms_from_idle},
    {"NS06", ns06_finished_shot_extends_cooldown},
    {"NS07", ns07_web_rinse_does_not_consume_guard},
    {"NS08", ns08_blocked_beep_respects_alert_checkbox},
    {"NS09", ns09_armed_rinse_gesture_runs_and_stays_armed},
    {"NS10", ns10_idle_rinse_gesture_does_not_rearm},
    {"W54", w54_local_buzzer_triple_on_auto_to_manual_guard_end},
    {"W55", w55_local_buzzer_queues_second_triple_while_busy},
    {"W56", w56_atm_beep_queued_when_scale_lost_after_deadline},
    {"W57", w57_paddle_return_reminder_falls_back_to_scale_when_piezo_not_ready},
    {"W58", w58_paddle_return_reminder_does_not_advance_interval_when_muted},
    {"W59", w59_local_buzzer_plays_short_long_and_double_patterns},
    {"W60", w60_web_buzzer_test_plays_requested_pattern},
    {"W61", w61_web_buzzer_test_rejected_while_active},
    {"W62", w62_local_buzzer_drive_matches_compile_flag},
    {"W63", w63_scale_priority_paddle_uses_scale_when_connected},
    {"W64", w64_buzzer_only_first_drop_uses_local_buzzer},
    {"W65", w65_scale_only_mutes_scale_lost_triple},
    {"W66", w66_web_bookoo_debug_dispatches_actions},
    {"W67", w67_web_bookoo_debug_rejected_while_active},
    {"W68", w68_web_bookoo_debug_rejected_when_disconnected},
    {"W69", w69_web_bookoo_debug_rejected_when_not_generic},
    {"W70", w70_bookoo_connect_mutes_in_buzzer_only},
    {"W71", w71_bookoo_connect_sets_volume_in_scale_priority},
    {"W72", w72_bookoo_connect_skips_disabled_volume_and_non_bookoo},
    {"W73", w73_apply_config_buzzer_only_sends_bookoo_silence},
    {"W74", w74_apply_config_enabling_mute_sends_silence_only_in_buzzer_only},
    {"W75", w75_bookoo_discovery_connect_applies_beep_policy},
    {"W76", w76_buzzer_only_start_beeps_at_cn9_not_ble_result},
    {"W77", w77_scale_priority_disconnected_beeps_on_cn9_without_ble},
    {"W78", w78_scale_priority_connected_start_does_not_use_buzzer},
    {"W79", w79_buzzer_only_stop_beeps_before_timer_stop_result},
    {"W80", w80_buzzer_only_retare_beeps_before_tare_result},
    {"W81", w81_scale_priority_failed_start_falls_back_after_disconnect},
    {"W82", w82_pulse_train_loops_until_deadline_or_stopIf},
    {"W83", w83_web_buzzer_test_pulse_uses_same_train_for_3s},
    {"W84", w84_pulse_train_yields_to_triple},
    {"W85", w85_debug_pulse_rates_use_same_on_ms_and_3s},
    {"W86", w86_config_applies_to_ram_immediately_and_coalesces},
    {"W87", w87_nvs_fail_keeps_ram_and_requeues},
    {"W88", w88_save_network_flush_includes_live_runtime},
    {"W89", w89_restart_flush_includes_live_and_aborts_on_fail},
    {"D01", d01_idle_scan_stays_enabled_between_ticks},
    {"D02", d02_full_empty_mac_uses_name_scan},
    {"D03", d03_scan_start_failed_uses_backoff},
    {"D04", d04_full_cache_keeps_directed_scan},
    {"D05", d05_hci_watchdog_force_restarts_same_filter},
    {"D06", d06_forget_pauses_discovery_for_30s},
    {"S01", s01_shot_log_filters_short_and_rinse},
    {"S02", s02_shot_log_appends_after_drip_delay},
    {"S03", s03_shot_log_clear_empties_records},
    {"S14", s14_last_shot_persists_every_cycle},
    {"S15", s15_last_shot_updates_weight_after_drip},
    {"S16", s16_last_shot_clear_empties_snapshot},
    {"S04", s04_shot_log_remove_by_id},
    {"S05", s05_shot_log_migrates_schema_v2},
    {"S06", s06_shot_log_local_sec_from_utc},
    {"S07", s07_shot_log_stores_fixed_wall_time},
    {"S08", s08_shot_log_without_sync_has_no_wall_time},
    {"S09", s09_shot_log_migrates_schema_v3},
    {"S10", s10_shot_log_migrates_schema_v4},
    {"S11", s11_shot_log_record_stays_v5_size},
    {"S12", s12_shot_log_migrates_schema_v5},
    {"S13", s13_persist_debug_messages_identify_origin},
    {"H01", h01_health_threshold_alerts_fire_once_per_crossing},
    {"N01", n01_wall_clock_tracks_utc_from_anchor},
    {"N02", n02_ntp_hostname_validation},
    {"N03", n03_unsynced_retry_is_one_minute},
    {"N04", n04_brew_start_requests_ntp_when_unsynced},
    {"N05", n05_rinse_start_requests_ntp_when_unsynced},
    {"N06", n06_synced_clock_skips_activity_ntp_request},
    {"N07", n07_syncing_clock_skips_activity_ntp_request},
    {"N08", n08_web_rinse_requests_ntp_when_unsynced},
    {"N09", n09_network_bringup_ignores_paddle},
    {"SC01", sc01_hello_replies_how_are_you},
    {"SC02", sc02_factory_reset_rejected_while_active},
    {"SC03", sc03_set_wifi_queues_save_network},
    {"SC04", sc04_clear_shots_empties_log},
    {"SC05", sc05_serial_cli_parser_covers_supported_commands},
    {"SC06", sc06_serial_cli_feed_completes_on_crlf},
    {"SC07", sc07_reset_ap_password_and_clear_wifi_queue},
    {"SC08", sc08_set_ap_password_queues_change},
    {"SC09", sc09_serial_debug_toggles_without_ready},
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
