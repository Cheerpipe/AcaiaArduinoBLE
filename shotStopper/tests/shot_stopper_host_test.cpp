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
#include <limits>
#include <string>

#include "../shotStopper.cpp"

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

void resetHarness(bool initialPaddleOn, bool scaleConnected) {
  deleteHostResources();

  Serial.reset();
  resetSerialCliState();

  hostMillis = 0;
  bootStartedAtMs = 0;
  usbSerialEnableSource = UsbSerialEnableSource::OFF;
  hostPinLevel.fill(HIGH);
  hostPinMode.fill(0);
  hostTrackedRelayPin = RELAY_GPIO;
  hostTrackedRelayOpenLevel = RELAY_OPEN_LEVEL;
  hostTrackedRelayClosedLevel = RELAY_CLOSED_LEVEL;
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
  hostLedcAttachCalls = 0;
  hostLedcLastFreq = 0;
  hostEspTimerCreateSucceeds = true;
  hostEspTimerStartSucceeds = true;
  hostGptimerCreateSucceeds = true;
  hostGptimerArmSucceeds = true;
  hostCircuitArmBeforeCommitHook = nullptr;
  hostTaskWatchdogOperationsSucceed = true;
  hostTaskWatchdogConfigured = false;
  hostTaskWatchdogSubscriptions = 0;
  hostTaskWatchdogFeeds = 0;
  hostTaskYieldCalls = 0;
  taskWatchdogRestoreFailed = false;
  hostCpuFrequencySetSucceeds = true;
  EEPROM.beginSucceeds = true;
  BLE.beginSucceeds = true;
  resetSafetyResetGuardForHost();

  stopperState = StopperState::REQUIRES_OFF;
  shot = ShotTrajectory{};
  session = CycleSession{};
  resetCupPresence();
  pendingFinalize = PendingShotFinalize{};
  bullseyeTracker.clear();
  bullseyeMelodyConfig = BullseyeMelodyConfig{};
  stagedBullseyeMelodyConfig = BullseyeMelodyConfig{};
  stagedBullseyeRequestId = 0;
  pendingScaleTimerStop = PendingScaleTimerStop{};
  pendingBrewRfRestore = false;
  runtimeConfig = RuntimeConfig{};
  runtimeConfig.rinseEnabled = true;
  // Host scenarios cover scale-path alerts unless a test sets the channel.
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  runtimeConfig.autoRetare = false;
  runtimeConfig.bbwProtectionMs = 3000;
  runtimeConfig.fastExtractionGuardEnabled = false;
  runtimeConfig.slowExtractionGuardEnabled = false;
  runtimeConfig.noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
  runtimeConfig.avoidAccidentalTouchEnabled = false;
  // Host scenarios assert immediate scale timer stop when the display is
  // already at/past circuit whole seconds; catch-up is covered by ST02–ST06.
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  lastCycle = LastCycleSummary{};
  persistedLastShot = PersistedLastShot{};
  lastShotNvsDirty = false;
  shotLogPersistFailLatched = false;
  shotCurvePersistFailLatched = false;
  lastShotPersistFailLatched = false;
  shotStorePersistRetryAtMs = 0;
  lastShotStore.clear();
  g_hostFlashIoMutexAvailable = true;
  shotCurveSampler.reset(0);
  lastShotCurve = emptyShotCurveRecord();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  noScaleShotGuardArmed = true;
  noScaleShotGuardActivityAtMs = 0;
  noScaleShotGuardScaleWasAvailable = false;
  noScaleShotGuardHold = false;
  noScaleShotGuardHoldAtMs = 0;
  noScaleShotGuardNeedsFreshActivator = false;
  resetNoScaleRequireBypassGesture();
  noScaleRequireBypassCompletedThisLoop = false;
  cupStartGuardHold = false;
  cupStartGuardHoldAtMs = 0;
  debugLog.clear();
  lastReportedLogOverwritten = 0;
  serialLogLevel = LogLevel::NONE;
  ringRetainLogLevel = LogLevel::INFO;
  publishedControlStatus = ControlStatusSnapshot{};
  taskProfiler.resetForHost();
  controlStatusSeq = 0;
  publishedControlGate = ControlGateSnapshot{};
  controlGateSeq = 0;
  controlStatusPublishRequested = false;
  bleCompanionRuntimeSnapshot = BleCompanionRuntimeSnapshot{};
  bleCompanionRuntimeSnapshot.enabled = true;
  bleCompanionStatusSnapshot = BleCompanionStatusSnapshot{};
  bleCompanionStatusSnapshot.enabled = true;
  bleCompanionStatusSnapshot.configuredEnabled = true;
  maintenanceLease = MaintenanceLease{};
  plannedRestartHeld = false;
  pendingPlannedRestart = WebCommand{};
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
  scale = EspressoScaleBLE(DEBUG);
  scale.connected = scaleConnected;
  resetScaleWorkerRadioStateForHost();
  scalePreferredMac[0] = '\0';
  scalePreferredName[0] = '\0';
  scalePreferredMacDirty = false;
  scaleDiscoveryPausedUntilMs = 0;
  scalePreferredDirectedResetGeneration = 0;
  scaleLinkState = ScaleLinkState::DISCONNECTED;
  scaleConnecting = false;
  scaleDisconnectSequence = 0;
  scaleConnectionGeneration = 0;
  scalePacketSequence = 0;
  scalePacketGaps = 0;
  lastScalePacketGapLogMs = 0;
  lastScaleWeightAtMs = 0;
  scaleWeightUpdateIntervalMs = 0;
  scaleRejectedPackets = 0;
  scaleReconnects = 0;
  scaleRecoveredStaleCount = 0;
  scaleRecoveredStaleMs = 0;
  recoverableStaleOpen = false;
  recoverableStaleStartedAtMs = 0;
  recoverableStaleDisconnectSequence = 0;
  recoverableStaleConnectionGeneration = 0;
  telemetryWeightStreamState = WeightStreamState::NO_SAMPLE;
  scaleLastDisconnectReason = 0;
  scaleLinkRssiValid = false;
  scaleLinkRssi = 0;
  lastScaleLinkRssiSampleMs = 0;
  scaleTimerValid = false;
  scaleTimerMs = 0;
  scaleTimerAgeMs = 0;
  scaleLinkFeatures = scaleFeatureSetNone();
  copyCString(scaleProtocolName, sizeof(scaleProtocolName), "none");
  scaleWorkerProgressAtMs = hostMillis;
  scaleEventsDropped = 0;
  scaleWorkerStackMinWords = 0;
  freeHeapBytes = 0;
  minimumFreeHeapBytes = 0;
  largestFreeHeapBlockBytes = 0;
  loopStackMinWords = 0;
  loopMaxGapMs = 0;
  loopIntervalGapMs = 0;
  healthIntervalMaxGapMs = 0;
  healthHeapAlertLatched = false;
  healthHeapRestartLatched = false;
  healthHeapLowSinceMs = 0;
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
#if SHOT_STOPPER_MACHINE_TYPE == 0
  paddleReturnReminderActive = false;
  paddleReturnReminderLastAtMs = 0;
  paddleReturnReminderStartedAtMs = 0;
#endif
  scaleCompletionBeepPending = false;
  scaleCompletionBeepScheduled = false;
  hostAutoScaleWorkerProgress = true;
  updateWorkerLinkState();

  rawActivatorOn = false;
  activatorOn = false;
  activatorTurnedOn = false;
  activatorTurnedOff = false;
  rawActivatorChangedAtMs = 0;
  virtualHoldOn = false;
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
  feedbackTransitionPending = false;
  feedbackExpectedClosed = false;
  feedbackTransitionStartedAtMs = 0;
  feedbackTransitionStampPending = false;
  safetyHeartbeatLevel = false;
  safetyHeartbeatToggledAtMs = 0;
  safeRestartRequested = false;
  platformClockReady = true;
  persistenceReady = true;
  bleStackReady = true;
  bootDegraded = false;
  bleCompanionResultDropped = 0;
  safetyResetStatus = SafetyResetSnapshot{};
  firmwareInitializationComplete = true;
  scaleConnectedLedInitialized = false;
  lastScaleConnectedLedOn = false;
  lastScaleConnectedLedPattern = ScaleConnectedLedPattern::OFF;
  lastScaleConnectedLedToggleAtMs = 0;

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
  hostPinLevel[ACTIVATOR_GPIO] = initialPaddleOn ? ACTIVATOR_ACTIVE_LEVEL
                                              : !ACTIVATOR_ACTIVE_LEVEL;
  initializeActivatorInput();
#if SHOT_STOPPER_MACHINE_TYPE != 0
  machineOnActivatorReady();
#endif
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
  runtimeConfig.requireCupToStart = false;
  mutableActiveShotPreset(presetBank).requireCupToStart = false;
  publishRecipeState();
}

void verifySafetyInvariants() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const bool stateMayCloseRelay =
      stopperState == StopperState::BREW ||
      stopperState == StopperState::RINSE ||
      stopperState == StopperState::MANUAL_NO_SCALE;

  if (relay.closed && (!stateMayCloseRelay || !session.active)) {
    std::cerr << "Safety invariant failed: machine circuit closed in "
              << stopperStateName(stopperState) << "\n";
    ++failures;
  }
  if ((stopperState == StopperState::READY ||
       stopperState == StopperState::REQUIRES_OFF) &&
      relay.closed) {
    std::cerr << "Safety invariant failed: safe state has machine circuit closed\n";
    ++failures;
  }
  if (!MACHINE_USES_MOMENTARY_SWITCH &&
      machineLastIntention().stablyOff &&
      stopperState != StopperState::RINSE &&
      session.source != ControlSource::WEB && relay.closed &&
      !machineHidesPhysicalStop()) {
    std::cerr << "Safety invariant failed: stable idle has machine circuit closed\n";
    ++failures;
  }
}

void stampHostObservedWeightFresh() {
  observedWeight = currentWeight;
  observedWeightReceivedAtMs = hostMillis;
  if (observedWeightSequence == 0) {
    observedWeightSequence = currentWeightSequence != 0 ? currentWeightSequence
                                                        : 1;
  }
  const uint32_t gen = getScaleLinkSnapshot().connectionGeneration;
  observedWeightConnectionGeneration = gen != 0 ? gen : 1;
}

void runLoopAfter(uint32_t deltaMs) {
  hostMillis += deltaMs;
  if (hostAutoScaleWorkerProgress && scale.connected) {
    markScaleWorkerProgress();
    if (session.active && session.automaticEnabled) {
      currentWeightReceivedAtMs = hostMillis;
      ++currentWeightSequence;
      session.receivedFreshWeightInCycle = true;
      stampHostObservedWeightFresh();
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
  hostPinLevel[ACTIVATOR_GPIO] = on ? ACTIVATOR_ACTIVE_LEVEL
                                : !ACTIVATOR_ACTIVE_LEVEL;
  if (hostAutoScaleWorkerProgress && scale.connected) {
    markScaleWorkerProgress();
  }
  loop();
  verifySafetyInvariants();
}

void setScaleConnected(bool connected) {
  scale.connected = connected;
  updateWorkerLinkState();
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
  const uint32_t startedAtMs = hostMillis;
  for (uint8_t index = 0; index < sampleCount; ++index) {
    publishWeight(weight + (index == 1 ? 0.1f : 0.0f),
                  startedAtMs + index * intervalMs, 1, baseSequence + index);
  }
  if (sampleCount > 0U) {
    hostMillis = startedAtMs + (sampleCount - 1U) * intervalMs;
  }
}

void seedCupPresence(float weight) {
  currentWeight = weight;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  publishStableCupWeight(weight, 1);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
}

void reachReadyFromBoot() {
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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

bool executeNextScaleCommand();

void advanceToBrew() {
  (void)executeNextScaleCommand();
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

ScaleCommand queuedCommandAt(size_t index) {
  ScaleCommand command;
  std::memcpy(&command, scaleCommandQueue->items.at(index).data(),
              sizeof(command));
  return command;
}

void publishScaleTimer(uint32_t timerMs) {
  scale.timerValid = true;
  scale.timerMs = timerMs;
  scaleTimerValid = true;
  scaleTimerMs = timerMs;
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

void t02_boot_with_activator_on() {
  resetHarness(true, true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS * 4);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  CHECK(executeNextScaleCommand());
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

void t04b_rinse_disabled_short_on_off_is_not_rinse() {
  resetHarness(false, true);
  runtimeConfig.rinseEnabled = false;
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  CHECK(executeNextScaleCommand());
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState != StopperState::RINSE);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t04c_rinse_demote_does_not_learn_offset() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const float originalOffset = runtimeConfig.weightOffsetG;
  const uint32_t rawOnAt = startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(shot.automaticBrew);
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(!shot.automaticBrew);
  CHECK(!session.calibrationEligible);
  runLoopAfter(runtimeConfig.rinseDurationMs);
  CHECK(!session.active);
  CHECK(!pendingFinalize.pending);
  currentWeight = static_cast<float>(DEFAULT_GOAL_WEIGHT_G) + 3.0f;
  currentWeightReceivedAtMs = hostMillis + 1;
  ++currentWeightSequence;
  runLoopAfter(runtimeConfig.dripDelayMs);
  CHECK(runtimeConfig.weightOffsetG == originalOffset);
}

void t29_rinse_demote_begin_fail_does_not_enter_rinse() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  const uint32_t rawOffAt = rawOnAt + runtimeConfig.rinseGestureMs;
  CHECK(hostMillis <= rawOffAt);
  runLoopAfter(rawOffAt - hostMillis);
  CHECK(stopperState == StopperState::BREW);
  CHECK(setMachineCircuitClosed(false));
  hostGptimerArmSucceeds = false;
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::RELAY_SAFETY_FAILURE);
  CHECK(stopperState != StopperState::RINSE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!rinseActuationActive);
}

void t05_release_between_rinse_and_brew_is_short_shot() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  CHECK(executeNextScaleCommand());
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
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
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(session.rinseStartedAtMs == rinseStarted);
}

void t09_rinse_ending_on_requires_off() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 500);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  runLoopAfter(runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs));
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t10_paddle_bounce_does_not_start_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS - 1);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
  CHECK(!getRelaySafetySnapshot().closed);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS);
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
  circuitClosed = false;
  relaySafetyTripped = false;
  stopperState = StopperState::REQUIRES_OFF;
  hostRelayOpenWrites = 0;
  setup();
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
  CHECK(hostRelayOpenWrites >= 1);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::OPEN);
  CHECK(getRelaySafetySnapshot().fault == RelaySafetyFault::NONE);
  CHECK(!getRelaySafetySnapshot().resetRecoveryRequired);
}

void t14_automatic_stop_stays_open_while_activator_on() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  runLoopAfter(HARD_MAX_CIRCUIT_CLOSED_MS * 2);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void t15_repeated_rinse_and_brew_reset_session_state() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t firstCycle = startCycle();
  const uint32_t firstId = session.id;
  CHECK(executeNextScaleCommand());
  releaseAtPhysicalDuration(firstCycle, 500);
  runLoopAfter(runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs));
  CHECK(stopperState == StopperState::READY);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
  CHECK(executeNextScaleCommand());

  startCycle();
  CHECK(session.id != firstId);
  CHECK(!session.stopTimerRequested);
  CHECK(session.timerStopResult == TimerStopResult::NOT_REQUIRED);
  CHECK(session.endReason == EndReason::NONE);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(scale.tareStartTimerCalls == 2);
}

void t16_only_micra_states_are_compiled() {
  CHECK(StopperState::REQUIRES_OFF != StopperState::READY);
  CHECK(ACTIVATOR_ACTIVE_LEVEL == LOW);
  CHECK(RELAY_OPEN_LEVEL != RELAY_CLOSED_LEVEL);
}

void t17_simultaneous_global_limit_and_paddle_off_is_idempotent() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.operationalWallMs = HARD_MAX_CIRCUIT_CLOSED_MS;
  startCycle();
  advanceToBrew();
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS - ACTIVATOR_DEBOUNCE_MS);
  setRawPaddle(false);
  hostRelayOpenWrites = 0;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
  CHECK(hostRelayOpenWrites == 1);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
}

void t18_rinse_and_short_shot_each_request_one_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  uint32_t rawOnAt = startCycle();
  CHECK(executeNextScaleCommand());
  releaseAtPhysicalDuration(rawOnAt, 500);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
  runLoopAfter(runtimeConfig.rinseDurationMs);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  resetHarness(false, true);
  reachReadyFromBoot();
  rawOnAt = startCycle();
  CHECK(executeNextScaleCommand());
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 100);
  CHECK(session.endReason == EndReason::ACTIVATOR);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
}

void t24_paddle_off_during_brew_is_immediate() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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

  releaseAtPhysicalDuration(rawActivatorChangedAtMs,
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(pendingFinalize.dripDelayMs);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  CHECK(scale.resetTimerCalls == 1);
  CHECK(scale.startTimerCalls == 1);
  CHECK(scale.tareCalls == 1);
  loop();
  CHECK(session.remoteTimerStarted);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  advanceToBrew();
  scale.stopTimerSucceeds = false;
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(executeNextScaleCommand());
  CHECK(scale.stopTimerCalls == 1);
  CHECK(session.timerStopResult == TimerStopResult::WRITE_FAILED);
}

void r05_regression_uses_last_ten_valid_samples() {
  float times[WEIGHT_TREND_POINT_COUNT];
  float weights[WEIGHT_TREND_POINT_COUNT];
  for (size_t i = 0; i < WEIGHT_TREND_POINT_COUNT; ++i) {
    times[i] = static_cast<float>(i + 1);
    weights[i] = static_cast<float>(i + 1) * 2.0f;
  }
  CHECK(fabsf(predictedWeightStopTimeS(times, weights, WEIGHT_TREND_POINT_COUNT,
                                       34.5f, 60.0f) -
              17.25f) < 0.001f);
  CHECK(fabsf(predictedWeightStopTimeS(times, weights, WEIGHT_TREND_POINT_COUNT,
                                       41.0f, 60.0f) -
              20.5f) < 0.001f);

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
  CHECK(shot.expectedEndS == session.config.operationalWallMs / 1000.0f);

  // Intercept already above target with a positive slope predicts a time in
  // the past of the sample window; fall back to the operational wall.
  resetShotTrajectory(session.startedAtMs);
  for (size_t i = 0; i < TREND_POINT_COUNT; ++i) {
    recordWeightSample(40.0f + static_cast<float>(i) * 0.1f,
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  CHECK(shot.expectedEndS == session.config.operationalWallMs / 1000.0f);

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  startCycle();
  session.extractionExtended = true;
  resetShotTrajectory(session.startedAtMs);
  for (size_t i = 1; i <= TREND_POINT_COUNT; ++i) {
    recordWeightSample(static_cast<float>(i) * 2.0f,
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  CHECK(fabsf(shot.expectedEndS - 20.5f) < 0.001f);

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  startCycle();
  session.slowExtractionExtended = true;
  resetShotTrajectory(session.startedAtMs);
  for (size_t i = 1; i <= TREND_POINT_COUNT; ++i) {
    recordWeightSample(static_cast<float>(i) * 2.0f,
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  const float expectedSlowEndS =
      (session.config.minRecoveryWeightG - session.config.weightOffsetG) /
      2.0f;
  CHECK(fabsf(shot.expectedEndS - expectedSlowEndS) < 0.001f);

  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  resetShotTrajectory(session.startedAtMs);
  session.receivedFreshWeightInCycle = false;
  for (size_t i = 1; i <= MAX_SHOT_DATAPOINTS + 8; ++i) {
    recordWeightSample(static_cast<float>(i) * 0.5f,
                       shot.startMs + static_cast<uint32_t>(i * 1000));
  }
  CHECK(shot.datapoints == MAX_SHOT_DATAPOINTS);
  CHECK(shot.weight[MAX_SHOT_DATAPOINTS - 1U] ==
        static_cast<float>(MAX_SHOT_DATAPOINTS + 8U) * 0.5f);
  CHECK(shot.timeS[0] == static_cast<float>(9));
}

void r06_hard_timer_opens_circuit_without_control_loop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);

  hostMillis = circuitClosedAtMs + HARD_MAX_CIRCUIT_CLOSED_MS;
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
  rawActivatorChangedAtMs = hostMillis;
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  runtimeConfig.operationalWallMs = HARD_MAX_CIRCUIT_CLOSED_MS;
  startCycle();
  reachManualNoScaleState();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::RELAY_SAFETY_FAILURE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == 0);

  resetHarness(false, false);
  reachReadyFromBoot();
  hostEspTimerStartSucceeds = false;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(pendingFinalize.dripDelayMs);
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
  runLoopAfter(pendingFinalize.dripDelayMs);
  CHECK(runtimeConfig.weightOffsetG == validOffset);

  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = true;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = 3.0f;
  currentWeight = 32.0f;
  ++currentWeightSequence;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(pendingFinalize.dripDelayMs);
  finishHostMaintenance();
  CHECK(fabsf(runtimeConfig.weightOffsetG) < 0.001f);
}

void r58_extended_shot_does_not_learn_weight_offset() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const float originalOffset = runtimeConfig.weightOffsetG;
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = true;
  pendingFinalize.extractionExtended = true;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 0;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = originalOffset;
  currentWeight = DEFAULT_GOAL_WEIGHT_G + 3.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(pendingFinalize.dripDelayMs);
  finishHostMaintenance();
  CHECK(runtimeConfig.weightOffsetG == originalOffset);
}

void r65_slow_extended_shot_does_not_learn_weight_offset() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const float originalOffset = runtimeConfig.weightOffsetG;
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = true;
  pendingFinalize.slowExtractionExtended = true;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 0;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = originalOffset;
  currentWeight = DEFAULT_GOAL_WEIGHT_G - 6.0f;
  currentWeightSequence = 1;
  currentWeightReceivedAtMs = hostMillis + 1;
  runLoopAfter(pendingFinalize.dripDelayMs);
  finishHostMaintenance();
  CHECK(runtimeConfig.weightOffsetG == originalOffset);
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

void r12b_discovery_clears_stale_connected_link_snapshot() {
  resetHarness(false, true);
  setScaleLinkState(ScaleLinkState::CONNECTED);
  markScaleWorkerProgress();
  CHECK(getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED);
  CHECK(scaleAvailable());

  // Library already dropped (e.g. beep path called isConnected()), but the
  // snapshot was left CONNECTED. Discovery must clear it immediately even
  // while an indefinite idle scan is in progress.
  scale.connected = false;
  scale.scanning = true;
  uint32_t lastScanCycleMs = hostMillis;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = hostMillis;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(getScaleLinkSnapshot().state == ScaleLinkState::DISCONNECTED);
  CHECK(!scaleAvailable());
}

void r12c_connected_link_rssi_samples_and_clears() {
  resetHarness(false, true);
  scale.linkRssiValue = -64;
  serviceScaleLinkRssi(1000);
  ScaleLinkSnapshot snap = getScaleLinkSnapshot();
  CHECK(snap.rssiValid);
  CHECK(snap.rssi == -64);
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.scaleRssiValid);
  CHECK(status.scaleRssi == -64);

  scale.linkRssiValue = -40;
  serviceScaleLinkRssi(1500);
  snap = getScaleLinkSnapshot();
  CHECK(snap.rssiValid);
  CHECK(snap.rssi == -64);

  serviceScaleLinkRssi(2000);
  snap = getScaleLinkSnapshot();
  CHECK(snap.rssi == -40);

  scale.linkRssiValue = SCALE_LINK_RSSI_UNAVAILABLE;
  serviceScaleLinkRssi(3000);
  snap = getScaleLinkSnapshot();
  CHECK(!snap.rssiValid);

  scale.linkRssiValue = -70;
  serviceScaleLinkRssi(4000);
  setScaleConnected(false);
  snap = getScaleLinkSnapshot();
  CHECK(!snap.rssiValid);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(!status.scaleRssiValid);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  update.config.operationalWallMs = HARD_MAX_CIRCUIT_CLOSED_MS + 1;
  CHECK(validateRuntimeConfig(update.config) ==
        ConfigValidationError::OPERATIONAL_WALL);
  CHECK(enqueueWebCommand(update));
  loop();
  CHECK(runtimeConfig.operationalWallMs == original.operationalWallMs);
}

void r15_gptimer_opens_circuit_without_arduino_or_esp_timer_tasks() {
  resetHarness(false, false);
  CHECK(setMachineCircuitClosed(true, 5000));
  hostMillis += 5000;
  independentSafetyTimer.serviceForHost();

  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.operationalTripped);
  CHECK(relay.state == RelaySafetyState::TRIPPED);
  CHECK(relay.fault == RelaySafetyFault::OPERATIONAL_LIMIT);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

void r16_timeout_during_arm_transaction_can_never_close_circuit() {
  resetHarness(false, false);
  hostCircuitArmBeforeCommitHook = []() {
    ++hostMillis;
    independentSafetyTimer.serviceForHost();
  };

  CHECK(!setMachineCircuitClosed(true, 1));
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

  CHECK(!setMachineCircuitClosed(true, 5000));
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::TIMER_ARM_FAILED);
  CHECK(hostRelayClosedWrites == 0);
}

void r18_watchdog_fault_opens_circuit_and_requests_safe_restart() {
  resetHarness(false, false);
  CHECK(setMachineCircuitClosed(true, 5000));

  reportTaskWatchdogFault();
  serviceRelaySafety();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::TASK_WATCHDOG_FAILURE);
  CHECK(safeRestartRequested);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

void r18b_ota_watchdog_restore_failure_requests_safe_restart() {
  resetHarness(false, false);
  CHECK(setMachineCircuitClosed(true, 5000));
  hostTaskWatchdogOperationsSucceed = true;
  {
    TaskWatchdogOtaWindow window;
    CHECK(window.widened());
    hostTaskWatchdogOperationsSucceed = false;
  }
  CHECK(taskWatchdogRestoreFailed);
  runLoopAfter(0);
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.fault == RelaySafetyFault::TASK_WATCHDOG_FAILURE);
  CHECK(!taskWatchdogRestoreFailed);
}

void r19_reset_during_close_reopens_without_recovery_lockout() {
  resetHarness(false, false);
  recordRelayCommandedClosed(true);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  safetyResetStatus = beginSafetyResetGuard();
  initializeRelaySafetyStateAfterBoot();

  CHECK(safetyResetStatus.resetDuringClose);
  CHECK(safetyResetStatus.unsafeReset);
  CHECK(!safetyResetStatus.recoveryRequired);
  CHECK(relaySafetyState == RelaySafetyState::OPEN);
  CHECK(relaySafetyFault == RelaySafetyFault::NONE);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
  CHECK(safetyResetRecord.relayMarker == SAFETY_RELAY_OPEN_MARKER);
}

void r19b_panic_boot_is_ready_for_webui_and_next_circuit_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  hostSafetyResetReasonCode = 4;
  hostSafetyResetReasonUnsafe = true;
  recordRelayCommandedClosed(true);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  safetyResetStatus = beginSafetyResetGuard();
  initializeRelaySafetyStateAfterBoot();

  CHECK(safetyResetStatus.unsafeReset);
  CHECK(!safetyResetStatus.recoveryRequired);
  CHECK(relaySafetyState == RelaySafetyState::OPEN);
  CHECK(stopperState == StopperState::READY);
  CHECK(controlAllowsConfigurationNow());

  CHECK(setMachineCircuitClosed(true, runtimeConfig.operationalWallMs));
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(setMachineCircuitClosed(false));
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
  CHECK(!reset.recoveryRequired);
}

void r20b_reset_history_keeps_reason_and_previous_uptime() {
  resetHarness(false, false);
  SafetyResetSnapshot reset = beginSafetyResetGuard();
  CHECK(reset.resetHistoryCount == 1);
  CHECK(reset.resetHistory[0].reasonCode == 1);
  CHECK(reset.resetHistory[0].uptimeMs == 0);

  recordResetUptime(123456);
  hostSafetyResetReasonCode = 4;
  hostSafetyResetReasonUnsafe = true;
  reset = beginSafetyResetGuard();
  CHECK(reset.resetHistoryCount == 2);
  CHECK(reset.resetHistory[0].reasonCode == 4);
  CHECK(reset.resetHistory[0].uptimeMs == 123456);
  CHECK(reset.resetHistory[1].reasonCode == 1);
}

void r20c_reset_uptime_checkpoint_is_no_more_frequent_than_one_minute() {
  resetHarness(false, false);
  (void)beginSafetyResetGuard();
  recordResetUptime(59999);
  CHECK(safetyResetRecord.currentUptimeMs == 0);
  recordResetUptime(60000);
  CHECK(safetyResetRecord.currentUptimeMs == 60000);
  recordResetUptime(119999);
  CHECK(safetyResetRecord.currentUptimeMs == 60000);
  recordResetUptime(120000);
  CHECK(safetyResetRecord.currentUptimeMs == 120000);
}

void r20d_clear_reset_history_keeps_current_reset_reason() {
  resetHarness(false, false);
  SafetyResetSnapshot reset = beginSafetyResetGuard();
  const uint32_t reason = reset.reasonCode;
  CHECK(reset.resetHistoryCount == 1);
  CHECK(clearPersistedResetHistory(reset.unsafeResetCount));
  CHECK(safetyResetRecordValid());
  CHECK(safetyResetRecord.historyCount == 0);
  CHECK(reason == hostSafetyResetReasonCode);
}

void w01_default_runtime_configuration_is_valid() {
  const RuntimeConfig config;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  CHECK(config.operationalWallMs == DEFAULT_OPERATIONAL_WALL_MS);
  CHECK(config.rinseGestureMs == 1000);
  CHECK(!config.rinseEnabled);
  CHECK(config.minBbwBrewTimeMs == 28000);
  CHECK(config.canTareStartTimer);
  CHECK(config.postTareBaselineGraceMs == DEFAULT_POST_TARE_BASELINE_GRACE_MS);
  CHECK(config.scaleTimerStopExtraDelayMs ==
        DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS);
  CHECK(config.firstDropBeep);
  CHECK(config.paddleReturnReminderBeep);
  CHECK(!config.soundAlertsMuted);
  CHECK(config.buzzerScaleLostBeep);
  CHECK(config.buzzerAutoToManualGuardEndBeep);
  CHECK(config.buzzerManualNoScaleBeep);
  CHECK(config.buzzerScaleConnectedBeep);
  CHECK(config.scaleConnectedLed);
  CHECK(config.buzzerExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(config.buzzerSlowExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(DEFAULT_EXTENDED_PULSE_RATE == ExtendedPulseRate::FAST);
  CHECK(buzzerPatternForExtendedPulseRate(config.buzzerExtendedPulseRate) ==
        BuzzerPattern::PULSE_4HZ);
  CHECK(buzzerPatternForExtendedPulseRate(config.buzzerSlowExtendedPulseRate) ==
        BuzzerPattern::PULSE_4HZ);
  CHECK(config.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(DEFAULT_ALERT_OUTPUT_CHANNEL ==
        (BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY
                                : AlertOutputChannel::SCALE_ONLY));
  CHECK(config.bookooMuteOnBuzzerOnly);
  CHECK(config.bookooConnectBeepLevel == DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(config.fastExtractionGuardEnabled);
  CHECK(config.slowExtractionGuardEnabled);
  CHECK(fabsf(config.minRecoveryWeightG - DEFAULT_MIN_RECOVERY_WEIGHT_G) <
        0.001f);
  CHECK(config.maxBbwBrewTimeMs == DEFAULT_MAX_BBW_BREW_TIME_MS);
  CHECK(config.noScaleBbwMode ==
        static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE));
  CHECK(config.cupProtectionEnabled);
  CHECK(config.stopIfCupRemoved);
  CHECK(!config.requireCupToStart);
  CHECK(config.avoidAccidentalTouchEnabled);
  CHECK(fabsf(config.cupPresentWeightG - DEFAULT_CUP_PRESENT_WEIGHT_G) < 0.001f);
  CHECK(fabsf(config.cupRemovedWeightG - DEFAULT_CUP_REMOVED_WEIGHT_G) < 0.001f);
  CHECK(config.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
  CHECK(config.dripDelayMs == DEFAULT_DRIP_DELAY_MS);
  CHECK(!config.serialDebugOutput);
  CHECK(config.ringRetainLogLevel == static_cast<uint8_t>(LogLevel::NONE));
  CHECK(config.paddleMode == static_cast<uint8_t>(PaddleMode::NATURAL));
  CHECK(runtimeStopPulseMs(config) == COMPILED_STOP_PULSE_MS);
  CHECK(runtimeMaxSinglePressMs(config) == COMPILED_MAX_SINGLE_PRESS_MS);
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
  config = RuntimeConfig{};
  config.fastExtractionGuardEnabled = false;
  config.slowExtractionGuardEnabled = true;
  config.minRecoveryWeightG = 36.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION);
  config = RuntimeConfig{};
  config.fastExtractionGuardEnabled = true;
  config.slowExtractionGuardEnabled = true;
  config.maxBbwBrewTimeMs = config.minBbwBrewTimeMs;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION);
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
  config.postTareBaselineGraceMs = MIN_POST_TARE_BASELINE_GRACE_MS - 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::POST_TARE_BASELINE_GRACE);
  config = RuntimeConfig{};
  config.postTareBaselineGraceMs = MAX_POST_TARE_BASELINE_GRACE_MS + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::POST_TARE_BASELINE_GRACE);
  config = RuntimeConfig{};
  config.postTareBaselineGraceMs = MIN_POST_TARE_BASELINE_GRACE_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.postTareBaselineGraceMs = MAX_POST_TARE_BASELINE_GRACE_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
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
  config.buzzerSlowExtendedPulseRate = 9;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTENDED_PULSE_RATE);
  config = RuntimeConfig{};
  config.noScaleBbwMode = 3;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::NO_SCALE_BBW_MODE);
  config = RuntimeConfig{};
  config.lastShotCooldownMs = MIN_LAST_SHOT_COOLDOWN_MS - 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::LAST_SHOT_COOLDOWN);
  config = RuntimeConfig{};
  config.lastShotCooldownMs = MAX_LAST_SHOT_COOLDOWN_MS + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::LAST_SHOT_COOLDOWN);
  config = RuntimeConfig{};
  config.dripDelayMs = MIN_DRIP_DELAY_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.dripDelayMs = MAX_DRIP_DELAY_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.dripDelayMs = MAX_DRIP_DELAY_MS + 1;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::DRIP_DELAY);
  config = RuntimeConfig{};
  config.ringRetainLogLevel = static_cast<uint8_t>(LogLevel::NONE) + 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::RING_RETAIN_LOG_LEVEL);
  uint8_t parsedLevel = 255;
  CHECK(parseLogLevel("info", parsedLevel));
  CHECK(parsedLevel == static_cast<uint8_t>(LogLevel::INFO));
  CHECK(parseLogLevel("none", parsedLevel));
  CHECK(parsedLevel == static_cast<uint8_t>(LogLevel::NONE));
  CHECK(!parseLogLevel("verbose", parsedLevel));
  config = RuntimeConfig{};
  config.paddleMode = 9;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::PADDLE_MODE);
  uint8_t parsedPaddle = 255;
  CHECK(parsePaddleMode("natural", parsedPaddle));
  CHECK(parsedPaddle == static_cast<uint8_t>(PaddleMode::NATURAL));
  CHECK(parsePaddleMode("original", parsedPaddle));
  CHECK(parsedPaddle == static_cast<uint8_t>(PaddleMode::ORIGINAL));
  CHECK(parsePaddleMode("auto", parsedPaddle));
  CHECK(parsedPaddle == static_cast<uint8_t>(PaddleMode::AUTO));
  CHECK(!parsePaddleMode("legacy", parsedPaddle));
  config = RuntimeConfig{};
  config.stopPulseTenMs = 4;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::STOP_PULSE);
  config = RuntimeConfig{};
  config.maxSinglePressHundredMs = 51;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::MAX_SINGLE_PRESS);
  config = RuntimeConfig{};
  config.stopPulseTenMs = 0;
  config.maxSinglePressHundredMs = 0;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  CHECK(runtimeStopPulseMs(config) == COMPILED_STOP_PULSE_MS);
  CHECK(runtimeMaxSinglePressMs(config) == COMPILED_MAX_SINGLE_PRESS_MS);
  config = RuntimeConfig{};
  config.reedConfirmTimeoutHundredMs = 1;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::REED_CONFIRM_TIMEOUT);
  config.reedConfirmTimeoutHundredMs = 51;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::REED_CONFIRM_TIMEOUT);
  config.reedConfirmTimeoutHundredMs = 0;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  CHECK(runtimeReedConfirmTimeoutMs(config) ==
        COMPILED_REED_CONFIRM_TIMEOUT_MS);
  config = RuntimeConfig{};
  config.shotReactTimeoutS = 2;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SHOT_REACT_TIMEOUT);
  config.shotReactTimeoutS = 31;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SHOT_REACT_TIMEOUT);
  config.shotReactTimeoutS = 0;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  CHECK(runtimeShotReactTimeoutMs(config) == COMPILED_SHOT_REACT_TIMEOUT_MS);
  config.shotReactTimeoutS = 12;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  CHECK(runtimeShotReactTimeoutS(config) == 12);
  config = RuntimeConfig{};
  config.cupPresentWeightG = 0.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::CUP_PRESENT_WEIGHT);
  config = RuntimeConfig{};
  config.cupPresentWeightG = MIN_CUP_PRESENT_WEIGHT_G - 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::CUP_PRESENT_WEIGHT);
  config = RuntimeConfig{};
  config.cupPresentWeightG = MAX_CUP_PRESENT_WEIGHT_G + 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::CUP_PRESENT_WEIGHT);
  config = RuntimeConfig{};
  config.cupRemovedWeightG = 0.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::CUP_REMOVED_WEIGHT);
  config = RuntimeConfig{};
  config.cupRemovedWeightG = MAX_CUP_REMOVED_WEIGHT_G + 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::CUP_REMOVED_WEIGHT);
  config = RuntimeConfig{};
  config.cupRemovedWeightG = MIN_CUP_REMOVED_WEIGHT_G - 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::CUP_REMOVED_WEIGHT);
}

void w04_wifi_credentials_have_strict_bounds() {
  CHECK(validWifiSsid("Micra"));
  CHECK(!validWifiSsid(""));
  CHECK(validWifiPassword("12345678", false));
  CHECK(!validWifiPassword("1234", false));
  CHECK(validWifiPassword("", true));
  CHECK(validDevicePassword("ineedacoffee"));
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

void w04b_select_best_sta_ap_prefers_strongest_matching_bssid() {
  const uint8_t weakBssid[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  const uint8_t strongBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  const uint8_t otherBssid[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  const StaApScanEntry entries[] = {
      {"CafeLAN", weakBssid, 1, -80, false},
      {"OtherNet", strongBssid, 6, -40, false},
      {"CafeLAN", strongBssid, 11, -55, false},
      {"CafeLAN", otherBssid, 6, -50, true},
  };
  size_t bestIndex = 99;
  CHECK(selectBestStaAp(entries, 4, "CafeLAN", false, bestIndex));
  CHECK(bestIndex == 2);
  CHECK(entries[bestIndex].rssi == -55);
  CHECK(entries[bestIndex].bssid[0] == 0xAA);

  bestIndex = 99;
  CHECK(!selectBestStaAp(entries, 4, "Missing", false, bestIndex));
  CHECK(!selectBestStaAp(nullptr, 0, "CafeLAN", false, bestIndex));
  CHECK(selectBestStaAp(entries, 4, "CafeLAN", true, bestIndex));
  CHECK(bestIndex == 3);
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

void configureShortOperationalWallForManual() {
  runtimeConfig.operationalWallMs = 20000;
  runtimeConfig.bbwProtectionMs = 7000;
  runtimeConfig.autoToManualGuardManualLimitMs = 15000;
  runtimeConfig.autoToManualGuardBaselineMs = 15000;
  CHECK(validateRuntimeConfig(runtimeConfig) == ConfigValidationError::NONE);
}

void armTimerOnlySession() {
  runtimeConfig.timerOnly = true;
  ShotPreset &preset = mutableActiveShotPreset(presetBank);
  preset.brewByWeight = false;
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
}

void assertManualCycleSkipsOperationalWallUntilHardCap(
    StopperState expectedState) {
  CHECK(stopperState == expectedState);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().operationalLimitMs ==
        HARD_MAX_CIRCUIT_CLOSED_MS);
  hostMillis = circuitClosedAtMs + runtimeConfig.operationalWallMs;
  hostServiceEspTimer(operationalLimitTimer);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!getRelaySafetySnapshot().operationalTripped);
  CHECK(stopperState == expectedState);
  hostMillis = circuitClosedAtMs + HARD_MAX_CIRCUIT_CLOSED_MS;
  hostServiceEspTimer(relaySafetyTimer);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().tripped);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
}

void w11_operational_timer_opens_without_control_loop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  configureShortOperationalWallForManual();
  startCycle();
  assertManualCycleSkipsOperationalWallUntilHardCap(
      StopperState::MANUAL_NO_SCALE);
}

void w11b_timer_only_natural_skips_operational_wall() {
  resetHarness(false, true);
  armTimerOnlySession();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::NATURAL);
  reachReadyFromBoot();
  configureShortOperationalWallForManual();
  startCycle();
  assertManualCycleSkipsOperationalWallUntilHardCap(StopperState::BREW);
}

void w11c_timer_only_original_skips_operational_wall() {
  resetHarness(false, true);
  armTimerOnlySession();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  reachReadyFromBoot();
  configureShortOperationalWallForManual();
  startCycle();
  CHECK(!machineCycleHardMaxArmedForTest());
  assertManualCycleSkipsOperationalWallUntilHardCap(StopperState::BREW);
}

void w11d_timer_only_auto_skips_operational_wall() {
  resetHarness(false, true);
  armTimerOnlySession();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  reachReadyFromBoot();
  configureShortOperationalWallForManual();
  startCycle();
  assertManualCycleSkipsOperationalWallUntilHardCap(StopperState::BREW);
}

void w12_hard_limit_cannot_be_configured_above_sixty_seconds() {
  RuntimeConfig config;
  config.operationalWallMs = HARD_MAX_CIRCUIT_CLOSED_MS + 1;
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
  WebCommand on = webControlCommand(WebCommandType::REMOTE_ON);
  processWebCommand(on);
  CHECK(session.active);
  CHECK(session.source == ControlSource::WEB);
  CHECK(virtualHoldOn);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  WebCommand off = webControlCommand(WebCommandType::REMOTE_OFF);
  processWebCommand(off);
  CHECK(stopperState == StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w14_physical_motion_overrides_web_control() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::REMOTE_ON);
  processWebCommand(on);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  CHECK(executeNextScaleCommand());
  runLoopAfter(runtimeConfig.rinseDurationMs);
  CHECK(stopperState == StopperState::READY);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);

  processWebCommand(rinse);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::PHYSICAL_OVERRIDE);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
}

void w16_web_stop_during_rinse_preserves_rearm() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, 400);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  WebCommand stop;
  stop.type = WebCommandType::STOP;
  processWebCommand(stop);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::WEB_STOP);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w17_web_session_stop_is_a_safe_stop() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::REMOTE_ON);
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
  WebCommand on = webControlCommand(WebCommandType::REMOTE_ON);
  processWebCommand(on);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w20_restart_waits_while_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  const RelaySafetySnapshot before = getRelaySafetySnapshot();
  WebCommand restart;
  restart.type = WebCommandType::RESTART;
  restart.unsafeWebUiOverride = true;
  processWebCommand(restart);
  CHECK(session.active);
  CHECK(plannedRestartHeld);
  CHECK(!maintenanceLease.active);
  CHECK(getRelaySafetySnapshot().closed == before.closed);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);

  const uint32_t elapsed = elapsedMs(session.startedAtMs);
  if (elapsed <= runtimeConfig.rinseGestureMs) {
    runLoopAfter(runtimeConfig.rinseGestureMs - elapsed + 1);
  }
  CHECK(session.active);
  CHECK(plannedRestartHeld);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(!session.active);
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS + 1);
  CHECK(!plannedRestartHeld);
  CHECK(maintenanceLease.active ||
        hostLastForwardedNetworkCommand.type == WebCommandType::RESTART);
}

void w20b_planned_esp_restart_waits_for_shot() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  safeRestartRequested = true;
  loop();
  CHECK(session.active);
  CHECK(safeRestartRequested);
  CHECK(getRelaySafetySnapshot().closed);

  criticalTaskWatchdogFault = true;
  loop();
  CHECK(!safeRestartRequested);
  CHECK(!getRelaySafetySnapshot().closed);
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
  CHECK(ring.countAfter(0) == DEBUG_EVENT_CAPACITY);
  DebugEvent first = {};
  CHECK(ring.copyFirstAfter(0, first));
  CHECK(first.argument1 == 5);
  DebugEvent second = {};
  CHECK(ring.copyFirstAfter(first.sequence, second));
  CHECK(second.argument1 == 6);
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
  CHECK(status.machineRunning);
  CHECK(status.source == ControlSource::PHYSICAL);
  CHECK(status.config.revision == session.config.revision);
  ControlGateSnapshot gate;
  copyControlGate(gate);
  CHECK(gate.activeCycle);
  CHECK(gate.relayClosed);
  CHECK(gate.machineRunning);
  CHECK(gate.source == ControlSource::PHYSICAL);
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
  releaseAtPhysicalDuration(rawActivatorChangedAtMs,
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
  updateWorkerLinkState();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  simulateFirstDrops();
  CHECK(!scaleBeepPending);
  CHECK(!executePendingScaleBrewBeep());
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
  hostPinLevel[ACTIVATOR_GPIO] = ACTIVATOR_ACTIVE_LEVEL;
  CHECK(!rawActivatorOn);
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(status.physicalActivatorOn);
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

void w50e_buzzer_phase_timer_holds_double_as_truncated_triple() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::DOUBLE));
  CHECK(localBuzzer.beepCount == 2);
  CHECK(localBuzzer.onMs == BUZZER_BEEP_ON_MS);
  CHECK(localBuzzer.gapMs == BUZZER_BEEP_GAP_MS);
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

void w50d_recovery_buzzer_patterns_have_exact_timings() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::RECOVERY_LONG));
  CHECK(localBuzzer.beepCount == 1);
  CHECK(localBuzzer.onMs == 1500);
  hostMillis += 1499;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += 1;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(!localBuzzer.busy());

  CHECK(localBuzzer.request(BuzzerPattern::RECOVERY_NETWORK_OK));
  CHECK(localBuzzer.beepCount == 3);
  CHECK(localBuzzer.onMs == 50);
  CHECK(localBuzzer.gapMs == 50);
  for (uint8_t note = 0; note < 3; ++note) {
    hostMillis += 50;
    hostServiceEspTimer(localBuzzer.phaseTimer);
    if (note + 1U < 3U) {
      hostMillis += 50;
      hostServiceEspTimer(localBuzzer.phaseTimer);
    }
  }
  CHECK(!localBuzzer.busy());

  CHECK(localBuzzer.request(BuzzerPattern::RECOVERY_FACTORY_OK));
  CHECK(localBuzzer.beepCount == 5);
  CHECK(localBuzzer.onMs == 50);
  CHECK(localBuzzer.gapMs == 50);
}

void checkLocalCompletionTone();

void w51_local_buzzer_echo_inverted_on_scale_lost_during_bbw() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  CHECK(session.automaticEnabled ||
        session.weightControlState == WeightControlState::ACTIVE ||
        session.weightControlState == WeightControlState::VALIDATING);
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_LOST);
  loop();
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w51b_scale_lost_echo_inverted_when_idle() {
  resetHarness(false, true);
  reachReadyFromBoot();
  CHECK(!session.active);
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_LOST);
}

void w51c_scale_lost_silent_when_flag_off() {
  resetHarness(false, true);
  runtimeConfig.buzzerScaleLostBeep = false;
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  CHECK(localBuzzer.acceptedRequests == before);
}

void w52_local_buzzer_triple_on_manual_bbw_without_scale() {
  resetHarness(false, false);
  CHECK(!runtimeConfig.timerOnly);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  // No-scale TRIPLE (warning) then circuit start cue, which queues behind it.
  CHECK(localBuzzer.acceptedRequests == before + 2);
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
  // No-scale TRIPLE is suppressed when BBW is off; circuit start still beeps.
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

bool debugEventExists(DebugCode code, int32_t argument1 = INT32_MIN,
                      int32_t argument2 = INT32_MIN);

void enableNoScaleShotGuardForTest() {
  runtimeConfig.noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE);
  noScaleShotGuardArmed = true;
  noScaleShotGuardActivityAtMs = 0;
  noScaleShotGuardHold = false;
  noScaleShotGuardHoldAtMs = 0;
  noScaleShotGuardNeedsFreshActivator = false;
}

void attemptBlockedNoScaleStart() {
  CHECK(stopperState == StopperState::READY);
  CHECK(noScaleShotGuardArmed);
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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

void ns07_web_rinse_consumes_guard() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(stopperState == StopperState::RINSE);
  CHECK(!noScaleShotGuardArmed);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED));
  CHECK(!debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED));
}

void ns08_blocked_beep_respects_alert_checkbox() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.buzzerManualNoScaleBeep = false;
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(localBuzzer.acceptedRequests == before);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(!noScaleShotGuardArmed);
  CHECK(localBuzzer.acceptedRequests == before);
}

void ns09_armed_rinse_gesture_runs_and_consumes_guard() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  CHECK(noScaleShotGuardArmed);
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  const uint32_t rawOnAt = hostMillis;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(noScaleShotGuardArmed);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!noScaleShotGuardArmed);
  CHECK(debugEventExists(DebugCode::RINSE_CLASSIFIED));
  CHECK(debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED));
  CHECK(!debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED));
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 2);
}

void ns10_idle_rinse_gesture_does_not_rearm() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  CHECK(!noScaleShotGuardArmed);
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  const uint32_t rawOnAt = startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 2);
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(!noScaleShotGuardArmed);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 2);
}

void ns11_web_rinse_with_scale_keeps_armed() {
  resetHarness(false, true);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  publishWeight(0.0f);
  CHECK(noScaleShotGuardArmed);
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(stopperState == StopperState::RINSE);
  CHECK(noScaleShotGuardArmed);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED));
  CHECK(!debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED));
}

void ns12_failed_rinse_does_not_consume_guard() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  CHECK(noScaleShotGuardArmed);
  hostGptimerArmSucceeds = false;
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(noScaleShotGuardArmed);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED));
}

void ns13_require_scale_blocks_repeated_starts() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  reachReadyFromBoot();
  for (int attempt = 0; attempt < 2; ++attempt) {
    setRawPaddle(true);
    runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
    runLoopAfter(runtimeConfig.rinseGestureMs + 1);
    CHECK(stopperState == StopperState::READY);
    CHECK(noScaleShotGuardArmed);
    CHECK(noScaleShotGuardHold);
    CHECK(!getRelaySafetySnapshot().closed);
    setRawPaddle(false);
    runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
    CHECK(!noScaleShotGuardHold);
  }
  runLoopAfter(runtimeConfig.lastShotCooldownMs + 1);
  CHECK(noScaleShotGuardArmed);
}

void ns14_require_scale_blocks_rinse() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(noScaleShotGuardArmed);
  CHECK(!getRelaySafetySnapshot().closed);
}

void ns15_require_scale_is_inactive_when_bbw_off() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  runtimeConfig.timerOnly = true;
  ShotPreset &preset = mutableActiveShotPreset(presetBank);
  preset.brewByWeight = false;
  runtimeConfig = composeEffectiveConfig(runtimeConfig, presetBank);
  reachReadyFromBoot();
  startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(getRelaySafetySnapshot().closed);
}

void ns16_require_scale_reconnect_while_held_needs_new_gesture() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  reachReadyFromBoot();
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(noScaleShotGuardHold);
  setScaleConnected(true);
  markScaleWorkerProgress();
  publishWeight(0.0f);
  loop();
  CHECK(noScaleShotGuardHold);
  CHECK(!getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  runLoopAfter(1);
  CHECK(!noScaleShotGuardHold);
  CHECK(stopperState == StopperState::READY);
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
}

void completeRequireScaleBypassGesture(uint32_t gapMs = 100) {
  for (int cycle = 0; cycle < 3; ++cycle) {
    setRawPaddle(true);
    runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
    setRawPaddle(false);
    runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
    if (cycle < 2) {
      runLoopAfter(gapMs);
    }
  }
}

void ns17b_require_scale_double_cycle_stays_blocked() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  reachReadyFromBoot();

  for (int cycle = 0; cycle < 2; ++cycle) {
    setRawPaddle(true);
    runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
    setRawPaddle(false);
    runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  }

  CHECK(noScaleShotGuardArmed);
  CHECK(noScaleRequireBypassCycles == 2);
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.activeCue != BuzzerCue::SCALE_CONNECTED);
}

void ns17_require_scale_triple_cycle_temporarily_allows() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  runtimeConfig.lastShotCooldownMs = 1000;
  reachReadyFromBoot();

  completeRequireScaleBypassGesture();

  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!noScaleShotGuardArmed);
  CHECK(!noScaleShotGuardHold);
  CHECK(noScaleShotGuardNeedsFreshActivator);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_CONNECTED);
  CHECK(debugEventExists(DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED));

  runLoopAfter(runtimeConfig.lastShotCooldownMs + 1);
  CHECK(noScaleShotGuardArmed);
}

void ns18_require_scale_triple_cycle_must_fit_window() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  runtimeConfig.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
  reachReadyFromBoot();

  completeRequireScaleBypassGesture(NO_SCALE_REQUIRE_BYPASS_WINDOW_MS);

  CHECK(noScaleShotGuardArmed);
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.activeCue != BuzzerCue::SCALE_CONNECTED);
}

void w54_local_buzzer_triple_on_auto_to_manual_guard_end() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  const uint32_t before = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::AUTO_TO_MANUAL_GUARD, StopperState::REQUIRES_OFF);
  CHECK(localBuzzer.acceptedRequests == before + 2);
  CHECK(session.endReason == EndReason::AUTO_TO_MANUAL_GUARD);
  checkLocalCompletionTone();
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
  // Scale-lost RTTTL is preempted by machine circuit completion; ATM cue
  // queues behind it.
  CHECK(localBuzzer.acceptedRequests == before + 3);
  checkLocalCompletionTone();
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

void w65_scale_only_mutes_scale_lost() {
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

void checkLocalCompletionTone() {
  CHECK(localBuzzer.activeCue == BuzzerCue::SHOT_END);
  CHECK(localBuzzer.rtttlPlayback);
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
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL_FAST);
  CHECK(localBuzzer.activePulseRate ==
        static_cast<uint8_t>(ExtendedPulseRate::FAST));
  CHECK(localBuzzer.rtttlPlayback);
  CHECK(localBuzzer.request(BuzzerPattern::TRIPLE));
  CHECK(localBuzzer.active == BuzzerPattern::TRIPLE);
  CHECK(localBuzzer.activeCue == BuzzerCue::NONE);
  CHECK(!buzzerPatternIsPulseTrain(localBuzzer.pending));
  drainLocalBuzzer();
}

void w84b_echo_inverted_upgrades_weak_pending() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::SINGLE));
  CHECK(localBuzzer.active == BuzzerPattern::SINGLE);
  CHECK(localBuzzer.request(BuzzerPattern::DOUBLE));
  CHECK(localBuzzer.pending == BuzzerPattern::DOUBLE);
  CHECK(localBuzzer.request(BuzzerPattern::ECHO_INVERTED));
  CHECK(localBuzzer.pending == BuzzerPattern::ECHO_INVERTED);
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

void w91_chime_sequence_uses_irregular_note_timings() {
  resetHarness(false, false);
  CHECK(localBuzzer.request(BuzzerPattern::CHIME));
  CHECK(localBuzzer.active == BuzzerPattern::CHIME);
  CHECK(localBuzzer.beepCount == 3);
  CHECK(localBuzzer.onMs == BUZZER_CHIME_NOTES[0].onMs);
  CHECK(localBuzzer.gapMs == BUZZER_CHIME_NOTES[0].gapMs);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += BUZZER_CHIME_NOTES[0].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_CHIME_NOTES[0].gapMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  CHECK(localBuzzer.onMs == BUZZER_CHIME_NOTES[1].onMs);
  CHECK(localBuzzer.gapMs == BUZZER_CHIME_NOTES[1].gapMs);
  hostMillis += BUZZER_CHIME_NOTES[1].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_CHIME_NOTES[1].gapMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  CHECK(localBuzzer.onMs == BUZZER_CHIME_NOTES[2].onMs);
  hostMillis += BUZZER_CHIME_NOTES[2].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(!localBuzzer.busy());
}

void w92_parse_sequence_pattern_ids() {
  BuzzerPattern parsed = BuzzerPattern::NONE;
  CHECK(parseBuzzerPatternId("chime", parsed));
  CHECK(parsed == BuzzerPattern::CHIME);
  CHECK(parseBuzzerPatternId("swing", parsed));
  CHECK(parsed == BuzzerPattern::SWING);
  CHECK(parseBuzzerPatternId("echo", parsed));
  CHECK(parsed == BuzzerPattern::ECHO);
  CHECK(parseBuzzerPatternId("echoinv", parsed));
  CHECK(parsed == BuzzerPattern::ECHO_INVERTED);
  CHECK(parseBuzzerPatternId("morse", parsed));
  CHECK(parsed == BuzzerPattern::MORSE);
  CHECK(parseBuzzerPatternId("snap", parsed));
  CHECK(parsed == BuzzerPattern::SNAP);
}

void w93_scale_connected_echo_on_rising_edge() {
  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  CHECK(runtimeConfig.buzzerScaleConnectedBeep);
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(true);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_CONNECTED);
  CHECK(localBuzzer.rtttlPlayback);
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  CHECK(localBuzzer.beepCount ==
        localBuzzer.cueNoteCount[static_cast<uint8_t>(
            BuzzerCue::SCALE_CONNECTED)]);
#endif
  const uint32_t afterConnect = localBuzzer.acceptedRequests;
  setScaleConnected(true);
  CHECK(localBuzzer.acceptedRequests == afterConnect);
  setScaleConnected(false);
  CHECK(localBuzzer.acceptedRequests == afterConnect + 1);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_LOST ||
        localBuzzer.pendingCue == BuzzerCue::SCALE_LOST);
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    hostMillis += 40;
    hostServiceEspTimer(localBuzzer.phaseTimer);
    localBuzzer.service(hostMillis);
  }
  setScaleConnected(true);
  CHECK(localBuzzer.acceptedRequests == afterConnect + 2);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_CONNECTED);
}

void w94_scale_connected_silent_when_flag_off_or_scale_only() {
  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runtimeConfig.buzzerScaleConnectedBeep = false;
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(true);
  CHECK(localBuzzer.acceptedRequests == before);

  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  runtimeConfig.buzzerScaleConnectedBeep = true;
  const uint32_t beforePriority = localBuzzer.acceptedRequests;
  setScaleConnected(true);
  CHECK(localBuzzer.acceptedRequests == beforePriority + 1);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_CONNECTED);

  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  const uint32_t beforeScaleOnly = localBuzzer.acceptedRequests;
  setScaleConnected(true);
  CHECK(localBuzzer.acceptedRequests == beforeScaleOnly);
}

void w95_web_buzzer_test_plays_chime_sequence() {
  resetHarness(false, false);
  reachReadyFromBoot();
  BuzzerPattern parsed = BuzzerPattern::NONE;
  CHECK(parseBuzzerPatternId("chime", parsed));
  CHECK(parsed == BuzzerPattern::CHIME);
  WebCommand command = webControlCommand(WebCommandType::BUZZER_TEST);
  command.buzzerPattern = BuzzerPattern::CHIME;
  const uint32_t before = localBuzzer.acceptedRequests;
  processWebCommand(command);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.active == BuzzerPattern::CHIME);
  CHECK(localBuzzer.beepCount == 3);
  CHECK(localBuzzer.onMs == BUZZER_CHIME_NOTES[0].onMs);
}

void w96_echo_inverted_uses_long_bookend_tones() {
  resetHarness(false, false);
  CHECK(BUZZER_ECHO_INVERTED_NOTES[0].onMs == 220);
  CHECK(BUZZER_ECHO_INVERTED_NOTES[3].onMs == 220);
  CHECK(BUZZER_ECHO_INVERTED_NOTES[1].onMs == 50);
  CHECK(BUZZER_ECHO_INVERTED_NOTES[2].onMs == 50);
  CHECK(BUZZER_ECHO_INVERTED_NOTES[3].gapMs == 0);
  CHECK(localBuzzer.request(BuzzerPattern::ECHO_INVERTED));
  CHECK(localBuzzer.active == BuzzerPattern::ECHO_INVERTED);
  CHECK(localBuzzer.beepCount == 4);
  CHECK(localBuzzer.onMs == BUZZER_ECHO_INVERTED_NOTES[0].onMs);
  CHECK(localBuzzer.gapMs == BUZZER_ECHO_INVERTED_NOTES[0].gapMs);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[0].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[0].gapMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  CHECK(localBuzzer.onMs == BUZZER_ECHO_INVERTED_NOTES[1].onMs);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[1].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[1].gapMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  CHECK(localBuzzer.onMs == BUZZER_ECHO_INVERTED_NOTES[2].onMs);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[2].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[2].gapMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
  CHECK(localBuzzer.onMs == BUZZER_ECHO_INVERTED_NOTES[3].onMs);
  hostMillis += BUZZER_ECHO_INVERTED_NOTES[3].onMs;
  hostServiceEspTimer(localBuzzer.phaseTimer);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(!localBuzzer.busy());

  reachReadyFromBoot();
  BuzzerPattern parsed = BuzzerPattern::NONE;
  CHECK(parseBuzzerPatternId("echoinv", parsed));
  CHECK(parsed == BuzzerPattern::ECHO_INVERTED);
  WebCommand command = webControlCommand(WebCommandType::BUZZER_TEST);
  command.buzzerPattern = BuzzerPattern::ECHO_INVERTED;
  const uint32_t before = localBuzzer.acceptedRequests;
  processWebCommand(command);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(localBuzzer.active == BuzzerPattern::ECHO_INVERTED);
}

void w98_buzzer_sequences_start_and_end_with_sound() {
  const BuzzerPattern patterns[] = {
      BuzzerPattern::CHIME,          BuzzerPattern::SWING,
      BuzzerPattern::ECHO,           BuzzerPattern::ECHO_INVERTED,
      BuzzerPattern::MORSE,          BuzzerPattern::SNAP,
      BuzzerPattern::RECOVERY_NETWORK_OK, BuzzerPattern::RECOVERY_FACTORY_OK,
      BuzzerPattern::RECOVERY_ERROR};
  for (const BuzzerPattern pattern : patterns) {
    uint8_t count = 0;
    const BuzzerNote *notes = buzzerSequenceNotes(pattern, count);
    CHECK(buzzerNotesStartAndEndWithSound(notes, count));
  }
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
  std::strncpy(scale.connectedProtocol, "acaia",
               sizeof(scale.connectedProtocol) - 1);
  scale.independentBeepSupported = false;
  scale.tareStartTimerSupported = false;
  updateWorkerLinkState();
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
  std::strncpy(scale.connectedProtocol, "acaia",
               sizeof(scale.connectedProtocol) - 1);
  scale.independentBeepSupported = false;
  updateWorkerLinkState();
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

void w74b_sound_alert_master_mutes_and_cancels_all_routes() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  CHECK(emitAlert(AlertEvent::FIRST_DROP, 7));
  CHECK(scaleBeepPending);

  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.soundAlertsMuted = true;
  processWebCommand(update);
  CHECK(runtimeConfig.soundAlertsMuted);
  CHECK(!scaleBeepPending);
  CHECK(!scaleCompletionBeepScheduled);
  CHECK(!emitAlert(AlertEvent::FIRST_DROP, 8));
  CHECK(!scaleBeepPending);

  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  const uint32_t accepted = localBuzzer.acceptedRequests;
  emitImmediateCommandAlertIfBuzzer(AlertEvent::START_TIMER);
  CHECK(localBuzzer.acceptedRequests == accepted);
  requestCompletionAlert();
  CHECK(!scaleCompletionBeepScheduled);

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  CHECK(emitAlert(AlertEvent::SCALE_CONNECTED));
  CHECK(localBuzzer.busy());
  update = WebCommand{};
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.soundAlertsMuted = true;
  processWebCommand(update);
  CHECK(!localBuzzer.busy());
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
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.commandLog.empty());

  hostMillis += BOOKOO_CONNECT_BEEP_DEFER_MS - 1;
  serviceScaleWorkerLink();
  CHECK(scale.commandLog.empty());
  hostMillis += 1;
  serviceScaleWorkerLink();
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:0");

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.bookooMuteOnBuzzerOnly = true;
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  scale.scanning = true;
  scale.connected = true;
  scale.commandLog.clear();
  lastScanCycleMs = 0;
  lastConnectLogMs = 0;
  connectAttemptSeriesActive = false;
  scanSessionAtMs = 0;
  scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.commandLog.empty());
  scale.newWeightAvailableValue = true;
  serviceScaleWorkerLink();
  CHECK(scale.commandLog.empty());
  serviceScaleWorkerLink();
  CHECK(scale.commandLog.size() == 1);
  CHECK(scale.commandLog[0] == "setBeepLevel:0");
}

void w76_buzzer_only_start_beeps_at_circuit_not_ble_result() {
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

void w77_scale_priority_disconnected_beeps_on_circuit_without_ble() {
  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  CHECK(localBuzzer.acceptedRequests == before + 2);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 0);
  // Drain manual-no-scale TRIPLE so stop completion LONG can start cleanly.
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  const uint32_t afterStart = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == afterStart + 1);
  checkLocalCompletionTone();
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void w78_scale_priority_connected_start_beeps_at_circuit() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w79_buzzer_only_stop_beeps_before_timer_stop_result() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  for (uint32_t step = 0; step < 20 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  const uint32_t before = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  checkLocalCompletionTone();
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 1);
}

void w79b_stop_beeps_at_circuit_while_scale_timer_stop_still_pending() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runtimeConfig.scaleTimerStopExtraDelayMs = 200;
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  drainLocalBuzzer();
  const uint32_t before = localBuzzer.acceptedRequests;
  publishScaleTimer(5000);
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  checkLocalCompletionTone();
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
}

void w79c_rinse_without_scale_beeps_at_circuit_open_and_close() {
  resetHarness(false, false);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  runtimeConfig.buzzerManualNoScaleBeep = false;
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  processWebCommand(webControlCommand(WebCommandType::RINSE));
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  drainLocalBuzzer();
  const uint32_t afterStart = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::RINSE_COMPLETE, StopperState::READY);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == afterStart + 1);
  checkLocalCompletionTone();
}

void w79d_scale_only_start_and_stop_still_use_local_circuit_beeps() {
  resetHarness(false, true);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  reachReadyFromBoot();
  const uint32_t before = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(executeNextScaleCommand());
  drainLocalBuzzer();
  const uint32_t afterStart = localBuzzer.acceptedRequests;
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(localBuzzer.acceptedRequests == afterStart + 1);
  checkLocalCompletionTone();
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
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    runLoopAfter(40);
  }
  const uint32_t before = localBuzzer.acceptedRequests;
  setScaleConnected(false);
  // Scale-lost RTTTL on disconnect, then command fallback start cue.
  CHECK(localBuzzer.acceptedRequests == before + 1);
  CHECK(executeNextScaleCommand());
  CHECK(localBuzzer.acceptedRequests == before + 2);
}

void configureEclairCapabilities() {
  strncpy(scale.connectedProtocol, "atomheart_eclair",
          sizeof(scale.connectedProtocol) - 1);
  scale.connectedProtocol[sizeof(scale.connectedProtocol) - 1] = '\0';
  scale.tareStartTimerSupported = false;
  scale.independentBeepSupported = false;
  scale.commandFeedbackSupported = false;
  updateWorkerLinkState();
}

void w94_eclair_scale_priority_uses_local_alerts() {
  resetHarness(false, true);
  configureEclairCapabilities();
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  reachReadyFromBoot();
  const uint32_t startAlerts = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(localBuzzer.acceptedRequests == startAlerts + 1);
  CHECK(executeNextScaleCommand());
  CHECK(scale.commandLog.size() == 3);
  CHECK(scale.commandLog[0] == "resetTimer");
  CHECK(scale.commandLog[1] == "startTimer");
  CHECK(scale.commandLog[2] == "tare");
  CHECK(scale.beepCalls == 0);
  CHECK(scale.setBeepLevelCalls == 0);
  establishPostTareBaseline();
  advanceToBrew();
  drainLocalBuzzer();

  const uint32_t retareAlerts = localBuzzer.acceptedRequests;
  CHECK(requestRemoteRetare());
  emitImmediateCommandAlertIfBuzzer(AlertEvent::TARE);
  CHECK(localBuzzer.acceptedRequests == retareAlerts + 1);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 1);

  drainLocalBuzzer();
  const uint32_t firstDropAlerts = localBuzzer.acceptedRequests;
  simulateFirstDrops();
  CHECK(localBuzzer.acceptedRequests == firstDropAlerts + 1);
  CHECK(!scaleBeepPending);
  CHECK(scale.beepCalls == 0);
}

void w95_eclair_scale_only_omits_unsupported_alerts() {
  resetHarness(false, true);
  configureEclairCapabilities();
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  reachReadyFromBoot();
  const uint32_t alerts = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(localBuzzer.acceptedRequests == alerts + 1);
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  advanceToBrew();
  drainLocalBuzzer();
  const uint32_t afterStart = localBuzzer.acceptedRequests;
  simulateFirstDrops();
  CHECK(localBuzzer.acceptedRequests == afterStart);
  CHECK(!scaleBeepPending);
  CHECK(scale.beepCalls == 0);
  CHECK(scale.setBeepLevelCalls == 0);
}

void w97_eclair_buzzer_only_never_queues_scale_beeps() {
  resetHarness(false, true);
  configureEclairCapabilities();
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  reachReadyFromBoot();
  const uint32_t startAlerts = localBuzzer.acceptedRequests;
  startCycle();
  CHECK(localBuzzer.acceptedRequests == startAlerts + 1);
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  advanceToBrew();
  drainLocalBuzzer();
  const uint32_t firstDropAlerts = localBuzzer.acceptedRequests;
  simulateFirstDrops();
  CHECK(localBuzzer.acceptedRequests == firstDropAlerts + 1);
  CHECK(!scaleBeepPending);
  CHECK(scale.beepCalls == 0);
  CHECK(scale.setBeepLevelCalls == 0);
}

void setHostPreferredScaleMac(const char *mac) {
  strncpy(scalePreferredMac, mac, PREFERRED_SCALE_MAC_CAPACITY - 1);
  scalePreferredMac[PREFERRED_SCALE_MAC_CAPACITY - 1] = '\0';
}

void d01_idle_scan_stays_enabled_between_ticks() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.lastAddressScan);
  CHECK(scale.startScanCalls == 1);
  const size_t calls = scale.startScanCalls;
  hostMillis += SCALE_DISCOVERY_TICK_MS - 1;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.startScanCalls == calls);
}

void d13_idle_delays_relax_without_scale() {
  resetHarness(false, false);
  CHECK(!scale.isConnected());
  CHECK(!scale.isConnecting());
  CHECK(scaleWorkerTickDelayMs() == SCALE_WORKER_NO_SCALE_DELAY_MS);
  CHECK(controlLoopTickDelayMs() == LOOP_NO_SCALE_DELAY_MS);

  scale.connecting = true;
  CHECK(scaleWorkerTickDelayMs() == 1);
  CHECK(controlLoopTickDelayMs() == LOOP_NO_SCALE_DELAY_MS);
  scale.connecting = false;

  ScaleCommand command;
  command.type = ScaleCommandType::TARE_ONLY;
  CHECK(xQueueSend(scaleCommandQueue, &command, 0) == pdTRUE);
  CHECK(scaleWorkerTickDelayMs() == 1);
  CHECK(xQueueReceive(scaleCommandQueue, &command, 0) == pdTRUE);
  CHECK(scaleWorkerTickDelayMs() == SCALE_WORKER_NO_SCALE_DELAY_MS);

  setScaleConnected(true);
  markScaleWorkerProgress();
  CHECK(scaleWorkerTickDelayMs() == 1);
  CHECK(controlLoopTickDelayMs() == 1);
}

void d14_control_status_publishes_on_cycle_edge() {
  resetHarness(false, false);
  reachReadyFromBoot();
  publishControlStatus();
  CHECK(!publishedControlStatus.activeCycle);
  hostMillis += 10;
  startCycle();
  CHECK(publishedControlStatus.activeCycle);
  CHECK(publishedControlStatus.state == StopperState::BREW ||
        publishedControlStatus.state == StopperState::MANUAL_NO_SCALE);
  ControlGateSnapshot gate;
  copyControlGate(gate);
  CHECK(gate.activeCycle);
  CHECK(gate.relayClosed);

  const uint32_t seqAfterEdge = controlStatusSeq;
  serviceControlStatusPublish();
  CHECK(controlStatusSeq == seqAfterEdge);
  controlStatusPublishRequested = true;
  serviceControlStatusPublish();
  CHECK(controlStatusSeq != seqAfterEdge);
  CHECK(!controlStatusPublishRequested);
}

void d15_companion_publish_throttles_without_scale() {
  CHECK(!bleCompanionStatusShouldPublish(true, false, 10, 11));
  CHECK(bleCompanionStatusShouldPublish(false, true, 10, 11));
  CHECK(!bleCompanionStatusShouldPublish(false, false, 10, 59));
  CHECK(bleCompanionStatusShouldPublish(false, false, 10, 60));
  CHECK(bleCompanionStatusShouldPublish(true, false, 10, 60));
  CHECK(bleCompanionStatusShouldPublish(false, false, 0, 1));
  BleCompanionStatusSnapshot a;
  BleCompanionStatusSnapshot b;
  CHECK(bleCompanionStatusUnchanged(a, b));
  b.advertising = true;
  CHECK(!bleCompanionStatusUnchanged(a, b));
}

void d02_first_mode_uses_name_scan() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  scalePreferredMac[0] = '\0';
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
  CHECK(!scale.lastAddressScan);
  CHECK(scale.lastStartScanMac[0] == '\0');
}

void d03_scan_start_failed_retries_immediately() {
  resetHarness(false, false);
  reachReadyFromBoot();
  scale.startScanSucceeds = false;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(!scale.scanning);
  CHECK(scale.startScanCalls == 1);
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.startScanCalls == 2);
  CHECK(!scale.scanning);
}

void d04_full_cache_keeps_directed_scan() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  for (uint8_t tick = 0; tick < 3; ++tick) {
    hostMillis += SCALE_DISCOVERY_TICK_MS;
    serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                connectAttemptSeriesActive,
                                scanSessionAtMs, scanLastAdvertAtMs);
  }
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.lastAddressScan);
  CHECK(strcmp(scale.lastStartScanMac, "AA:BB:CC:DD:EE:FF") == 0);
}

void d05_hci_watchdog_force_restarts_same_filter() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.startScanCalls == 1);
  CHECK(!scale.lastForceRestart);
  CHECK(scale.lastScanInterval == BLE_SCAN_AGGRESSIVE_INTERVAL);
  CHECK(scale.lastScanWindow == BLE_SCAN_AGGRESSIVE_WINDOW);
  const size_t callsBeforeRestart = scale.startScanCalls;
  size_t ticks = 0;
  while (scale.startScanCalls == callsBeforeRestart) {
    hostMillis += SCALE_DISCOVERY_TICK_MS;
    serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                connectAttemptSeriesActive,
                                scanSessionAtMs, scanLastAdvertAtMs);
    ++ticks;
    CHECK(ticks < 40);
  }
  CHECK(ticks == SCALE_SCAN_HCI_RESTART_MS / SCALE_DISCOVERY_TICK_MS);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(scale.lastAddressScan);
  CHECK(scale.lastForceRestart);
  CHECK(scale.startScanCalls == 2);
}

void d05b_scan_intensity_change_restarts_gap() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.startScanCalls == 1);
  CHECK(scale.lastScanInterval == BLE_SCAN_AGGRESSIVE_INTERVAL);
  applyLiveBleScanIntensity(BleScanIntensity::LIGHT);
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.startScanCalls == 2);
  CHECK(scale.lastForceRestart);
  CHECK(scale.lastScanInterval == BLE_SCAN_LIGHT_INTERVAL);
  CHECK(scale.lastScanWindow == BLE_SCAN_LIGHT_WINDOW);
  const size_t calls = scale.startScanCalls;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.startScanCalls == calls);
}

void d05c_scan_intensity_hci_presets() {
  CHECK(BLE_SCAN_LIGHT_INTERVAL == 0x00B8);
  CHECK(BLE_SCAN_LIGHT_WINDOW == 0x002E);
  CHECK(BLE_SCAN_NORMAL_INTERVAL == 0x0064);
  CHECK(BLE_SCAN_NORMAL_WINDOW == 0x0032);
  CHECK(BLE_SCAN_AGGRESSIVE_INTERVAL == 0x0020);
  CHECK(BLE_SCAN_AGGRESSIVE_WINDOW == 0x0020);
  uint16_t interval = 0;
  uint16_t window = 0;
  bleScanHciParams(BleScanIntensity::LIGHT, interval, window);
  CHECK(interval == BLE_SCAN_LIGHT_INTERVAL);
  CHECK(window == BLE_SCAN_LIGHT_WINDOW);
  bleScanHciParams(BleScanIntensity::NORMAL, interval, window);
  CHECK(interval == BLE_SCAN_NORMAL_INTERVAL);
  CHECK(window == BLE_SCAN_NORMAL_WINDOW);
  bleScanHciParams(BleScanIntensity::AGGRESSIVE, interval, window);
  CHECK(interval == BLE_SCAN_AGGRESSIVE_INTERVAL);
  CHECK(window == BLE_SCAN_AGGRESSIVE_WINDOW);
  BleScanIntensity parsed = BleScanIntensity::LIGHT;
  CHECK(parseBleScanIntensityId("aggressive", parsed));
  CHECK(parsed == BleScanIntensity::AGGRESSIVE);
  CHECK(parseBleScanIntensityId("light", parsed));
  CHECK(parsed == BleScanIntensity::LIGHT);
  CHECK(parseBleScanIntensityId("normal", parsed));
  CHECK(parsed == BleScanIntensity::NORMAL);
  CHECK(!parseBleScanIntensityId("burst", parsed));
  CHECK(strcmp(bleScanIntensityName(BleScanIntensity::NORMAL), "normal") == 0);
}

void d16_scale_connect_debug_reports_phases() {
  resetHarness(false, false);
  reachReadyFromBoot();
  debugLog.clear();
  ringRetainLogLevel = LogLevel::DEBUG;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(debugEventExists(DebugCode::SCALE_SCAN_STARTED, SCALE_SCAN_TARGET_ANY,
                         static_cast<int32_t>(BleScanIntensity::AGGRESSIVE)));
  CHECK(!debugEventExists(DebugCode::SCALE_CONNECTING));

  scale.connecting = true;
  scale.scanning = false;
  scale.connectStep = 2;
  scale.connectAttempts = 1;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(debugEventExists(DebugCode::SCALE_GATT_CONNECTING, SCALE_SCAN_TARGET_ANY,
                         INT32_MIN));
  CHECK(debugEventExists(DebugCode::SCALE_CONNECT_ATTEMPT_FAILED, 1, 2));

  scale.connectAttempts = 3;
  scale.pollScanFailsConnect = true;
  scale.disconnectReason = ScaleDisconnectReason::CONNECT_FAILED;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(debugEventExists(DebugCode::SCALE_CONNECT_ATTEMPT_FAILED, 3, 2));
  CHECK(debugEventExists(DebugCode::SCALE_CONNECT_FAILED, 4, 2));

  char message[128] = {};
  DebugEvent failed = {};
  failed.code = DebugCode::SCALE_CONNECT_FAILED;
  failed.argument1 = 4;
  failed.argument2 = 2;
  CHECK(formatLifecycleDebugMessage(failed, message, sizeof(message)));
  CHECK(std::string(message).find("connect failed") != std::string::npos);
  CHECK(std::string(message).find("step=connect") != std::string::npos);
  CHECK(debugCodeDefaultLevel(DebugCode::SCALE_CONNECT_FAILED) ==
        LogLevel::WARNING);
  CHECK(debugCodeDefaultLevel(DebugCode::SCALE_SCAN_STARTED) == LogLevel::INFO);
  CHECK(debugCodeDefaultLevel(DebugCode::SCALE_GATT_CONNECTING) ==
        LogLevel::INFO);
}

void d06_forget_pauses_discovery_for_30s() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  const size_t callsBeforeForget = scale.startScanCalls;
  scale.connected = true;
  clearPreferredScaleCache();
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(!scale.connected);
  CHECK(!scale.scanning);
  CHECK(scale.startScanCalls == callsBeforeForget);
  hostMillis += SCALE_PAIRING_DISCOVERY_PAUSE_MS - 1;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(!scale.scanning);
  CHECK(scale.startScanCalls == callsBeforeForget);
  hostMillis += 2;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs, scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
  CHECK(scale.lastStartScanMac[0] == '\0');
}

void d07_prefer_falls_back_after_grace() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::PREFER);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(scale.directedScan);
  CHECK(!scale.lastAddressScan);
  const size_t callsBefore = scale.startScanCalls;
  hostMillis += SCALE_PREFER_FALLBACK_MS;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
  CHECK(!scale.lastAddressScan);
  CHECK(scale.startScanCalls > callsBefore);
  CHECK(scale.lastStartScanMac[0] == '\0');
}

void d08_select_none_clears_without_pause() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  selectPreferredScale("", "");
  CHECK(scalePreferredMac[0] == '\0');
  CHECK(runtimeConfig.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FIRST));
  CHECK(!scaleDiscoveryPaused());
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
}

void d09_first_mode_connects_seen_advertisement() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  scalePreferredMac[0] = '\0';
  memset(scaleHistory, 0, sizeof(scaleHistory));
  scaleHistorySeq = 0;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(!scale.directedScan);
  CHECK(scale.lastStartScanMac[0] == '\0');

  strncpy(scale.seenMac, "AA:BB:CC:DD:EE:FF", sizeof(scale.seenMac) - 1);
  strncpy(scale.seenName, "LUNAR", sizeof(scale.seenName) - 1);
  scale.seenPending = true;
  scale.pollScanConnects = true;
  scale.pollScanStepsToConnect = 1;
  hostMillis += SCALE_DISCOVERY_TICK_MS;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.connected);
  CHECK(getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED);
  CHECK(preferredScaleMacEqual(scaleHistory[0].mac, "AA:BB:CC:DD:EE:FF"));
  CHECK(scalePreferredMac[0] == '\0');
}

void d12_advertisement_history_does_not_dirty_persist() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  scalePreferredMac[0] = '\0';
  memset(scaleHistory, 0, sizeof(scaleHistory));
  scaleHistorySeq = 0;
  scalePreferredMacDirty = false;
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  strncpy(scale.seenMac, "AA:BB:CC:DD:EE:01", sizeof(scale.seenMac) - 1);
  strncpy(scale.seenName, "LUNAR", sizeof(scale.seenName) - 1);
  scale.seenPending = true;
  scale.pollScanConnects = false;
  hostMillis += SCALE_DISCOVERY_TICK_MS;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(preferredScaleMacEqual(scaleHistory[0].mac, "AA:BB:CC:DD:EE:01"));
  CHECK(!scalePreferredMacDirty);

  scale.pollScanConnects = true;
  scale.pollScanStepsToConnect = 1;
  strncpy(scale.seenMac, "AA:BB:CC:DD:EE:01", sizeof(scale.seenMac) - 1);
  strncpy(scale.seenName, "LUNAR", sizeof(scale.seenName) - 1);
  scale.seenPending = true;
  hostMillis += SCALE_DISCOVERY_TICK_MS;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.connected);
  CHECK(scalePreferredMacDirty);
}

void d10_companion_pauses_while_scale_connected_or_connecting() {
  resetHarness(false, false);
  reachReadyFromBoot();
  scale.connected = false;
  scale.connecting = false;
  scale.scanning = false;
  CHECK(!companionAdvertisingShouldPause());
  scale.scanning = true;
  CHECK(!companionAdvertisingShouldPause());
  scale.scanning = false;
  scale.connecting = true;
  CHECK(companionAdvertisingShouldPause());
  scale.connecting = false;
  scale.connected = true;
  CHECK(companionAdvertisingShouldPause());
}

void d10b_companion_pauses_for_hunt_window_after_scan_start() {
  resetHarness(false, false);
  reachReadyFromBoot();
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.scanning);
  CHECK(companionAdvertisingShouldPause());
  hostMillis += SCALE_HUNT_RF_CLEAR_MS - 1;
  CHECK(companionAdvertisingShouldPause());
  hostMillis += 2;
  CHECK(!companionAdvertisingShouldPause());
  CHECK(scale.scanning);
}

void d11_select_preferred_is_noop_when_unchanged() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                              connectAttemptSeriesActive, scanSessionAtMs,
                              scanLastAdvertAtMs);
  CHECK(scale.scanning);
  const size_t calls = scale.startScanCalls;
  selectPreferredScale("", "");
  CHECK(scale.scanning);
  CHECK(scale.startScanCalls == calls);

  setHostPreferredScaleMac("AA:BB:CC:DD:EE:FF");
  strncpy(scalePreferredName, "LUNAR", sizeof(scalePreferredName) - 1);
  selectPreferredScale("AA:BB:CC:DD:EE:FF", "LUNAR");
  CHECK(scale.scanning);
  CHECK(scale.startScanCalls == calls);
}

void w62_local_buzzer_drive_matches_compile_flag() {
  resetHarness(false, false);
  CHECK(localBuzzer.ready);
  CHECK(hostPinMode[BUZZER_GPIO] == OUTPUT);
  CHECK(hostPinLevel[BUZZER_GPIO] == LOW);
  CHECK(hostLedcAttachCalls == 1);
}

void w99_rtttl_parser_decodes_notes_and_rests() {
  RtttlNote notes[RTTTL_MAX_NOTES];
  uint8_t count = 0;
  CHECK(parseRtttl(RTTTL_TARE, notes, count));
  CHECK(count == 1);
  CHECK(notes[0].freqHz >= 780 && notes[0].freqHz <= 790);
  CHECK(notes[0].durationMs >= 60 && notes[0].durationMs <= 80);
  CHECK(parseRtttl(RTTTL_PADDLE_OFF, notes, count));
  CHECK(count == 3);
  CHECK(notes[0].freqHz >= 780 && notes[0].freqHz <= 790);
  CHECK(notes[1].freqHz == 0);
  CHECK(notes[2].freqHz >= 780 && notes[2].freqHz <= 790);
  CHECK(parseRtttl(RTTTL_START_TIMER, notes, count));
  CHECK(count == 1);
  CHECK(notes[0].freqHz >= 1040 && notes[0].freqHz <= 1055);
  for (uint8_t i = 1; i <= static_cast<uint8_t>(BuzzerCue::RECOVERY_ERROR);
       ++i) {
    const BuzzerCue cue = static_cast<BuzzerCue>(i);
    const char *rtttl = rtttlForCue(cue);
    if (rtttl == nullptr) {
      CHECK(buzzerCueIsLooping(cue) || cue == BuzzerCue::BULLSEYE);
      continue;
    }
    CHECK(parseRtttl(rtttl, notes, count));
    CHECK(count > 0);
  }
  CHECK(parseRtttl(RTTTL_EXTENDED_PULSE_SLOW, notes, count));
  CHECK(count == 3);
  CHECK(notes[0].freqHz >= 490 && notes[0].freqHz <= 498);
  CHECK(notes[0].durationMs == 20);
  CHECK(notes[1].freqHz == 0 && notes[1].durationMs == 320);
  CHECK(notes[2].freqHz == 0 && notes[2].durationMs == 160);
  CHECK(parseRtttl(RTTTL_EXTENDED_PULSE_MEDIUM, notes, count));
  CHECK(count == 2);
  CHECK(notes[0].durationMs == 20 && notes[1].durationMs == 320);
  CHECK(parseRtttl(RTTTL_EXTENDED_PULSE_FAST, notes, count));
  CHECK(count == 3);
  CHECK(notes[0].durationMs == 20 && notes[1].durationMs == 213);
  CHECK(notes[2].durationMs == 20);
  CHECK(parseRtttl(RTTTL_EXTENDED_PULSE_RAPID, notes, count));
  CHECK(count == 3);
  CHECK(notes[0].durationMs == 20 && notes[1].durationMs == 160);
  CHECK(notes[2].durationMs == 20);
  CHECK(rtttlForExtendedPulseRate(static_cast<uint8_t>(
            ExtendedPulseRate::OFF)) == nullptr);
  CHECK(rtttlForExtendedPulseRate(static_cast<uint8_t>(
            ExtendedPulseRate::FAST)) == RTTTL_EXTENDED_PULSE_FAST);
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  resetHarness(false, false);
  CHECK(localBuzzer.cueNoteCount[static_cast<uint8_t>(BuzzerCue::TARE)] == 1);
  CHECK(localBuzzer.cueNoteCount[static_cast<uint8_t>(
            BuzzerCue::PADDLE_REMINDER)] == 3);
  CHECK(localBuzzer.pulseNoteCount[static_cast<uint8_t>(
            ExtendedPulseRate::FAST)] == 3);
#endif
}

void w99f_bullseye_rtttl_is_bounded_and_plays_without_allocation() {
  resetHarness(false, false);
  constexpr const char *tune =
      "bullseye:d=16,o=5,b=180:c,e,g,c6,8p,c6";
  BullseyeMelodyConfig config;
  config.enabled = true;
  copyCString(config.rtttl, sizeof(config.rtttl), tune);
  CHECK(validBullseyeMelodyConfig(config));
  CHECK(localBuzzer.configureBullseyeRtttl(config.rtttl));
  CHECK(localBuzzer.requestBullseye());
  CHECK(localBuzzer.activeCue == BuzzerCue::BULLSEYE);
  CHECK(localBuzzer.rtttlPlayback);
  CHECK(localBuzzer.rtttlCount == 6);

  config.rtttl[0] = '\0';
  CHECK(!validBullseyeMelodyConfig(config));
  config.enabled = false;
  CHECK(validBullseyeMelodyConfig(config));
  copyCString(config.rtttl, sizeof(config.rtttl), "not-rtttl");
  CHECK(!validBullseyeMelodyConfig(config));

  uint8_t count = 0;
  CHECK(!parseRtttlBounded("x:d=4,o=5,b=120:,c", nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
  CHECK(!parseRtttlBounded("x:d=4,o=5,b=120:c,,d", nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
  CHECK(!parseRtttlBounded("x:d=4,o=5,b=120:cd", nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
  CHECK(!parseRtttlBounded("x:d=4,o=5,b=120:65c", nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
  CHECK(!parseRtttlBounded("x:d=4,o=5,b=120:p#", nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
  CHECK(!parseRtttlBounded("x:d=4,o=5,b=120:c9", nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
  char unterminated[RTTTL_INPUT_CAPACITY];
  memset(unterminated, 'c', sizeof(unterminated));
  CHECK(!parseRtttlBounded(unterminated, nullptr,
                           BULLSEYE_RTTTL_MAX_NOTES, count));
}

void w99i_bullseye_test_never_queues_mutable_custom_notes() {
  resetHarness(false, false);
  constexpr const char *testTune = "test:d=16,o=5,b=180:c,e,g";
  CHECK(localBuzzer.configureBullseyeRtttl(testTune));
  const BuzzerToneCommand tare =
      deriveBuzzerTone(AlertEvent::TARE, false, 0, 0);
  CHECK(localBuzzer.requestTone(tare));
  CHECK(!localBuzzer.requestBullseye(/*allowQueue=*/false));
  CHECK(localBuzzer.activeCue == BuzzerCue::TARE);
  CHECK(localBuzzer.pendingCue == BuzzerCue::NONE);

  localBuzzer.stopAll();
  CHECK(localBuzzer.requestBullseye(/*allowQueue=*/false));
  CHECK(localBuzzer.activeCue == BuzzerCue::BULLSEYE);
  CHECK(localBuzzer.rtttlCount == 3);
}

void w99g_bullseye_requires_one_second_of_exact_fresh_samples() {
  BullseyeTracker tracker;
  tracker.arm(36, 1000, 3000, 10);
  CHECK(!tracker.accept(36.0f, 1100, 11, 1000));
  CHECK(!tracker.accept(35.99f, 1500, 12, 1000));
  CHECK(!tracker.accept(36.0f, 1600, 13, 1000));
  CHECK(!tracker.accept(36.0f, 2500, 14, 1000));
  CHECK(tracker.accept(36.0f, 2600, 15, 1000));
  CHECK(!tracker.pending);

  tracker.arm(36, 1000, 3000, 20);
  CHECK(!tracker.accept(36.0f, 3900, 21, 1000));
  CHECK(tracker.accept(36.0f, 4900, 22, 1000));
  CHECK(!tracker.expired(5000));

  tracker.arm(36, 1000, 3000, 30);
  CHECK(!tracker.accept(36.0f, 2000, 31, 1000));
  CHECK(!tracker.accept(36.0f, 3001, 32, 1000));
  CHECK(tracker.targetSampleCount == 1);
  CHECK(tracker.expired(5001));
}

void w99h_bullseye_service_runs_only_in_buzzer_only_mode() {
  resetHarness(false, true);
  bullseyeMelodyConfig.enabled = true;
  copyCString(bullseyeMelodyConfig.rtttl,
              sizeof(bullseyeMelodyConfig.rtttl),
              "bullseye:d=16,o=5,b=180:c,e,g,c6");
  CHECK(localBuzzer.configureBullseyeRtttl(bullseyeMelodyConfig.rtttl));
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
  hostMillis = 1000;
  markScaleWorkerProgress();
  bullseyeTracker.arm(36, hostMillis, 3000, 10);
  currentWeight = 36.0f;
  currentWeightSequence = 11;
  currentWeightReceivedAtMs = 1100;
  hostMillis = 1100;
  serviceBullseyeMelody();
  CHECK(bullseyeTracker.pending);
  currentWeightSequence = 12;
  currentWeightReceivedAtMs = 2100;
  hostMillis = 2100;
  markScaleWorkerProgress();
  serviceBullseyeMelody();
  CHECK(!bullseyeTracker.pending);
  CHECK(localBuzzer.activeCue == BuzzerCue::BULLSEYE);

  localBuzzer.stopAll();
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
  bullseyeTracker.arm(36, hostMillis, 3000, currentWeightSequence);
  ++currentWeightSequence;
  currentWeightReceivedAtMs = hostMillis + 100;
  hostMillis += 100;
  serviceBullseyeMelody();
  CHECK(!bullseyeTracker.pending);
  CHECK(localBuzzer.activeCue == BuzzerCue::NONE);
}

void w99b_passive_cue_drives_ledc_note_frequency() {
  resetHarness(false, false);
  const BuzzerToneCommand cmd =
      deriveBuzzerTone(AlertEvent::TARE, false, 0, 0);
  CHECK(cmd.valid);
  CHECK(cmd.cue == BuzzerCue::TARE);
  CHECK(localBuzzer.requestTone(cmd));
  CHECK(localBuzzer.activeCue == BuzzerCue::TARE);
  CHECK(localBuzzer.rtttlPlayback);
  CHECK(hostLedcLastFreq >= 780 && hostLedcLastFreq <= 790);
  CHECK(hostPinLevel[BUZZER_GPIO] == HIGH);
}

void w99c_select_alert_sink_preserves_channel_rules() {
  AlertChannelContext ctx;
  ctx.soundAlertsEnabled = true;
  ctx.buzzerSupportEnabled = true;
  ctx.buzzerReady = true;
  ctx.channel = AlertOutputChannel::SCALE_ONLY;
  CHECK(selectAlertSink(AlertKind::Independent, AlertEvent::SCALE_LOST, ctx) ==
        AlertSink::None);
  ctx.channel = AlertOutputChannel::BUZZER_ONLY;
  CHECK(selectAlertSink(AlertKind::Independent, AlertEvent::SCALE_LOST, ctx) ==
        AlertSink::Buzzer);
  ctx.scaleAvailable = true;
  ctx.scaleSupportsIndependentBeep = true;
  ctx.channel = AlertOutputChannel::SCALE_PRIORITY;
  CHECK(selectAlertSink(AlertKind::Independent, AlertEvent::FIRST_DROP, ctx) ==
        AlertSink::Scale);
  ctx.channel = AlertOutputChannel::BUZZER_ONLY;
  CHECK(selectAlertSink(AlertKind::CommandImmediate, AlertEvent::START_TIMER,
                        ctx) == AlertSink::Buzzer);
  ctx.channel = AlertOutputChannel::SCALE_ONLY;
  CHECK(selectAlertSink(AlertKind::CommandImmediate, AlertEvent::START_TIMER,
                        ctx) == AlertSink::None);
  ctx.soundAlertsEnabled = false;
  ctx.channel = AlertOutputChannel::BUZZER_ONLY;
  CHECK(selectAlertSink(AlertKind::Independent, AlertEvent::FIRST_DROP, ctx) ==
        AlertSink::None);
  CHECK(selectAlertSink(AlertKind::Recovery, AlertEvent::TARE, ctx) ==
        AlertSink::Buzzer);
  ctx.soundAlertsEnabled = true;
  ctx.scaleAvailable = false;
  ctx.channel = AlertOutputChannel::SCALE_PRIORITY;
  CHECK(selectAlertSink(AlertKind::Independent, AlertEvent::FIRST_DROP, ctx) ==
        AlertSink::Buzzer);
  ctx.commandFeedbackExpected = true;
  ctx.commandAttempted = true;
  ctx.writeSucceeded = false;
  CHECK(selectAlertSink(AlertKind::CommandFallback, AlertEvent::START_TIMER,
                        ctx) == AlertSink::Buzzer);
  const BuzzerToneCommand tareTone =
      deriveBuzzerTone(AlertEvent::TARE, false, 0, 0);
  CHECK(tareTone.valid && tareTone.cue == BuzzerCue::TARE);
  const BuzzerToneCommand endTone =
      deriveBuzzerTone(AlertEvent::COMPLETION_EXTRA, false, 0, 0);
  CHECK(endTone.valid && endTone.cue == BuzzerCue::SHOT_END);
  const BuzzerToneCommand lostTone =
      deriveBuzzerTone(AlertEvent::SCALE_LOST, false, 0, 0);
  CHECK(lostTone.valid && lostTone.cue == BuzzerCue::SCALE_LOST);
  const BuzzerToneCommand pulseOff =
      deriveBuzzerTone(AlertEvent::EXTENDED_PULSE, false, 0, 0);
  CHECK(!pulseOff.valid);
  const BuzzerToneCommand pulseFast = deriveBuzzerTone(
      AlertEvent::EXTENDED_PULSE, false,
      static_cast<uint8_t>(ExtendedPulseRate::FAST), 0);
  CHECK(pulseFast.valid && pulseFast.cue == BuzzerCue::ABNORMAL_FAST &&
        pulseFast.pulseRate == static_cast<uint8_t>(ExtendedPulseRate::FAST) &&
        pulseFast.looping);
}

void w99d_pending_rtttl_starts_from_preparsed_catalog() {
  resetHarness(false, false);
  const BuzzerToneCommand tare =
      deriveBuzzerTone(AlertEvent::TARE, false, 0, 0);
  const BuzzerToneCommand end =
      deriveBuzzerTone(AlertEvent::COMPLETION_EXTRA, false, 0, 0);
  CHECK(localBuzzer.requestTone(tare));
  CHECK(localBuzzer.activeCue == BuzzerCue::TARE);
  CHECK(localBuzzer.requestTone(end));
  CHECK(localBuzzer.pendingCue == BuzzerCue::SHOT_END);
  localBuzzer.stopIfCue(BuzzerCue::TARE);
  CHECK(localBuzzer.activeCue == BuzzerCue::SHOT_END);
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  CHECK(localBuzzer.rtttlPlayback);
  CHECK(localBuzzer.beepCount ==
        localBuzzer.cueNoteCount[static_cast<uint8_t>(BuzzerCue::SHOT_END)]);
#endif
}

void w99e_recovery_cue_bypasses_mute_via_pipeline() {
  resetHarness(false, false);
  runtimeConfig.soundAlertsMuted = true;
  CHECK(!soundAlertsEnabled());
  playRecoveryCue(BuzzerCue::RECOVERY_START);
  CHECK(localBuzzer.activeCue == BuzzerCue::RECOVERY_START);
  CHECK(localBuzzer.rtttlPlayback);
}

void w39_history_mutation_blocked_while_brew_rf_active() {
  ControlStatusSnapshot status;
  CHECK(controlAllowsHistoryMutation(status));
  status.activeCycle = true;
  CHECK(!controlAllowsHistoryMutation(status));
  status.activeCycle = false;
  status.machineRunning = true;
  CHECK(!controlAllowsHistoryMutation(status));
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
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
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(session.startedWithScale);
  CHECK(session.automaticEnabled);
  CHECK(stopperState == StopperState::BREW);
  CHECK(!session.receivedFreshWeightInCycle);
  CHECK(getRelaySafetySnapshot().closed);
}

void r22_confirmed_implausible_weight_does_not_stop() {
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
  CHECK(!session.directStopPending);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
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
  CHECK(plannedRestartHeld);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
}

void w86_config_applies_to_ram_immediately_and_coalesces() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t firstRevision = runtimeConfig.revision;
  const uint8_t originalMode = runtimeConfig.noScaleBbwMode;
  const uint8_t alternateMode = originalMode ==
                                        static_cast<uint8_t>(NoScaleBbwMode::OFF)
                                    ? static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE)
                                    : static_cast<uint8_t>(NoScaleBbwMode::OFF);
  const uint32_t originalCooldown = runtimeConfig.lastShotCooldownMs;
  const bool originalAutoTare = runtimeConfig.autoTare;
  WebCommand first;
  first.type = WebCommandType::APPLY_CONFIG;
  first.config = runtimeConfig;
  first.config.noScaleBbwMode = alternateMode;
  first.config.dripDelayMs = 4200;
  first.config.postTareBaselineGraceMs = 3500;
  processWebCommand(first);
  CHECK(!maintenanceLease.active);
  CHECK(runtimeConfig.noScaleBbwMode == alternateMode);
  CHECK(runtimeConfig.lastShotCooldownMs == originalCooldown);
  CHECK(runtimeConfig.autoTare == originalAutoTare);
  CHECK(runtimeConfig.dripDelayMs == 4200);
  CHECK(runtimeConfig.postTareBaselineGraceMs == 3500);
  CHECK(runtimeConfig.revision == firstRevision + 1);
  CHECK(runtimePersistPending);

  WebCommand second;
  second.type = WebCommandType::APPLY_CONFIG;
  second.config = runtimeConfig;
  second.config.lastShotCooldownMs = runtimeConfig.lastShotCooldownMs;
  second.config.noScaleBbwMode = originalMode;
  processWebCommand(second);
  CHECK(runtimeConfig.noScaleBbwMode == originalMode);
  CHECK(runtimeConfig.revision == firstRevision + 2);
  CHECK(runtimePersistPending);
  CHECK(!maintenanceLease.active);

  setRawPaddle(true);
  CHECK(runtimeConfig.noScaleBbwMode == originalMode);
  CHECK(runtimeConfig.revision == firstRevision + 2);
}

void w87_nvs_fail_keeps_ram_and_requeues() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t firstRevision = runtimeConfig.revision;
  const uint8_t originalMode = runtimeConfig.noScaleBbwMode;
  const uint8_t alternateMode = originalMode ==
                                        static_cast<uint8_t>(NoScaleBbwMode::OFF)
                                    ? static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE)
                                    : static_cast<uint8_t>(NoScaleBbwMode::OFF);
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.noScaleBbwMode = alternateMode;
  processWebCommand(update);
  CHECK(runtimeConfig.noScaleBbwMode == alternateMode);
  CHECK(runtimeConfig.revision == firstRevision + 1);
  CHECK(runtimePersistPending);

  hostRuntimePersistSucceeds = false;
  runLoopAfter(RUNTIME_PERSIST_DEBOUNCE_MS + 1);
  CHECK(runtimeConfig.noScaleBbwMode == alternateMode);
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
  CHECK(hostLastFlushedRuntime.noScaleBbwMode == alternateMode);
}

void w88_save_network_flush_includes_live_runtime() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const uint32_t firstRevision = runtimeConfig.revision;
  WebCommand apply;
  apply.type = WebCommandType::APPLY_CONFIG;
  apply.config = runtimeConfig;
  apply.config.noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
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
  CHECK(hostLastFlushedRuntime.noScaleBbwMode ==
        runtimeConfig.noScaleBbwMode);
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

void r24_web_control_is_available_without_session_owner() {
  resetHarness(false, false);
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::REMOTE_ON));
  CHECK(session.active);
  CHECK(session.source == ControlSource::WEB);
  CHECK(getRelaySafetySnapshot().closed);

  WebCommand paddleOff = webControlCommand(WebCommandType::REMOTE_OFF);
  processWebCommand(paddleOff);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);

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

void r27_platform_clock_failure_prevents_circuit_close() {
  resetHarness(false, false);
  reachReadyFromBoot();
  platformClockReady = false;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::LOCKOUT);
  CHECK(getRelaySafetySnapshot().fault ==
        RelaySafetyFault::INITIALIZATION_FAILED);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
}

void r28_terminal_control_result_is_retained_until_forwarded() {
  resetHarness(false, false);
  reachReadyFromBoot();
  processWebCommand(webControlCommand(WebCommandType::REMOTE_ON));
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

void r33_weight_mailbox_keeps_latest_without_consumer_gap() {
  resetHarness(false, true);
  reachReadyFromBoot();
  ScaleEvent first;
  first.type = ScaleEventType::WEIGHT;
  first.receivedAtMs = hostMillis;
  first.weightG = 1.0f;
  ScaleEvent latest = first;
  latest.weightG = 2.0f;
  CHECK(publishScaleEvent(first, false));
  debugLog.clear();
  CHECK(publishScaleEvent(latest, false));
  CHECK(scalePacketGaps == 0);
  CHECK(!debugEventExists(DebugCode::SCALE_PACKET_GAP));
  latest.weightG = 3.0f;
  CHECK(publishScaleEvent(latest, false));
  CHECK(scalePacketGaps == 0);
  processScaleWorkerEvents();
  CHECK(observedWeight == 3.0f);
}

void r33b_stream_gap_counts_connected_inter_packet_silence() {
  resetHarness(false, true);
  reachReadyFromBoot();
  ScaleEvent sample;
  sample.type = ScaleEventType::WEIGHT;
  sample.receivedAtMs = hostMillis;
  sample.weightG = 1.0f;
  CHECK(publishScaleEvent(sample, false));
  CHECK(scalePacketGaps == 0);

  hostMillis += SCALE_STREAM_GAP_MS;
  sample.receivedAtMs = hostMillis;
  sample.weightG = 1.1f;
  debugLog.clear();
  CHECK(publishScaleEvent(sample, false));
  CHECK(scalePacketGaps == 0);

  hostMillis += SCALE_STREAM_GAP_MS + 1;
  sample.receivedAtMs = hostMillis;
  sample.weightG = 1.2f;
  CHECK(publishScaleEvent(sample, false));
  CHECK(scalePacketGaps == 1);
  CHECK(debugEventExists(DebugCode::SCALE_PACKET_GAP));

  debugLog.clear();
  hostMillis += SCALE_STREAM_GAP_MS + 1;
  sample.receivedAtMs = hostMillis;
  sample.weightG = 1.3f;
  CHECK(publishScaleEvent(sample, false));
  CHECK(scalePacketGaps == 2);
  CHECK(!debugEventExists(DebugCode::SCALE_PACKET_GAP));
  hostMillis += SCALE_PACKET_GAP_LOG_MIN_MS;
  sample.receivedAtMs = hostMillis;
  sample.weightG = 1.4f;
  CHECK(publishScaleEvent(sample, false));
  CHECK(debugEventExists(DebugCode::SCALE_PACKET_GAP));

  setScaleConnected(false);
  hostMillis += 5000;
  setScaleConnected(true);
  sample.receivedAtMs = hostMillis;
  sample.weightG = 2.0f;
  const uint32_t gapsBeforeReconnect = scalePacketGaps;
  CHECK(publishScaleEvent(sample, false));
  CHECK(scalePacketGaps == gapsBeforeReconnect);

  processScaleWorkerEvents();
  CHECK(observedWeight == 2.0f);
}

void r33c_weight_update_interval_is_smoothed_and_reset_safely() {
  resetHarness(false, true);
  reachReadyFromBoot();
  ScaleEvent sample;
  sample.type = ScaleEventType::WEIGHT;
  sample.receivedAtMs = hostMillis;
  sample.weightG = 1.0f;
  CHECK(publishScaleEvent(sample, false));
  CHECK(getScaleLinkSnapshot().weightUpdateIntervalMs == 0);

  sample.receivedAtMs += 100;
  CHECK(publishScaleEvent(sample, false));
  CHECK(getScaleLinkSnapshot().weightUpdateIntervalMs == 100);

  sample.receivedAtMs += 108;
  CHECK(publishScaleEvent(sample, false));
  CHECK(getScaleLinkSnapshot().weightUpdateIntervalMs == 101);

  sample.receivedAtMs += SCALE_STREAM_GAP_MS + 1;
  CHECK(publishScaleEvent(sample, false));
  CHECK(getScaleLinkSnapshot().weightUpdateIntervalMs == 0);

  setScaleConnected(false);
  setScaleConnected(true);
  sample.receivedAtMs += 100;
  CHECK(publishScaleEvent(sample, false));
  CHECK(getScaleLinkSnapshot().weightUpdateIntervalMs == 0);
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

void r35_connected_without_weight_stream_is_not_available() {
  resetHarness(false, true);
  reachReadyFromBoot();
  observedWeightSequence = 0;
  scaleWorkerTaskHandle = reinterpret_cast<TaskHandle_t>(1);
  CHECK(getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED);
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
  setScaleConnected(false);
  setScaleConnected(true);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(!status.currentWeightValid);
}

void r36_recovered_stale_metrics_count_connected_gaps_only() {
  ControlStatusSnapshot status;

  resetHarness(false, true);
  reachReadyFromBoot();
  publishWeight(12.0f);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::FRESH);
  CHECK(status.scaleRecoveredStaleCount == 0);
  CHECK(status.scaleRecoveredStaleMs == 0);

  hostMillis += MAX_AUTOMATION_WEIGHT_AGE_MS + 100;
  markScaleWorkerProgress();
  loop();
  CHECK(telemetryWeightStreamState == WeightStreamState::STALE);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::STALE);
  CHECK(status.scaleRecoveredStaleCount == 0);
  CHECK(status.scaleRecoveredStaleMs == 0);

  hostMillis += 250;
  markScaleWorkerProgress();
  publishWeight(12.1f);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::FRESH);
  CHECK(status.scaleRecoveredStaleCount == 1);
  CHECK(status.scaleRecoveredStaleMs == 250);

  hostMillis += MAX_AUTOMATION_WEIGHT_AGE_MS + 100;
  markScaleWorkerProgress();
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::STALE);
  CHECK(status.scaleRecoveredStaleCount == 1);
  hostMillis += 400;
  markScaleWorkerProgress();
  publishWeight(12.2f);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::FRESH);
  CHECK(status.scaleRecoveredStaleCount == 2);
  CHECK(status.scaleRecoveredStaleMs == 650);

  resetHarness(false, true);
  reachReadyFromBoot();
  publishWeight(12.0f);
  publishControlStatus();
  hostMillis += MAX_AUTOMATION_WEIGHT_AGE_MS + 100;
  markScaleWorkerProgress();
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::STALE);
  setScaleConnected(false);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.scaleRecoveredStaleCount == 0);
  CHECK(status.scaleRecoveredStaleMs == 0);

  setScaleConnected(true);
  publishWeight(12.0f);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::FRESH);
  CHECK(status.scaleRecoveredStaleCount == 0);

  resetHarness(false, true);
  reachReadyFromBoot();
  publishWeight(12.0f);
  publishControlStatus();
  setScaleConnected(false);
  publishControlStatus();
  setScaleConnected(true);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::STALE);
  CHECK(status.scaleRecoveredStaleCount == 0);
  publishWeight(12.0f);
  publishControlStatus();
  copyControlStatus(status);
  CHECK(status.weightStreamState == WeightStreamState::FRESH);
  CHECK(status.scaleRecoveredStaleCount == 0);
  CHECK(status.scaleRecoveredStaleMs == 0);
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
  DebugEvent circuit = {};
  circuit.code = DebugCode::CIRCUIT_ARM_FAILED;
  circuit.argument1 = static_cast<int32_t>(CircuitArmFailReason::SAFETY_LOCKOUT);
  CHECK(formatLifecycleDebugMessage(circuit, message, sizeof(message)));
  CHECK(std::string(message).find("safety lockout") != std::string::npos);
  CHECK(debugCodeDefaultLevel(DebugCode::CIRCUIT_ARM_FAILED) ==
        LogLevel::CRITICAL);
  CHECK(debugCodeDefaultLevel(DebugCode::SCALE_CONNECTING) == LogLevel::DEBUG);
  CHECK(!logLevelAtMost(LogLevel::CRITICAL, LogLevel::NONE));
  CHECK(!logLevelAtMost(LogLevel::DEBUG, LogLevel::NONE));
  CHECK(logLevelAtMost(LogLevel::WARNING, LogLevel::INFO));
  CHECK(logLevelAtMost(LogLevel::INFO, LogLevel::INFO));
  CHECK(!logLevelAtMost(LogLevel::DEBUG, LogLevel::INFO));
}

void w25c_ring_retain_none_skips_ring_and_error_filters() {
  resetHarness(false, true);
  reachReadyFromBoot();
  debugLog.clear();
  ringRetainLogLevel = LogLevel::NONE;
  serialLogLevel = LogLevel::NONE;
  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_STARTED);
  addDebugEvent(DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
                static_cast<int32_t>(CircuitArmFailReason::SAFETY_LOCKOUT));
  DebugEvent noneEvents[DEBUG_EVENT_CAPACITY] = {};
  CHECK(copyDebugEvents(0, noneEvents, DEBUG_EVENT_CAPACITY) == 0);

  debugLog.clear();
  ringRetainLogLevel = LogLevel::ERROR;
  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_STARTED);
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECTING);
  addDebugEvent(DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
                static_cast<int32_t>(CircuitArmFailReason::SAFETY_LOCKOUT));
  DebugEvent errorEvents[DEBUG_EVENT_CAPACITY] = {};
  const size_t count =
      copyDebugEvents(0, errorEvents, DEBUG_EVENT_CAPACITY);
  CHECK(count == 1);
  CHECK(errorEvents[0].code == DebugCode::CIRCUIT_ARM_FAILED);
  CHECK(errorEvents[0].level == LogLevel::CRITICAL);
}

void r41_negative_weight_in_range_starts_automatic_cycle() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.requireCupToStart = false;
  mutableActiveShotPreset(presetBank).requireCupToStart = false;
  currentWeight = -236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(session.startedWithScale);
  CHECK(session.awaitingPostTareBaseline);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
}

void enableCupStartGuardForTest() {
  runtimeConfig.cupProtectionEnabled = true;
  runtimeConfig.requireCupToStart = true;
  mutableActiveShotPreset(presetBank).cupProtectionEnabled = true;
  mutableActiveShotPreset(presetBank).requireCupToStart = true;
}

void attemptBlockedCupStart() {
  CHECK(stopperState == StopperState::READY);
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(cupStartGuardHold);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(!cupStartGuardHold);
  CHECK(debugEventExists(DebugCode::CUP_START_GUARD_BLOCKED));
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
}

void cp01_zero_pre_tare_weight_blocks_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  attemptBlockedCupStart();
}

void cp02_negative_pre_tare_weight_blocks_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  currentWeight = -50.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  attemptBlockedCupStart();
}

void cp03_positive_pre_tare_weight_starts_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  seedCupPresence(80.0f);
  startCycle();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
}

void cp04_require_cup_off_allows_zero_start() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(stopperState == StopperState::BREW);
}

void cp05_rinse_without_cup_still_starts() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  const uint32_t rawOnAt = hostMillis;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(cupStartGuardHold);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps);
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(!cupStartGuardHold);
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
}

void cp06_cup_removed_stops_during_bbw_protection() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(!session.bbwProtectionEnded);
  publishWeight(-80.0f, hostMillis, 1, 40);
  publishWeight(-81.0f, hostMillis + 1, 1, 41);
  runLoopAfter(1);
  CHECK(session.endReason == EndReason::CUP_REMOVED);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(debugEventExists(DebugCode::CUP_REMOVED_CONFIRMED));
  CHECK(shotLogStopDetailFromEndReason(EndReason::CUP_REMOVED, true, false) ==
        ShotLogStopDetail::CUP_REMOVED);
  CHECK(shotLogCutFromEndReason(EndReason::CUP_REMOVED) == ShotLogCut::AUTO);
}

void cp07_zero_after_tare_does_not_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(0.0f, hostMillis, 1, 50);
  publishWeight(0.1f, hostMillis + 1, 1, 51);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp08_stop_if_cup_removed_off_keeps_negative_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.stopIfCupRemoved = false;
  mutableActiveShotPreset(presetBank).stopIfCupRemoved = false;
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-80.0f, hostMillis, 1, 40);
  publishWeight(-81.0f, hostMillis + 1, 1, 41);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp09_blocked_cup_start_is_silent_when_alerts_off() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  runtimeConfig.soundAlertsMuted = true;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  CHECK(debugEventExists(DebugCode::CUP_START_GUARD_BLOCKED));
  CHECK(localBuzzer.acceptedRequests == beforeBeeps);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
}

void cp10_web_paddle_blocks_cup_start_with_double_beep() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  const uint32_t beforeBeeps = localBuzzer.acceptedRequests;
  processWebCommand(webControlCommand(WebCommandType::REMOTE_ON));
  CHECK(stopperState == StopperState::READY);
  CHECK(!session.active);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(debugEventExists(DebugCode::CUP_START_GUARD_BLOCKED));
  CHECK(localBuzzer.acceptedRequests == beforeBeeps + 1);
}

void cp11_master_off_allows_zero_pre_tare_start() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  runtimeConfig.cupProtectionEnabled = false;
  mutableActiveShotPreset(presetBank).cupProtectionEnabled = false;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
}

void cp12_master_off_does_not_stop_on_negative_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.cupProtectionEnabled = false;
  mutableActiveShotPreset(presetBank).cupProtectionEnabled = false;
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-80.0f, hostMillis, 1, 40);
  publishWeight(-81.0f, hostMillis + 1, 1, 41);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp13_pre_tare_negative_packets_do_not_abort() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.requireCupToStart = false;
  mutableActiveShotPreset(presetBank).requireCupToStart = false;
  currentWeight = -236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(session.awaitingPostTareBaseline);
  publishWeight(-236.0f, hostMillis, 1, 2);
  publishWeight(-236.0f, hostMillis + 1, 1, 3);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  CHECK(session.awaitingPostTareBaseline);
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  publishWeight(-0.2f, hostMillis, 1, 10);
  publishWeight(-0.2f, hostMillis + 1, 1, 11);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  publishWeight(-80.0f, hostMillis, 1, 40);
  publishWeight(-81.0f, hostMillis + 1, 1, 41);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp14_post_tare_noise_does_not_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-0.2f, hostMillis, 1, 50);
  publishWeight(-0.2f, hostMillis + 1, 1, 51);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp15_timer_only_ignores_negative_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.timerOnly = true;
  seedCupPresence(80.0f);
  startCycle();
  CHECK(session.config.timerOnly);
  publishWeight(-80.0f, hostMillis, 1, 40);
  publishWeight(-81.0f, hostMillis + 1, 1, 41);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp16_small_negative_noise_does_not_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-2.9f, hostMillis, 1, 60);
  publishWeight(-2.9f, hostMillis + 1, 1, 61);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
}

void cp17_weight_at_removed_threshold_stops() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-3.0f, hostMillis, 1, 70);
  publishWeight(-3.1f, hostMillis + 1, 1, 71);
  runLoopAfter(1);
  CHECK(session.endReason == EndReason::CUP_REMOVED);
}

void cp18_custom_removed_threshold_is_honored() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.cupRemovedWeightG = -10.0f;
  mutableActiveShotPreset(presetBank).cupRemovedWeightG = -10.0f;
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-5.0f, hostMillis, 1, 80);
  publishWeight(-5.0f, hostMillis + 1, 1, 81);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  publishWeight(-10.0f, hostMillis + 2, 1, 82);
  publishWeight(-10.1f, hostMillis + 3, 1, 83);
  runLoopAfter(1);
  CHECK(session.endReason == EndReason::CUP_REMOVED);
}

void cp19_weight_below_present_threshold_blocks_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  currentWeight = 5.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  publishStableCupWeight(5.0f, 1);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  attemptBlockedCupStart();
}

void cp20_weight_at_present_threshold_starts_brew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  seedCupPresence(10.0f);
  startCycle();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
}

void cp21_custom_present_threshold_is_honored() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableCupStartGuardForTest();
  runtimeConfig.minimumCupWeightG = 8.0f;
  currentWeight = 7.8f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  publishStableCupWeight(7.8f, 1);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  attemptBlockedCupStart();
  seedCupPresence(8.0f);
  startCycle();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
}

void cup_fsm_boot_zero_is_absent() {
  resetHarness(false, true);
  reachReadyFromBoot();
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  publishWeight(0.0f, hostMillis, 1, 1);
  publishWeight(-0.2f, hostMillis + 50, 1, 2);
  publishWeight(2.0f, hostMillis + 100, 1, 3);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
}

void cup_fsm_stable_min_cup_is_present() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
}

void cup_fsm_noise_does_not_remove() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  notifyCupPresenceTare();
  holdCupPresenceTransitions(false);
  publishWeight(0.0f, hostMillis, 1, 20);
  publishWeight(-0.2f, hostMillis + 50, 1, 21);
  publishWeight(-0.2f, hostMillis + 51, 1, 22);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
}

void cup_fsm_spike_does_not_place() {
  resetHarness(false, true);
  reachReadyFromBoot();
  publishWeight(28.0f, hostMillis + 100, 1, 10);
  publishWeight(6.0f, hostMillis + 200, 1, 11);
  publishWeight(5.0f, hostMillis + 300, 1, 12);
  publishWeight(5.0f, hostMillis + 400, 1, 13);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
}

void cup_fsm_untared_lift_to_zero_is_removed() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  publishWeight(0.0f, hostMillis, 1, 20);
  publishWeight(0.0f, hostMillis + 50, 1, 21);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
}

void cup_fsm_tare_does_not_change_state() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  notifyCupPresenceTare();
  holdCupPresenceTransitions(false);
  publishStableCupWeight(0.0f, 30);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  publishWeight(0.0f, hostMillis, 1, 35);
  publishWeight(-0.2f, hostMillis + 50, 1, 36);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  publishWeight(-80.0f, hostMillis, 1, 40);
  publishWeight(-81.0f, hostMillis + 1, 1, 41);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  notifyCupPresenceTare();
  publishStableCupWeight(0.0f, 50);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
}

void cup_fsm_put_back_without_tare_is_present() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  notifyCupPresenceTare();
  holdCupPresenceTransitions(false);
  publishWeight(0.0f, hostMillis, 1, 20);
  publishWeight(-80.0f, hostMillis + 50, 1, 40);
  publishWeight(-81.0f, hostMillis + 51, 1, 41);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  publishStableCupWeight(0.0f, 50);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
}

void cup_fsm_disconnect_does_not_emit_removed() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  CHECK(session.endReason == EndReason::NONE);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  setScaleConnected(false);
  runLoopAfter(1);
  CHECK(session.endReason != EndReason::CUP_REMOVED);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
}

void cup_fsm_rinse_does_not_freeze_presence() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoTare = true;
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  const uint32_t remaining =
      runtimeConfig.rinseDurationMs - elapsedMs(session.rinseStartedAtMs);
  runLoopAfter(remaining);
  CHECK(stopperState == StopperState::READY);
  seedCupPresence(80.0f);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  enableCupStartGuardForTest();
  startCycle();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
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

void r66_post_tare_rejected_stream_does_not_enforce_atm() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = true;
  reachReadyFromBoot();
  currentWeight = 236.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(session.awaitingPostTareBaseline);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
  hostAutoScaleWorkerProgress = false;
  for (uint32_t waited = 0; waited <= MAX_AUTOMATION_WEIGHT_AGE_MS + 50U;
       waited += 100U) {
    hostMillis += 100U;
    markScaleWorkerProgress();
    publishWeight(236.0f);
    loop();
    verifySafetyInvariants();
  }
  CHECK(session.awaitingPostTareBaseline);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
  publishWeight(0.0f);
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(!session.autoToManualGuardEnforced);
}

void r67_observed_silence_enforces_atm() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = true;
  runtimeConfig.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
  runtimeConfig.autoToManualGuardManualLimitMs = 20000;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  hostAutoScaleWorkerProgress = false;
  for (uint32_t waited = 0; waited <= MAX_AUTOMATION_WEIGHT_AGE_MS + 50U;
       waited += 100U) {
    hostMillis += 100U;
    markScaleWorkerProgress();
    loop();
    verifySafetyInvariants();
  }
  CHECK(session.weightControlState == WeightControlState::SUSPENDED);
  CHECK(session.autoToManualGuardEnforced);
}

void r68_stable_accepted_weight_does_not_suspend() {
  resetHarness(false, true);
  runtimeConfig.autoToManualGuardEnabled = true;
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
  hostAutoScaleWorkerProgress = false;
  for (uint32_t waited = 0; waited <= MAX_AUTOMATION_WEIGHT_AGE_MS + 50U;
       waited += 100U) {
    hostMillis += 100U;
    markScaleWorkerProgress();
    publishWeight(20.0f);
    loop();
    verifySafetyInvariants();
  }
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(session.autoToManualGuardArmed);
  CHECK(!session.autoToManualGuardEnforced);
}

void r69_att_command_harvests_pending_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  scale.weight = 3.25f;
  scale.newWeightAvailableValue = true;
  CHECK(executeNextScaleCommand());
  processScaleWorkerEvents();
  CHECK(fabsf(observedWeight - 3.25f) < 0.001f);
  CHECK(observedWeightIsFresh());
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

void st01_cycle_elapsed_follows_circuit_immediately() {
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

void st02_scale_timer_stop_waits_until_display_catches_internal() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  startCycle();
  CHECK(executeNextScaleCommand());
  CHECK(session.remoteTimerStarted);
  runLoopAfter(4000);
  publishScaleTimer(3000);

  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);
  CHECK(pendingScaleTimerStop.targetMs == 4000U);

  runLoopAfter(0);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  publishScaleTimer(4000);
  runLoopAfter(0);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
}

void st03_scale_timer_stop_when_display_already_at_internal() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  startCycle();
  CHECK(executeNextScaleCommand());
  runLoopAfter(4000);
  publishScaleTimer(4200);
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
}

void st04_scale_timer_stop_extra_delay_applies_after_catchup() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 100;
  startCycle();
  CHECK(executeNextScaleCommand());
  runLoopAfter(4000);
  publishScaleTimer(4200);
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  runLoopAfter(99);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  runLoopAfter(1);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
}

void st05_scale_timer_stop_catchup_times_out() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  startCycle();
  CHECK(executeNextScaleCommand());
  runLoopAfter(4000);
  publishScaleTimer(3000);
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  runLoopAfter(MAX_SCALE_TIMER_STOP_CATCHUP_MS - 1);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  runLoopAfter(1);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
}

void st06_scale_timer_stop_without_valid_timer_is_immediate() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  startCycle();
  CHECK(executeNextScaleCommand());
  runLoopAfter(4000);
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
}

void st07_scale_timer_stop_waits_for_start_before_queueing_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 0;
  startCycle();
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
  CHECK(!session.remoteTimerStartSettled);

  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);
  CHECK(commandCount(ScaleCommandType::START_TIMER_AND_TARE) == 1);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 0);
  CHECK(queuedCommandAt(0).type == ScaleCommandType::START_TIMER_AND_TARE);

  CHECK(executeNextScaleCommand());
  CHECK(session.remoteTimerStartSettled);
  CHECK(session.remoteTimerStarted);
  CHECK(scale.tareStartTimerCalls == 1);
  CHECK(scale.stopTimerCalls == 0);

  runLoopAfter(0);
  CHECK(!pendingScaleTimerStop.pending);
  CHECK(session.stopTimerRequested);
  CHECK(commandCount(ScaleCommandType::STOP_TIMER) == 1);
  CHECK(executeNextScaleCommand());
  CHECK(scale.stopTimerCalls == 1);
}

void st08_scale_timer_stop_extra_delay_applies_without_valid_timer() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.scaleTimerStopExtraDelayMs = 100;
  startCycle();
  CHECK(executeNextScaleCommand());
  finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
  CHECK(pendingScaleTimerStop.pending);
  CHECK(!session.stopTimerRequested);

  runLoopAfter(99);
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
  simulateFirstDrops(0.0f, 20);
  CHECK(session.firstDropMs != 0);
  CHECK(session.flowDuringRetare);
  publishStableCupWeight(150.0f, 30);
  CHECK(!session.retarePerformed);
  CHECK(session.retareFlowFirstDetectedAtMs != 0);
}

void rt11_late_retare_records_first_drops_and_keeps_weight_control() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.firstDropBeep = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  advanceToBrew();
  uint32_t keepAliveSequence = 40;
  while (elapsedMs(session.startedAtMs) <
         runtimeConfig.postTareBaselineGraceMs + 500) {
    publishWeight(0.0f, hostMillis, 1, keepAliveSequence++);
    runLoopAfter(200);
  }
  CHECK(retareWindowOpen());
  publishStableCupWeight(150.0f, 10);
  CHECK(session.retarePerformed);
  CHECK(executeNextScaleCommand());
  CHECK(session.awaitingPostTareBaseline);
  CHECK(session.scaleBaselineReady);
  CHECK(fabsf(session.scaleBaselineG) < 0.001f);
  publishWeight(150.0f, hostMillis + 50, 1, 20);
  CHECK(session.awaitingPostTareBaseline);
  CHECK(session.scaleBaselineReady);
  CHECK(fabsf(session.scaleBaselineG) < 0.001f);
  CHECK(session.firstFlow.phase == FirstFlowPhase::TOUCH);
  CHECK(session.firstDropMs == 0);
  const uint32_t settledAtMs = hostMillis + 100;
  publishWeight(0.0f, settledAtMs, 1, 21);
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(session.scaleBaselineReady);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  publishWeight(0.35f, settledAtMs + 50, 1, 30);
  publishWeight(0.40f, settledAtMs + 150, 1, 31);
  publishWeight(0.45f, settledAtMs + 250, 1, 32);
  CHECK(session.firstDropMs != 0);
  CHECK(scaleBeepPending);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(!session.flowDuringRetare);
}

void rt12_early_retare_then_first_drops_are_recorded() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.firstDropBeep = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  publishStableCupWeight(150.0f, 10);
  CHECK(session.retarePerformed);
  CHECK(executeNextScaleCommand());
  CHECK(session.awaitingPostTareBaseline);
  publishWeight(0.0f, hostMillis + 50, 1, 20);
  CHECK(!session.awaitingPostTareBaseline);
  simulateFirstDrops(0.0f, 30);
  CHECK(session.firstDropMs != 0);
  CHECK(scaleBeepPending);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
}

void rt13_auto_tare_off_skips_automatic_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.autoTare = false;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  publishStableCupWeight(150.0f, 10);
  CHECK(!session.retarePerformed);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 0);
  CHECK(retareWindowOpen());
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

void w38_scale_connected_led_tracks_link_and_setting() {
  resetHarness(false, false);
  initializeScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);
  CHECK(runtimeConfig.scaleConnectedLed);
  CHECK(!getScaleLinkSnapshot().connecting);

  scale.connecting = true;
  updateWorkerLinkState();
  CHECK(getScaleLinkSnapshot().connecting);
  CHECK(getScaleLinkSnapshot().state == ScaleLinkState::DISCONNECTED);
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);
  hostMillis += SCALE_LED_FAST_BLINK_MS;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);
  hostMillis += SCALE_LED_FAST_BLINK_MS;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);

  runtimeConfig.scaleConnectedLed = false;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);
  runtimeConfig.scaleConnectedLed = true;
  scale.connecting = false;
  updateWorkerLinkState();
  CHECK(!getScaleLinkSnapshot().connecting);
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);

  setScaleConnected(true);
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);
  hostMillis += SCALE_LED_FAST_BLINK_MS;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);

  setScaleConnected(false);
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);

  setScaleConnected(true);
  publishWeight(12.0f);
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);
  hostMillis += MAX_AUTOMATION_WEIGHT_AGE_MS + 100;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);
  hostMillis += SCALE_LED_SLOW_BLINK_MS;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);
  hostMillis += SCALE_LED_SLOW_BLINK_MS;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == HIGH);

  runtimeConfig.scaleConnectedLed = false;
  serviceScaleConnectedLed();
  CHECK(hostPinLevel.at(SCALE_CONNECTED_LED_GPIO) == LOW);
}

void s01_shot_log_filters_short_and_rinse() {
  CHECK(!shotLogEligible(EndReason::SHORT_SHOT, 15000));
  CHECK(!shotLogEligible(EndReason::RINSE_COMPLETE, 15000));
  CHECK(!shotLogEligible(EndReason::UNCONFIRMED_START, 60000));
  CHECK(brewEndIsAbandonedStart(EndReason::UNCONFIRMED_START));
  CHECK(!brewEndIsAbandonedStart(EndReason::RINSE_COMPLETE));
  CHECK(!shotLogEligible(EndReason::ACTIVATOR, 9000));
  CHECK(shotLogEligible(EndReason::ACTIVATOR, 10000));
  CHECK(!shotLogBbwEligible(false, false, true));
  CHECK(!shotLogBbwEligible(true, true, true));
  CHECK(!shotLogBbwEligible(true, false, false));
  CHECK(shotLogBbwEligible(true, false, true));
  CHECK(!shotLogWeightEligible(0.9f, true));
  CHECK(shotLogWeightEligible(1.0f, true));
  CHECK(!shotLogWeightEligible(1.0f, false));
}

void s01b_shot_log_stop_detail_names_end_reasons() {
  CHECK(shotLogStopDetailFromEndReason(EndReason::ACTIVATOR, true, false) ==
        ShotLogStopDetail::ACTIVATOR);
  CHECK(shotLogStopDetailFromEndReason(EndReason::WEB_STOP, false, false) ==
        ShotLogStopDetail::WEB_STOP);
  CHECK(shotLogStopDetailFromEndReason(EndReason::PHYSICAL_OVERRIDE, false,
                                       false) ==
        ShotLogStopDetail::PHYSICAL_OVERRIDE);
  CHECK(shotLogStopDetailFromEndReason(EndReason::WEB_HEARTBEAT_TIMEOUT, false,
                                       false) ==
        ShotLogStopDetail::WEB_HEARTBEAT);
  CHECK(shotLogStopDetailFromEndReason(EndReason::GLOBAL_LIMIT, false, false) ==
        ShotLogStopDetail::HARD_LIMIT);
  CHECK(shotLogStopDetailFromEndReason(EndReason::CONFIGURED_WALL_LIMIT, false,
                                       false) ==
        ShotLogStopDetail::WALL_LIMIT);
  CHECK(shotLogStopDetailFromEndReason(EndReason::RELAY_SAFETY_FAILURE, false,
                                       false) ==
        ShotLogStopDetail::RELAY_SAFETY);
  CHECK(shotLogStopDetailFromEndReason(EndReason::WEIGHT_ANOMALY, true, true) ==
        ShotLogStopDetail::WEIGHT_ANOMALY);
  CHECK(shotLogStopDetailFromEndReason(EndReason::SCALE_THRESHOLD, true, true) ==
        ShotLogStopDetail::NORMAL_TARGET);
  CHECK(shotLogStopDetailFromEndReason(EndReason::CUP_REMOVED, true, false) ==
        ShotLogStopDetail::CUP_REMOVED);
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::ACTIVATOR), "activator") == 0);
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::HARD_LIMIT),
               "hard_limit") == 0);
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::WALL_LIMIT),
               "wall_limit") == 0);
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
  pendingFinalize.endReason = EndReason::ACTIVATOR;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs = 120;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  pendingFinalize.endedAtMs = hostMillis;
  runLoopAfter(pendingFinalize.dripDelayMs);
  CHECK(!pendingFinalize.pending);
  CHECK(shotLog.count() == 0);
}

void s02e_shot_log_appends_auto_bbw_after_drip_delay() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.logEligible = true;
  pendingFinalize.startedWithScale = true;
  pendingFinalize.timerOnly = false;
  pendingFinalize.automaticBrew = true;
  pendingFinalize.lastKnownWeightValid = true;
  pendingFinalize.lastKnownWeightG = 36.0f;
  pendingFinalize.finalState = StopperState::BREW;
  pendingFinalize.endReason = EndReason::SCALE_THRESHOLD;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs = 120;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  pendingFinalize.endedAtMs = hostMillis;
  runLoopAfter(pendingFinalize.dripDelayMs);
  CHECK(!pendingFinalize.pending);
  CHECK(shotLog.count() == 1);
  ShotLogRecord records[1] = {};
  CHECK(shotLog.copyNewestFirst(records, 1) == 1);
  CHECK(records[0].durationDs == 120);
  CHECK(records[0].shotType == static_cast<uint8_t>(ShotLogType::AUTO));
  CHECK(records[0].stopDetail ==
        static_cast<uint8_t>(ShotLogStopDetail::NORMAL_TARGET));
  CHECK(records[0].cutType == static_cast<uint8_t>(ShotLogCut::AUTO));
}

void s02f_shot_log_skips_sub_one_gram_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  persistedLastShot = PersistedLastShot{};
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.cycleId = 5;
  pendingFinalize.logEligible = true;
  pendingFinalize.startedWithScale = true;
  pendingFinalize.timerOnly = false;
  pendingFinalize.automaticBrew = true;
  pendingFinalize.lastKnownWeightValid = true;
  pendingFinalize.lastKnownWeightG = 0.5f;
  pendingFinalize.finalState = StopperState::BREW;
  pendingFinalize.endReason = EndReason::SCALE_THRESHOLD;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs = 120;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  pendingFinalize.endedAtMs = hostMillis;
  runLoopAfter(pendingFinalize.dripDelayMs);
  CHECK(!pendingFinalize.pending);
  CHECK(shotLog.count() == 0);
  CHECK(shotCurves.count() == 0);
  // Home last shot is independent of the 1 g history floor.
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.cycleId == 5);
  CHECK(persistedLastShot.weightValid);
  CHECK(fabsf(persistedLastShot.currentWeightG - 0.5f) < 0.001f);
}

void s02g_full_cycle_sub_one_gram_updates_last_shot_not_history() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  persistedLastShot = PersistedLastShot{};
  const uint32_t rawOnAt = startCycle();
  advanceToBrew();
  currentWeight = 0.5f;
  currentWeightSequence = session.weightSequenceAtStart + 1;
  currentWeightReceivedAtMs = hostMillis;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 0.5f;
  acceptWeightIntoTrajectory(0.5f, hostMillis, currentWeightSequence);
  releaseAtPhysicalDuration(rawOnAt, 12000);
  // Last shot updates immediately at cycle end (before drip delay).
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.weightValid);
  CHECK(fabsf(persistedLastShot.currentWeightG - 0.5f) < 0.001f);
  CHECK(shotLog.count() == 0);
  runLoopAfter(runtimeConfig.dripDelayMs);
  CHECK(shotLog.count() == 0);
  CHECK(shotCurves.count() == 0);
  CHECK(persistedLastShot.valid);
  CHECK(fabsf(persistedLastShot.currentWeightG - 0.5f) < 0.001f);
}

void s02c_shot_curve_samples_on_two_second_grid_and_latches_slow() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  session.active = true;
  session.startedWithScale = true;
  session.hasWeightAnchor = true;
  session.startedAtMs = hostMillis;
  session.circuitClosedAtMs = hostMillis;
  resetShotTrajectory(hostMillis);
  CHECK(shotCurveSampler.count == 0);
  acceptWeightIntoTrajectory(0.2f, hostMillis, 1);
  CHECK(shotCurveSampler.count == 1);
  CHECK(shotCurveSampler.weightCg[0] == 20);
  hostMillis += 2100;
  acceptWeightIntoTrajectory(8.5f, hostMillis, 2);
  CHECK(shotCurveSampler.count == 2);
  CHECK(shotCurveSampler.weightCg[1] == 850);
  enterSlowExtractionExtended(12.0f, hostMillis);
  CHECK(shotCurveSampler.extended.atDs == 21);
  CHECK(shotCurveSampler.extended.weightCg == 1200);
  hostMillis += 4000;
  acceptWeightIntoTrajectory(18.0f, hostMillis, 3);
  session.lastAcceptedWeightG = 18.0f;
  shot.automaticBrew = true;
  session.config.timerOnly = false;
  schedulePendingShotFinalize(EndReason::SLOW_EXTRACTION_MAX_TIME, 12000);
  CHECK(pendingFinalize.curve.count >= 2);
  CHECK(pendingFinalize.curve.extended.atDs == 21);
  CHECK(pendingFinalize.curve.extended.weightCg == 1200);
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.dripDelayMs = 0;
  runLoopAfter(0);
  CHECK(shotLog.count() == 1);
  CHECK(shotCurves.count() == 1);
  ShotCurveRecord curves[1] = {};
  CHECK(shotCurves.copyNewestFirst(curves, 1) == 1);
  CHECK(curves[0].shotId != 0);
  CHECK(curves[0].count >= 2);
  CHECK(curves[0].intervalS == SHOT_CURVE_INTERVAL_S);
}

void s02d_shot_curve_latches_first_drop_fast_and_atm() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  session.active = true;
  session.startedWithScale = true;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 0.5f;
  session.startedAtMs = hostMillis;
  session.circuitClosedAtMs = hostMillis;
  session.autoToManualGuardArmed = true;
  session.autoToManualGuardEnforced = false;
  session.weightControlState = WeightControlState::ACTIVE;
  resetShotTrajectory(hostMillis);
  const uint32_t t0 = hostMillis;
  acceptWeightIntoTrajectory(0.5f, hostMillis, 1);
  hostMillis = t0 + 4500;
  session.lastAcceptedWeightG = 4.0f;
  onFirstDropsDetected(hostMillis);
  CHECK(shotCurveSampler.firstDrop.atDs == 45);
  CHECK(shotCurveSampler.firstDrop.weightCg == 400);
  hostMillis = t0 + 13300;
  session.lastAcceptedWeightG = 36.0f;
  acceptWeightIntoTrajectory(36.0f, hostMillis, 2);
  enterFastExtractionExtended(36.0f, hostMillis);
  CHECK(shotCurveSampler.extended.atDs == 133);
  CHECK(shotCurveSampler.extended.weightCg == 3600);
  hostMillis = t0 + 14000;
  setWeightControlState(WeightControlState::SUSPENDED);
  CHECK(shotCurveEventPresent(shotCurveSampler.atm));
  CHECK(shotCurveSampler.atm.atDs == 140);
  CHECK(shotCurveSampler.atm.weightCg == 3600);
  hostMillis = t0 + 14500;
  setWeightControlState(WeightControlState::ACTIVE);
  CHECK(shotCurveSampler.atmClearedDs == 145);
  hostMillis = t0 + 15100;
  session.lastAcceptedWeightG = 43.7f;
  shot.automaticBrew = true;
  session.config.timerOnly = false;
  schedulePendingShotFinalize(EndReason::FAST_EXTRACTION_MAX_WEIGHT, 15100);
  CHECK(pendingFinalize.curve.firstDrop.atDs == 45);
  CHECK(pendingFinalize.curve.extended.atDs == 133);
  CHECK(pendingFinalize.curve.atm.atDs == 140);
  CHECK(pendingFinalize.curve.atmClearedDs == 145);
  CHECK(pendingFinalize.curve.ended.atDs == 151);
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.dripDelayMs = 0;
  runLoopAfter(0);
  CHECK(shotLog.count() == 1);
  CHECK(shotCurves.count() == 1);
  ShotCurveRecord curves[1] = {};
  CHECK(shotCurves.copyNewestFirst(curves, 1) == 1);
  CHECK(curves[0].firstDrop.atDs == 45);
  CHECK(curves[0].extended.atDs == 133);
  CHECK(curves[0].atm.atDs == 140);
  CHECK(curves[0].atmClearedDs == 145);
  CHECK(curves[0].ended.atDs == 151);

  session.extractionExtended = false;
  session.autoToManualGuardEnforced = false;
  session.autoToManualGuardArmed = true;
  session.weightControlState = WeightControlState::ACTIVE;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 28.0f;
  resetShotTrajectory(hostMillis);
  const uint32_t t1 = hostMillis;
  acceptWeightIntoTrajectory(28.0f, hostMillis, 10);
  hostMillis = t1 + 12000;
  setWeightControlState(WeightControlState::SUSPENDED);
  CHECK(shotCurveSampler.atm.atDs == 120);
  CHECK(shotCurveSampler.atmClearedDs == SHOT_LOG_METRIC_MISSING);
  hostMillis = t1 + 18000;
  shot.automaticBrew = true;
  session.config.timerOnly = false;
  schedulePendingShotFinalize(EndReason::AUTO_TO_MANUAL_GUARD, 18000);
  CHECK(pendingFinalize.curve.atm.atDs == 120);
  CHECK(pendingFinalize.curve.atmClearedDs == SHOT_LOG_METRIC_MISSING);
  CHECK(pendingFinalize.curve.ended.atDs == 180);
}

void s02h_fast_guard_keeps_sampling_and_settled_weight_replaces_endpoint() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();

  session.active = true;
  session.startedWithScale = true;
  session.config = snapshotConfig(runtimeConfig);
  session.config.timerOnly = false;
  session.config.avoidAccidentalTouchEnabled = false;
  session.weightControlState = WeightControlState::ACTIVE;
  session.ownedConnectionGeneration = 1;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 34.0f;
  session.startedAtMs = hostMillis;
  session.circuitClosedAtMs = hostMillis;
  stopperState = StopperState::BREW;
  resetShotTrajectory(hostMillis);
  const uint32_t t0 = hostMillis;

  CHECK(recordWeightSampleWithProvenance(34.0f, t0, 1, 1));
  hostMillis = t0 + 25800;
  enterFastExtractionExtended(36.0f, hostMillis);
  CHECK(shotCurveSampler.extended.atDs == 258);
  const size_t beforeFastSample = shot.datapoints;
  CHECK(recordWeightSampleWithProvenance(38.0f, t0 + 26000, 2, 1));
  CHECK(shot.datapoints == beforeFastSample + 1U);
  CHECK(fabsf(session.lastAcceptedWeightG - 38.0f) < 0.001f);

  hostMillis = t0 + 28000;
  CHECK(recordWeightSampleWithProvenance(40.0f, hostMillis, 3, 1));
  currentWeight = 40.0f;
  currentWeightSequence = 3;
  currentWeightReceivedAtMs = hostMillis;
  shot.automaticBrew = true;
  schedulePendingShotFinalize(EndReason::FAST_EXTRACTION_MAX_WEIGHT, 28000);
  CHECK(pendingFinalize.curve.ended.atDs == 280);
  CHECK(pendingFinalize.curve.ended.weightCg == 4000);
  CHECK(pendingFinalize.curve.weightCg[14] == 4000);

  session.active = false;
  stopperState = StopperState::READY;
  currentWeight = 42.1f;
  currentWeightSequence = 4;
  currentWeightReceivedAtMs = hostMillis + 1000;
  runLoopAfter(pendingFinalize.dripDelayMs);

  CHECK(!pendingFinalize.pending);
  CHECK(shotCurves.count() == 1);
  ShotCurveRecord curves[1] = {};
  CHECK(shotCurves.copyNewestFirst(curves, 1) == 1);
  CHECK(curves[0].ended.atDs == 280);
  CHECK(curves[0].ended.weightCg == 4210);
  CHECK(curves[0].weightCg[14] == 4210);
  CHECK(lastShotCurve.ended.atDs == 280);
  CHECK(lastShotCurve.ended.weightCg == 4210);
}

void verifySettledCurveEndpointForCut(EndReason reason, bool slowExtended,
                                      uint32_t durationMs) {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();

  session.active = true;
  session.startedWithScale = true;
  session.config = snapshotConfig(runtimeConfig);
  session.config.timerOnly = false;
  session.calibrationEligible = false;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 20.0f;
  session.startedAtMs = hostMillis;
  session.circuitClosedAtMs = hostMillis;
  stopperState = StopperState::BREW;
  resetShotTrajectory(hostMillis);
  const uint32_t t0 = hostMillis;

  acceptWeightIntoTrajectory(20.0f, t0, 1);
  if (slowExtended) {
    hostMillis = t0 + durationMs - 2200U;
    acceptWeightIntoTrajectory(34.0f, hostMillis, 2);
    enterSlowExtractionExtended(34.0f, hostMillis);
  }
  hostMillis = t0 + durationMs;
  acceptWeightIntoTrajectory(36.0f, hostMillis, 3);
  currentWeight = 36.0f;
  currentWeightSequence = 3;
  currentWeightReceivedAtMs = hostMillis;
  shot.automaticBrew = true;
  schedulePendingShotFinalize(reason, durationMs);
  CHECK(pendingFinalize.pending);
  CHECK(pendingFinalize.curve.ended.atDs == durationMs / 100U);
  CHECK(pendingFinalize.curve.ended.weightCg == 3600);

  session.active = false;
  stopperState = StopperState::READY;
  currentWeight = 38.4f;
  currentWeightSequence = 4;
  currentWeightReceivedAtMs = hostMillis + 1000U;
  runLoopAfter(pendingFinalize.dripDelayMs);

  CHECK(!pendingFinalize.pending);
  CHECK(shotCurves.count() == 1);
  ShotCurveRecord curves[1] = {};
  CHECK(shotCurves.copyNewestFirst(curves, 1) == 1);
  CHECK(curves[0].ended.atDs == durationMs / 100U);
  CHECK(curves[0].ended.weightCg == 3840);
  CHECK(lastShotCurve.ended.atDs == durationMs / 100U);
  CHECK(lastShotCurve.ended.weightCg == 3840);
}

void s02i_normal_and_slow_cuts_use_settled_curve_endpoint() {
  // Non-grid normal end verifies that only the event vertex is revised.
  verifySettledCurveEndpointForCut(EndReason::SCALE_THRESHOLD, false, 27900);
  // Both slow-guard exits use the same settled endpoint path.
  verifySettledCurveEndpointForCut(EndReason::SLOW_EXTRACTION_MAX_TIME, true,
                                   28000);
  verifySettledCurveEndpointForCut(EndReason::SLOW_EXTRACTION_MIN_WEIGHT, true,
                                   28000);
}

void s02b_drip_delay_is_snapshotted_and_honors_boundaries() {
  resetHarness(false, true);
  reachReadyFromBoot();
  session.config = snapshotConfig(runtimeConfig);
  session.config.dripDelayMs = 1700;
  session.startedWithScale = true;
  session.config.timerOnly = false;
  shot.automaticBrew = true;
  schedulePendingShotFinalize(EndReason::ACTIVATOR, 12000);
  CHECK(pendingFinalize.dripDelayMs == 1700);

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.dripDelayMs = 2500;
  pendingFinalize.endedAtMs = hostMillis;

  runtimeConfig.dripDelayMs = 0;
  runLoopAfter(2499);
  CHECK(pendingFinalize.pending);
  CHECK(pendingFinalize.dripDelayMs == 2500);
  runLoopAfter(1);
  CHECK(!pendingFinalize.pending);

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.dripDelayMs = 0;
  pendingFinalize.endedAtMs = hostMillis;
  runLoopAfter(0);
  CHECK(!pendingFinalize.pending);
}

void s17_new_cycle_commits_pending_log_as_last_known() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.logEligible = true;
  pendingFinalize.lastKnownWeightValid = true;
  pendingFinalize.lastKnownWeightG = 43.7f;
  pendingFinalize.startedWithScale = true;
  pendingFinalize.timerOnly = false;
  pendingFinalize.automaticBrew = true;
  pendingFinalize.finalState = StopperState::BREW;
  pendingFinalize.endReason = EndReason::SCALE_THRESHOLD;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs = 151;
  pendingFinalize.firstDropDs = 45;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  pendingFinalize.scaleBaselineG = 0.0f;
  pendingFinalize.endedAtMs = hostMillis;
  startCycle();
  CHECK(!pendingFinalize.pending);
  CHECK(session.active);
  CHECK(shotLog.dirty());
  CHECK(shotLog.count() == 1);
  ShotLogRecord records[1] = {};
  CHECK(shotLog.copyNewestFirst(records, 1) == 1);
  CHECK(records[0].actualWeightSource ==
        static_cast<uint8_t>(ActualWeightSource::LAST_KNOWN));
  CHECK(records[0].actualWeightCg == shotLogWeightToCentigrams(43.7f));
  CHECK(records[0].avgFlowCgS == 412);
}

void w90_save_unknown_preset_id_does_not_overwrite_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  const ShotPreset *dbl =
      findShotPreset(presetBank, FACTORY_PRESET_ID_DOUBLE);
  CHECK(dbl != nullptr);
  const uint8_t originalGoal = dbl->goalWeightG;
  WebCommand save;
  save.type = WebCommandType::PRESET_OP;
  save.requestId = 90;
  save.presetAction = static_cast<uint8_t>(PresetAction::SAVE);
  save.presetId = 99;
  save.config = runtimeConfig;
  save.config.goalWeightG = 40;
  processWebCommand(save);
  CHECK(hostLastForwardedNetworkCommand.requestId == 90);
  CHECK(hostLastForwardedNetworkCommand.resultState ==
        CommandResultState::FAILED);
  const ShotPreset *after =
      findShotPreset(presetBank, FACTORY_PRESET_ID_DOUBLE);
  CHECK(after != nullptr);
  CHECK(after->goalWeightG == originalGoal);
  CHECK(presetBank.activeId == FACTORY_PRESET_ID_DOUBLE);
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

void s14_last_shot_persists_manual_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  CHECK(!persistedLastShot.valid);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs + 100);
  // Manual / short cycles update home last shot; they still skip history.
  CHECK(persistedLastShot.valid);
  runLoopAfter(runtimeConfig.dripDelayMs);
  CHECK(persistedLastShot.valid);
  CHECK(shotLog.count() == 0);
}

void s14c_last_shot_keeps_no_scale_duration() {
  resetHarness(false, false);
  reachReadyFromBoot();
  persistedLastShot = PersistedLastShot{};
  shotLog.clear();
  const uint32_t rawOnAt = startCycle();
  CHECK(stopperState == StopperState::MANUAL_NO_SCALE);
  releaseAtPhysicalDuration(rawOnAt, 12500);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.durationMs >= 12000);
  CHECK(!persistedLastShot.weightValid);
  CHECK(shotLog.count() == 0);
}

void s14b_rinse_does_not_overwrite_last_shot() {
  resetHarness(false, false);
  reachReadyFromBoot();
  persistedLastShot = PersistedLastShot{};
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 42;
  persistedLastShot.weightValid = true;
  persistedLastShot.currentWeightG = 36.0f;
  persistLastShotSnapshot(persistedLastShot);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE ||
        stopperState == StopperState::READY);
  runLoopAfter(runtimeConfig.rinseDurationMs + runtimeConfig.dripDelayMs);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.cycleId == 42);
  CHECK(fabsf(persistedLastShot.currentWeightG - 36.0f) < 0.001f);
}

void s14d_web_stop_during_rinse_does_not_overwrite_last_shot() {
  resetHarness(false, false);
  reachReadyFromBoot();
  persistedLastShot = PersistedLastShot{};
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 42;
  persistedLastShot.weightValid = true;
  persistedLastShot.currentWeightG = 36.0f;
  persistLastShotSnapshot(persistedLastShot);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  processWebCommand(webControlCommand(WebCommandType::STOP));
  CHECK(session.endReason == EndReason::WEB_STOP);
  CHECK(!session.active);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.cycleId == 42);
  CHECK(fabsf(persistedLastShot.currentWeightG - 36.0f) < 0.001f);
}

void s15b_cup_off_after_end_keeps_last_known_actual() {
  resetHarness(false, true);
  reachReadyFromBoot();
  shotLog.clear();
  persistedLastShot = PersistedLastShot{};
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 9;
  persistedLastShot.weightValid = true;
  persistedLastShot.currentWeightG = 42.1f;
  persistLastShotSnapshot(persistedLastShot);

  currentWeight = 0.0f;
  currentWeightSequence = 8;
  currentWeightReceivedAtMs = hostMillis + 1;
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.cycleId = 9;
  pendingFinalize.logEligible = true;
  pendingFinalize.startedWithScale = true;
  pendingFinalize.timerOnly = false;
  pendingFinalize.automaticBrew = true;
  pendingFinalize.lastKnownWeightValid = true;
  pendingFinalize.lastKnownWeightG = 42.1f;
  pendingFinalize.finalState = StopperState::BREW;
  pendingFinalize.endReason = EndReason::FAST_EXTRACTION_MAX_WEIGHT;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs = 280;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 7;
  pendingFinalize.extractionGuardEnabled = true;
  pendingFinalize.extractionExtended = true;
  runLoopAfter(pendingFinalize.dripDelayMs);
  CHECK(!pendingFinalize.pending);
  CHECK(shotLog.count() == 1);
  ShotLogRecord records[1] = {};
  CHECK(shotLog.copyNewestFirst(records, 1) == 1);
  CHECK(records[0].actualWeightSource ==
        static_cast<uint8_t>(ActualWeightSource::LAST_KNOWN));
  CHECK(records[0].actualWeightCg == shotLogWeightToCentigrams(42.1f));
  CHECK(fabsf(persistedLastShot.currentWeightG - 42.1f) < 0.001f);
}

void s15c_last_shot_prefers_last_accepted_over_cup_off() {
  resetHarness(false, true);
  reachReadyFromBoot();
  session.active = true;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 42.1f;
  session.startedAtMs = hostMillis > 25000 ? hostMillis - 25000 : 1;
  session.circuitClosedAtMs = session.startedAtMs;
  session.firstDropMs = session.startedAtMs + 4500;
  session.weightSequenceAtStart = 1;
  currentWeight = 0.0f;
  currentWeightSequence = 5;
  currentWeightReceivedAtMs = hostMillis;
  persistLastShotFromEndedCycle(EndReason::FAST_EXTRACTION_MAX_WEIGHT, 25000);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.weightValid);
  CHECK(fabsf(persistedLastShot.currentWeightG - 42.1f) < 0.001f);
  CHECK(persistedLastShot.firstDropElapsedMs == 4500);
}

void s15_last_shot_persists_after_drip_when_eligible() {
  resetHarness(false, true);
  reachReadyFromBoot();
  persistedLastShot = PersistedLastShot{};
  currentWeight = 36.4f;
  currentWeightSequence = 5;
  currentWeightReceivedAtMs = hostMillis + 1;
  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.cycleId = 7;
  pendingFinalize.logEligible = true;
  pendingFinalize.startedWithScale = true;
  pendingFinalize.timerOnly = false;
  pendingFinalize.automaticBrew = true;
  pendingFinalize.finalState = StopperState::BREW;
  pendingFinalize.endReason = EndReason::SCALE_THRESHOLD;
  pendingFinalize.durationDs = 120;
  pendingFinalize.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.endedAtMs = hostMillis;
  pendingFinalize.endedWeightSequence = 4;
  runLoopAfter(pendingFinalize.dripDelayMs);
  CHECK(!pendingFinalize.pending);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.cycleId == 7);
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

void s16b_factory_reset_hides_last_shot_on_status() {
  resetHarness(false, false);
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 11;
  persistLastShotSnapshot(persistedLastShot);
  CHECK(lastShotStore.clear());
  clearLastShotSnapshot();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(!persistedLastShot.valid);
  CHECK(!status.lastShot.valid);
}

void s18_last_shot_keeps_no_scale_guard_from_cycle() {
  resetHarness(false, true);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  advanceToBrew();
  ScaleEvent fresh{};
  fresh.type = ScaleEventType::WEIGHT;
  fresh.weightG = 18.0f;
  fresh.receivedAtMs = hostMillis;
  fresh.packetSequence = ++currentWeightSequence;
  publishScaleEvent(fresh, false);
  processScaleWorkerEvents();
  acceptWeightIntoTrajectory(18.0f, hostMillis, currentWeightSequence);
  session.lastAcceptedWeightG = 18.0f;
  session.hasWeightAnchor = true;
  releaseAtPhysicalDuration(rawOnAt, 12000);
  runLoopAfter(runtimeConfig.dripDelayMs);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.noScaleShotGuardEnabled);
  CHECK(persistedLastShot.noScaleShotGuardArmed);
  CHECK(noScaleShotGuardArmed);
  consumeNoScaleShotGuard();
  CHECK(!noScaleShotGuardArmed);
  CHECK(persistedLastShot.noScaleShotGuardArmed);
  publishControlStatus();
  ControlStatusSnapshot status;
  copyControlStatus(status);
  CHECK(!status.noScaleShotGuardArmed);
  CHECK(status.lastShot.noScaleShotGuardArmed);
}

void n01_wall_clock_tracks_utc_from_anchor() {
  g_wallClock.reset();
  g_wallClock.setSyncing("pool.ntp.org", 1000);
  g_wallClock.queueSyncFromCallback(1'700'000'000U);
  CHECK(g_wallClock.applyPendingSync(1000));
  CHECK(g_wallClock.nowUtcSec(1000) == 1'700'000'000U);
  CHECK(g_wallClock.nowUtcSec(6000) == 1'700'000'005U);
}

void n01c_wall_clock_cancel_syncing_restores_anchor() {
  g_wallClock.reset();
  g_wallClock.setSyncing("pool.ntp.org", 1000);
  g_wallClock.queueSyncFromCallback(1'700'000'000U);
  CHECK(g_wallClock.applyPendingSync(1000));
  CHECK(g_wallClock.synced());
  g_wallClock.setSyncing("time.google.com", 5000);
  CHECK(g_wallClock.snapshot(5000).state == TimeSyncState::SYNCING);
  g_wallClock.cancelSyncing();
  const TimeStatusSnapshot restored = g_wallClock.snapshot(5000);
  CHECK(restored.state == TimeSyncState::SYNCED);
  CHECK(g_wallClock.synced());
  CHECK(g_wallClock.nowUtcSec(5000) == 1'700'000'004U);
  CHECK(restored.consecutiveFailures == 0);

  // A callback racing with an RF gate must be discardable without losing the
  // last good wall-clock anchor.
  g_wallClock.queueSyncFromCallback(1'800'000'000U);
  g_wallClock.cancelPendingSync();
  CHECK(!g_wallClock.applyPendingSync(5001));
  CHECK(g_wallClock.synced());
  CHECK(g_wallClock.nowUtcSec(5001) == 1'700'000'004U);

  g_wallClock.reset();
  g_wallClock.setSyncing("pool.ntp.org", 100);
  g_wallClock.cancelSyncing();
  CHECK(g_wallClock.snapshot(100).state == TimeSyncState::OFF);
  CHECK(!g_wallClock.synced());
}

void n01b_wall_clock_survives_millis_wrap() {
  g_wallClock.reset();
  g_wallClock.setSyncing("pool.ntp.org", 1000);
  g_wallClock.queueSyncFromCallback(1'700'000'000U);
  CHECK(g_wallClock.applyPendingSync(1000));
  constexpr uint32_t afterWrap = 50;
  const uint32_t elapsedMs = static_cast<uint32_t>(afterWrap - 1000U);
  CHECK(g_wallClock.nowUtcSec(afterWrap) ==
        1'700'000'000U + elapsedMs / 1000U);
  CHECK(g_wallClock.synced());
  const TimeStatusSnapshot snap = g_wallClock.snapshot(afterWrap);
  CHECK(snap.utcSec == 1'700'000'000U + elapsedMs / 1000U);
  CHECK(snap.lastSyncAgeMs == elapsedMs);
  CHECK(snap.state == TimeSyncState::STALE);

  g_wallClock.reset();
  g_wallClock.markFailed(UINT32_MAX - 1000U, 5);
  CHECK(g_wallClock.snapshot(UINT32_MAX - 500U).nextRetryInMs == 14500U);
  CHECK(g_wallClock.snapshot(100U).nextRetryInMs == 13899U);
}

void n02_ntp_hostname_validation() {
  CHECK(validNtpHostname("pool.ntp.org"));
  CHECK(validNtpHostname("time.google.com"));
  CHECK(!validNtpHostname(""));
  CHECK(!validNtpHostname("-bad.example"));
  CHECK(!validNtpHostname("bad space"));
}

void n03_unsynced_retry_is_fifteen_seconds() {
  CHECK(NTP_UNSYNCED_RETRY_MS == 15'000U);
  CHECK(ntpRetryDelayMs(0) == NTP_UNSYNCED_RETRY_MS);
  CHECK(ntpRetryDelayMs(5) == NTP_UNSYNCED_RETRY_MS);
}

void rf01_coex_is_always_bt() {
  CHECK(snapshotRfCoexPreference() == RfCoexPreference::BT);
  CHECK(strcmp(rfCoexPreferenceName(RfCoexPreference::BT), "BT") == 0);
  CHECK(strcmp(rfCoexPreferenceName(RfCoexPreference::WIFI), "WIFI") == 0);
  CHECK(strcmp(rfCoexPreferenceName(RfCoexPreference::BALANCE), "BALANCE") == 0);
  ensureRfCoexBt();
  CHECK(snapshotRfCoexPreference() == RfCoexPreference::BT);
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
  CHECK(hostLastForwardedNetworkCommand.commitConfirmed);
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
  CHECK(serialCliParseLine("HELP", request));
  CHECK(request.verb == SerialCliVerb::HELP);
  CHECK(serialCliParseLine("HELLO", request));
  CHECK(request.verb == SerialCliVerb::HELLO);
  CHECK(serialCliParseLine("REBOOT", request));
  CHECK(request.verb == SerialCliVerb::REBOOT);
  CHECK(serialCliParseLine("SET_DEVICE_PASSWORD password1234", request));
  CHECK(request.verb == SerialCliVerb::SET_DEVICE_PASSWORD);
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
  CHECK(serialCliParseLine("RESET_DEVICE_PASSWORD", request));
  CHECK(request.verb == SerialCliVerb::RESET_DEVICE_PASSWORD);
  CHECK(serialCliParseLine("CLEAR_WIFI", request));
  CHECK(request.verb == SerialCliVerb::CLEAR_WIFI);
  CHECK(serialCliParseLine("RESET_NETWORK_AP", request));
  CHECK(request.verb == SerialCliVerb::RESET_NETWORK_AP);
  CHECK(serialCliParseLine("SERIAL_DEBUG_ON", request));
  CHECK(request.verb == SerialCliVerb::SERIAL_DEBUG_ON);
  CHECK(serialCliParseLine("SERIAL_DEBUG_OFF", request));
  CHECK(request.verb == SerialCliVerb::SERIAL_DEBUG_OFF);
  CHECK(serialCliParseLine("DEBUG_FULL", request));
  CHECK(request.verb == SerialCliVerb::DEBUG_FULL);
  CHECK(serialCliParseLine("DEBUG_OFF", request));
  CHECK(request.verb == SerialCliVerb::DEBUG_OFF);
  CHECK(serialCliParseLine("DEBUG_STATUS", request));
  CHECK(request.verb == SerialCliVerb::DEBUG_STATUS);
  CHECK(serialCliParseLine("WIFI_CONNECT", request));
  CHECK(request.verb == SerialCliVerb::WIFI_CONNECT);
  CHECK(serialCliParseLine("WIFI_DISCONNECT", request));
  CHECK(request.verb == SerialCliVerb::WIFI_DISCONNECT);
  CHECK(serialCliParseLine("WIFI_RESTART", request));
  CHECK(request.verb == SerialCliVerb::WIFI_RESTART);
  CHECK(serialCliParseLine("WIFI_STATUS", request));
  CHECK(request.verb == SerialCliVerb::WIFI_STATUS);
  CHECK(serialCliParseLine("AP_START", request));
  CHECK(request.verb == SerialCliVerb::AP_START);
  CHECK(serialCliParseLine("AP_STOP", request));
  CHECK(request.verb == SerialCliVerb::AP_STOP);
  CHECK(serialCliParseLine("AP_STATUS", request));
  CHECK(request.verb == SerialCliVerb::AP_STATUS);
  CHECK(serialCliParseLine("WEBUI_START", request));
  CHECK(request.verb == SerialCliVerb::WEBUI_START);
  CHECK(serialCliParseLine("WEBUI_STOP", request));
  CHECK(request.verb == SerialCliVerb::WEBUI_STOP);
  CHECK(serialCliParseLine("WEBUI_RESTART", request));
  CHECK(request.verb == SerialCliVerb::WEBUI_RESTART);
  CHECK(serialCliParseLine("WEBUI_STATUS", request));
  CHECK(request.verb == SerialCliVerb::WEBUI_STATUS);
  CHECK(serialCliParseLine("NET_STATUS", request));
  CHECK(request.verb == SerialCliVerb::NET_STATUS);
  CHECK(serialCliParseLine("LOG_DUMP", request));
  CHECK(request.verb == SerialCliVerb::LOG_DUMP);
  CHECK(serialCliParseLine("HEALTH", request));
  CHECK(request.verb == SerialCliVerb::HEALTH);
  CHECK(serialCliParseLine("SCALE_STATUS", request));
  CHECK(request.verb == SerialCliVerb::SCALE_STATUS);
  CHECK(serialCliParseLine("NTP_STATUS", request));
  CHECK(request.verb == SerialCliVerb::NTP_STATUS);
  CHECK(serialCliParseLine("BLE_COMPAT_ENABLE", request));
  CHECK(request.verb == SerialCliVerb::BLE_COMPAT_ENABLE);
  CHECK(serialCliParseLine("BLE_COMPAT_DISABLE", request));
  CHECK(request.verb == SerialCliVerb::BLE_COMPAT_DISABLE);
  CHECK(serialCliParseLine("BLE_COMPAT_STATUS", request));
  CHECK(request.verb == SerialCliVerb::BLE_COMPAT_STATUS);
  CHECK(!serialCliParseLine("HELP extra", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(!serialCliParseLine("REBOOT extra", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(!serialCliParseLine("SERIAL_DEBUG_ON extra", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(serialCliParseLine("SET_DEVICE_PASSWORD ineedacoffee", request));
  CHECK(request.verb == SerialCliVerb::INVALID_ARGS);
  CHECK(serialCliParseLine("SET_DEVICE_PASSWORD oldpass newpass", request));
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

void bc01_ble_companion_validates_and_applies_recipe_writes() {
  resetHarness(false, false);
  reachReadyFromBoot();
  BleCompanionRequest request;
  request.sequence = 41;
  request.type = BleCompanionRequestType::SET_GOAL_WEIGHT;
  request.value = 42;
  CHECK(enqueueBleCompanionRequest(request));
  processBleCompanionRequests();
  BleCompanionResult result;
  CHECK(xQueueReceive(bleCompanionResultQueue, &result, 0) == pdTRUE);
  CHECK(result.accepted);
  CHECK(effectiveRuntimeConfig().goalWeightG == 42);

  request.sequence = 42;
  request.value = 5;
  CHECK(enqueueBleCompanionRequest(request));
  processBleCompanionRequests();
  CHECK(xQueueReceive(bleCompanionResultQueue, &result, 0) == pdTRUE);
  CHECK(!result.accepted);
  CHECK(result.reason == BleCompanionRejectReason::INVALID_VALUE);
  CHECK(effectiveRuntimeConfig().goalWeightG == 42);
}

void bc02_ble_companion_rejects_config_while_active_but_allows_ap() {
  resetHarness(false, false);
  reachReadyFromBoot();
  session.active = true;
  stopperState = StopperState::BREW;

  BleCompanionRequest request;
  request.sequence = 51;
  request.type = BleCompanionRequestType::SET_AUTO_TARE;
  request.value = 0;
  CHECK(enqueueBleCompanionRequest(request));
  processBleCompanionRequests();
  BleCompanionResult result;
  CHECK(xQueueReceive(bleCompanionResultQueue, &result, 0) == pdTRUE);
  CHECK(!result.accepted);
  CHECK(result.reason == BleCompanionRejectReason::NOT_READY);

  request.sequence = 52;
  request.type = BleCompanionRequestType::SET_AP_ENABLED;
  request.value = 1;
  CHECK(enqueueBleCompanionRequest(request));
  processBleCompanionRequests();
  CHECK(xQueueReceive(bleCompanionResultQueue, &result, 0) == pdTRUE);
  CHECK(result.accepted);
  WebCommand queued;
  CHECK(xQueueReceive(webCommandQueue, &queued, 0) == pdTRUE);
  CHECK(queued.type == WebCommandType::AP_START);
}

void bc03_ble_companion_rejects_legacy_reset_bbw_value() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  const uint32_t original = effectiveRuntimeConfig().bbwProtectionMs;
  BleCompanionRequest request;
  request.sequence = 61;
  request.type = BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS;
  request.value = 3;
  CHECK(enqueueBleCompanionRequest(request));
  processBleCompanionRequests();
  BleCompanionResult result;
  CHECK(xQueueReceive(bleCompanionResultQueue, &result, 0) == pdTRUE);
  CHECK(!result.accepted);
  CHECK(result.reason == BleCompanionRejectReason::INVALID_VALUE);
  CHECK(effectiveRuntimeConfig().bbwProtectionMs == original);
}

void bc04_ble_companion_enablement_is_next_boot_only() {
  resetHarness(false, false);
  CHECK(copyBleCompanionStatus().enabled);
  CHECK(copyBleCompanionStatus().configuredEnabled);
  CHECK(!copyBleCompanionStatus().restartRequired);

  CHECK(persistBleCompanionEnabled(false));
  const BleCompanionStatusSnapshot pendingDisable =
      copyBleCompanionStatus();
  CHECK(pendingDisable.enabled);
  CHECK(!pendingDisable.configuredEnabled);
  CHECK(pendingDisable.restartRequired);
  BleCompanionRuntimeSnapshot runtime;
  copyBleCompanionRuntimeSnapshot(runtime);
  CHECK(runtime.enabled);
  CHECK(!runtime.configuredEnabled);
  publishControlStatus();
  ControlStatusSnapshot control;
  copyControlStatus(control);
  CHECK(!control.bleCompanionEnabled);
  CHECK(control.bleCompanionActive);
  CHECK(control.bleCompanionRestartRequired);

  CHECK(persistBleCompanionEnabled(true));
  const BleCompanionStatusSnapshot canceled = copyBleCompanionStatus();
  CHECK(canceled.enabled);
  CHECK(canceled.configuredEnabled);
  CHECK(!canceled.restartRequired);
}

void bc05_ble_scan_intensity_applies_live_without_restart() {
  resetHarness(false, false);
  CHECK(liveBleScanIntensity() == BleScanIntensity::AGGRESSIVE);
  CHECK(!copyBleCompanionStatus().restartRequired);

  CHECK(persistBleScanIntensity(BleScanIntensity::LIGHT));
  CHECK(liveBleScanIntensity() == BleScanIntensity::LIGHT);
  CHECK(!copyBleCompanionStatus().restartRequired);
  publishControlStatus();
  ControlStatusSnapshot control;
  copyControlStatus(control);
  CHECK(control.bleCompanionScanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::LIGHT));
  CHECK(!control.bleCompanionRestartRequired);

  WebCommand command = webControlCommand(WebCommandType::BLE_SCAN_INTENSITY);
  command.bleScanIntensitySpecified = true;
  command.bleScanIntensity =
      static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE);
  processWebCommand(command);
  CHECK(liveBleScanIntensity() == BleScanIntensity::AGGRESSIVE);
  CHECK(!copyBleCompanionStatus().restartRequired);
}

void sc07_reset_device_password_and_clear_wifi_queue() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("RESET_DEVICE_PASSWORD\n");
  CHECK(serialTxContains("OK queued RESET_DEVICE_PASSWORD"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type ==
        WebCommandType::RESET_DEVICE_PASSWORD);

  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("CLEAR_WIFI\n");
  CHECK(serialTxContains("OK queued CLEAR_WIFI"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type ==
        WebCommandType::FORGET_NETWORK);
}

void sc08_set_device_password_queues_change() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("SET_DEVICE_PASSWORD password1234\n");
  CHECK(serialTxContains("OK queued SET_DEVICE_PASSWORD"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type ==
        WebCommandType::CHANGE_DEVICE_PASSWORD);
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
  CHECK(!serialTxContains("configuration accepted"));
  CHECK(!serialTxContains("Scale name scan: no advertisement"));
  DebugEvent events[DEBUG_EVENT_CAPACITY] = {};
  const size_t count = copyDebugEvents(0, events, DEBUG_EVENT_CAPACITY);
  bool sawAccepted = false;
  for (size_t i = 0; i < count; ++i) {
    if (events[i].code == DebugCode::CONFIG_ACCEPTED) {
      sawAccepted = true;
    }
  }
  CHECK(sawAccepted);
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

void sc10_help_prints_one_line_per_command() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.active);
  Serial.tx.clear();
  feedSerial("help\n");
  CHECK(serialTxContains("HELP  list commands"));
  CHECK(serialTxContains("HELLO  probe CLI"));
  CHECK(serialTxContains("REBOOT  restart firmware"));
  CHECK(serialTxContains("FACTORY_RESET  wipe Wi-Fi"));
  CHECK(serialTxContains("RESET_DEVICE_PASSWORD  restore device password"));
  CHECK(serialTxContains("SET_DEVICE_PASSWORD <password>"));
  CHECK(serialTxContains("SET_WIFI <ssid> [password]"));
  CHECK(serialTxContains("CLEAR_WIFI  forget STA Wi-Fi"));
  CHECK(serialTxContains("CLEAR_SHOTS  clear shot history"));
  CHECK(serialTxContains("RESET_NETWORK_AP  forget STA Wi-Fi"));
  CHECK(serialTxContains("SERIAL_DEBUG_ON  enable USB debug"));
  CHECK(serialTxContains("SERIAL_DEBUG_OFF  disable USB debug"));
  CHECK(serialTxContains("DEBUG_FULL  serial debug + ring DEBUG"));
  CHECK(serialTxContains("DEBUG_OFF  serial off and ring none"));
  CHECK(serialTxContains("DEBUG_STATUS  show serialDebugOutput"));
  CHECK(serialTxContains("WIFI_CONNECT  associate saved STA"));
  CHECK(serialTxContains("WIFI_DISCONNECT  drop STA"));
  CHECK(serialTxContains("WIFI_RESTART  drop then reconnect"));
  CHECK(serialTxContains("WIFI_STATUS  dump STA"));
  CHECK(serialTxContains("AP_START  raise SoftAP (stays up with STA)"));
  CHECK(serialTxContains("AP_STOP  stop SoftAP"));
  CHECK(serialTxContains("AP_STATUS  dump SoftAP"));
  CHECK(serialTxContains("WEBUI_START  start HTTP"));
  CHECK(serialTxContains("WEBUI_STOP  stop HTTP"));
  CHECK(serialTxContains("WEBUI_RESTART  bounce HTTP"));
  CHECK(serialTxContains("WEBUI_STATUS  dump HTTP"));
  CHECK(serialTxContains("NET_STATUS  WIFI + AP + WEBUI"));
  CHECK(serialTxContains(
      "LOG_DUMP  print RAM debug ring (deferred in brew/machine circuit)"));
  CHECK(serialTxContains("HEALTH  heap, loop gap, cpu load"));
  CHECK(serialTxContains("SCALE_STATUS  BLE scale link"));
  CHECK(serialTxContains("NTP_STATUS  wall clock"));
  CHECK(serialTxContains("e.g. SET_WIFI CafeLAN CafePass1"));
  CHECK(session.active);
}

void sc11_reboot_queues_restart_when_ready() {
  resetHarness(false, false);
  reachReadyFromBoot();
  feedSerial("REBOOT\n");
  CHECK(serialTxContains("OK queued REBOOT"));
  runLoopAfter(MAINTENANCE_LEASE_SETTLE_MS);
  CHECK(hostLastForwardedNetworkCommand.type == WebCommandType::RESTART);
}

void sc12_reboot_waits_while_active() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  feedSerial("REBOOT\n");
  CHECK(serialTxContains("OK queued REBOOT"));
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(plannedRestartHeld);
  CHECK(!maintenanceLease.active);
}

void sc13_debug_full_and_off_during_cycle() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.active);
  Serial.tx.clear();
  feedSerial("DEBUG_FULL\n");
  CHECK(serialTxContains("OK debug full"));
  CHECK(runtimeConfig.serialDebugOutput);
  CHECK(runtimeConfig.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::DEBUG));
  CHECK(serialLogLevel == LogLevel::DEBUG);
  CHECK(ringRetainLogLevel == LogLevel::DEBUG);
  CHECK(runtimePersistPending);
  Serial.tx.clear();
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECTING);
  CHECK(!serialTxContains("scale connecting"));
  DebugEvent events[DEBUG_EVENT_CAPACITY] = {};
  const size_t count = copyDebugEvents(0, events, DEBUG_EVENT_CAPACITY);
  bool sawConnecting = false;
  for (size_t i = 0; i < count; ++i) {
    if (events[i].code == DebugCode::SCALE_CONNECTING) {
      sawConnecting = true;
    }
  }
  CHECK(sawConnecting);
  feedSerial("DEBUG_OFF\n");
  CHECK(serialTxContains("OK debug off"));
  CHECK(!runtimeConfig.serialDebugOutput);
  CHECK(runtimeConfig.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::NONE));
  CHECK(serialLogLevel == LogLevel::NONE);
  CHECK(ringRetainLogLevel == LogLevel::NONE);
}

void sc14_network_actions_queue_without_ready() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  Serial.tx.clear();
  feedSerial("WIFI_CONNECT\n");
  CHECK(serialTxContains("WARN cycle active; proceeding"));
  CHECK(serialTxContains("OK queued WIFI_CONNECT"));
  CHECK(hostLastForwardedNetworkCommand.type == WebCommandType::WIFI_CONNECT);
  feedSerial("WEBUI_STOP\n");
  CHECK(serialTxContains("OK queued WEBUI_STOP"));
  CHECK(hostLastForwardedNetworkCommand.type == WebCommandType::WEBUI_STOP);
  feedSerial("AP_START\n");
  CHECK(serialTxContains("OK queued AP_START"));
  CHECK(hostLastForwardedNetworkCommand.type == WebCommandType::AP_START);
}

void sc15_status_printers_use_dump_views() {
  SerialCliNetworkDump dump;
  dump.wifiConfigured = true;
  dump.staOpen = false;
  strncpy(dump.staSsid, "CafeLAN", sizeof(dump.staSsid) - 1);
  dump.staState = 2;
  dump.wifiStatus = 3;
  dump.wifiMode = 1;
  dump.httpActive = true;
  dump.apActive = true;
  dump.staReconnectHeld = true;
  dump.apStartHeld = false;
  dump.httpStartHeld = true;
  strncpy(dump.staIp, "192.168.1.20", sizeof(dump.staIp) - 1);
  Serial.tx.clear();
  serialCliPrintNetStatus(dump);
  CHECK(serialTxContains("WIFI_STATUS"));
  CHECK(serialTxContains("ssid=CafeLAN"));
  CHECK(serialTxContains("staState=CONNECTED"));
  CHECK(serialTxContains("wifiStatus=3 CONNECTED"));
  CHECK(serialTxContains("staReconnectHeld=true"));
  CHECK(serialTxContains("AP_STATUS"));
  CHECK(serialTxContains("ssid=AdvancedShotStopperAP"));
  CHECK(serialTxContains("WEBUI_STATUS"));
  CHECK(serialTxContains("httpActive=true"));
  CHECK(serialTxContains("httpStartHeld=true"));

  SerialCliHealthDump health;
  health.freeHeapBytes = 80000;
  health.loopMaxGapMs = 12;
  health.healthIntervalMaxGapMs = 5;
  health.networkStackMinWords = 400;
  health.cpuLoadValid = true;
  health.cpuMhz = 80;
  health.cpuLoad5s = 0.42f;
  health.cpuLoad1m = 0.55f;
  health.cpuLoad5m = 0.61f;
  health.cpu0Busy = 0.20f;
  health.cpu1Busy = 0.22f;
  health.tempValid = true;
  health.tempC = 42.5f;
  health.tempPeakC = 47.0f;
  Serial.tx.clear();
  serialCliPrintHealth(health);
  CHECK(serialTxContains("HEALTH"));
  CHECK(serialTxContains("heapFree=80000"));
  CHECK(serialTxContains("loopMaxGapMs=12"));
  CHECK(serialTxContains("loopIntervalGapMs=5"));
  CHECK(serialTxContains("psramSize=0"));
  CHECK(serialTxContains("psramFree=0"));
  CHECK(serialTxContains("psramLargest=0"));
  CHECK(serialTxContains("bleHostAllocPsram=0"));
  CHECK(serialTxContains("bleHostAllocFallback=0"));
  CHECK(serialTxContains("hciRxDropped=0"));
  CHECK(serialTxContains("hciTxDropped=0"));
  CHECK(serialTxContains("workBufExternal=false"));
  CHECK(serialTxContains("jsonArenaExternal=false"));
  CHECK(serialTxContains("allocExternalFallback=0"));
  CHECK(serialTxContains("stackNetwork=400"));
  CHECK(serialTxContains("cpuMhz=80"));
  CHECK(serialTxContains("cpuLoad5s=0.42"));
  CHECK(serialTxContains("cpuLoad1m=0.55"));
  CHECK(serialTxContains("cpuLoad5m=0.61"));
  CHECK(serialTxContains("cpu0Busy=0.20"));
  CHECK(serialTxContains("cpu1Busy=0.22"));
  CHECK(serialTxContains("tempValid=true"));
  CHECK(serialTxContains("tempC=42.5"));
  CHECK(serialTxContains("tempPeakC=47.0"));

  SerialCliScaleDump scale;
  scale.state = "CONNECTED";
  scale.protocolName = "bookoo";
  strncpy(scale.preferredMac, "AA:BB:CC:DD:EE:FF",
          sizeof(scale.preferredMac) - 1);
  scale.weightFresh = true;
  scale.currentWeightG = 18.5f;
  Serial.tx.clear();
  serialCliPrintScaleStatus(scale);
  CHECK(serialTxContains("SCALE_STATUS"));
  CHECK(serialTxContains("state=CONNECTED"));
  CHECK(serialTxContains("preferredMac=AA:BB:CC:DD:EE:FF"));
  CHECK(serialTxContains("recoveredStaleCount=0"));
  CHECK(serialTxContains("recoveredStaleMs=0"));
  CHECK(serialTxContains("rssi=-"));
  CHECK(serialTxContains("weightG=18.50"));

  scale.rssiValid = true;
  scale.rssi = -62;
  Serial.tx.clear();
  serialCliPrintScaleStatus(scale);
  CHECK(serialTxContains("rssi=-62"));

  SerialCliNtpDump ntp;
  ntp.state = TimeSyncState::SYNCED;
  ntp.utcSec = 1700000000;
  strncpy(ntp.activeServer, "pool.ntp.org", sizeof(ntp.activeServer) - 1);
  ntp.staUp = false;
  Serial.tx.clear();
  serialCliPrintNtpStatus(ntp);
  CHECK(serialTxContains("NTP_STATUS"));
  CHECK(serialTxContains("state=SYNCED"));
  CHECK(serialTxContains("ntp cannot arm without STA"));
}

void sc16_debug_status_and_log_dump() {
  resetHarness(false, false);
  reachReadyFromBoot();
  ringRetainLogLevel = LogLevel::INFO;
  runtimeConfig.ringRetainLogLevel = static_cast<uint8_t>(LogLevel::INFO);
  Serial.tx.clear();
  feedSerial("DEBUG_STATUS\n");
  CHECK(serialTxContains("DEBUG_STATUS"));
  CHECK(serialTxContains("serialDebugOutput=false"));
  CHECK(serialTxContains("serialLogLevel=none"));
  CHECK(serialTxContains("ringRetainLogLevel=info"));
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED);
  Serial.tx.clear();
  feedSerial("LOG_DUMP\n");
  CHECK(serialTxContains("LOG_DUMP"));
  CHECK(serialTxContains("events="));
  CHECK(serialTxContains("configuration accepted"));
  ringRetainLogLevel = LogLevel::NONE;
  runtimeConfig.ringRetainLogLevel = static_cast<uint8_t>(LogLevel::NONE);
  Serial.tx.clear();
  feedSerial("LOG_DUMP\n");
  CHECK(serialTxContains("log ring retain is none"));
  startCycle();
  Serial.tx.clear();
  feedSerial("LOG_DUMP\n");
  CHECK(serialTxContains("ERR LOG dump deferred; circuit/cycle active"));
  CHECK(!serialTxContains("events="));
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

void s04c_delete_shot_record_removes_log_and_curve() {
  resetHarness(false, true);
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  ShotLogRecord record = {};
  record.durationDs = 120;
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  const uint32_t id = stored[0].id;
  CHECK(id != 0);
  ShotCurveRecord curve = emptyShotCurveRecord();
  curve.shotId = id;
  curve.intervalS = SHOT_CURVE_INTERVAL_S;
  CHECK(shotCurves.append(curve));
  CHECK(shotLog.containsId(id));
  CHECK(shotCurves.containsShotId(id));
  CHECK(deleteShotRecord(id));
  CHECK(!shotLog.containsId(id));
  CHECK(!shotCurves.containsShotId(id));
  CHECK(shotLog.count() == 0);
  CHECK(shotCurves.count() == 0);
}

void s04d_delete_shot_record_ok_without_curve() {
  resetHarness(false, true);
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  ShotLogRecord record = {};
  record.durationDs = 80;
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  const uint32_t id = stored[0].id;
  CHECK(!shotCurves.containsShotId(id));
  CHECK(deleteShotRecord(id));
  CHECK(!shotLog.containsId(id));
  CHECK(shotLog.count() == 0);
}

void s04e_delete_shot_record_keeps_log_if_curve_remove_fails() {
  resetHarness(false, true);
  shotLog.clear();
  ShotCurveLog::resetHostStorage();
  shotCurves.load();
  ShotLogRecord record = {};
  record.durationDs = 90;
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  const uint32_t id = stored[0].id;
  ShotCurveRecord curve = emptyShotCurveRecord();
  curve.shotId = id;
  curve.intervalS = SHOT_CURVE_INTERVAL_S;
  CHECK(shotCurves.append(curve));
  g_hostFlashIoMutexAvailable = false;
  const bool deleted = deleteShotRecord(id);
  g_hostFlashIoMutexAvailable = true;
  CHECK(!deleted);
  CHECK(shotLog.containsId(id));
  CHECK(shotCurves.containsShotId(id));
}

void b01_scale_worker_requires_ble_stack() {
  resetHarness(false, true);
  bleStackReady = false;
  CHECK(!initializeScaleWorker());
  publishControlStatus();
  CHECK(!publishedControlStatus.scaleWorkerReady);
  bleStackReady = true;
  publishControlStatus();
  CHECK(publishedControlStatus.scaleWorkerReady ==
        (scaleWorkerTaskHandle != nullptr));
}

void b02_setup_degrades_without_ble() {
  resetHarness(false, true);
  bleStackReady = false;
  setup();
  CHECK(bootDegraded);
  CHECK(!firmwareInitializationComplete);
  CHECK(publishedControlStatus.bootDegraded);
  CHECK(!publishedControlStatus.bootComplete);
  CHECK(!publishedControlStatus.scaleWorkerReady);
}

#if SHOT_STOPPER_ENABLE_JTAG == 1
void b03_jtag_build_starts_serial_without_jumper() {
  resetHarness(false, false);
  CHECK(!usbConsoleJumperPresent());
  setup();
  CHECK(Serial.beginCalls == 1);
  CHECK(usbSerialEnableSource == UsbSerialEnableSource::COMPILE_FLAG);
  CHECK(publishedControlStatus.usbSerialEnableSource ==
        UsbSerialEnableSource::COMPILE_FLAG);
  CHECK(!publishedControlStatus.usbConsoleIo4Closed);
}
#else
void b03_usb_console_stays_off_without_jumper() {
  resetHarness(false, false);
  CHECK(!usbConsoleJumperPresent());
  CHECK(hostPinMode[USB_CONSOLE_GPIO] == INPUT_PULLUP);
  setup();
  CHECK(Serial.beginCalls == 0);
  CHECK(usbSerialEnableSource == UsbSerialEnableSource::OFF);
  CHECK(publishedControlStatus.usbSerialEnableSource ==
        UsbSerialEnableSource::OFF);
  CHECK(!publishedControlStatus.usbConsoleIo4Closed);
}
#endif

void b04_usb_console_starts_when_jumper_held() {
  resetHarness(false, false);
  hostPinLevel[USB_CONSOLE_GPIO] = LOW;
  CHECK(usbConsoleJumperPresent());
  setup();
  CHECK(Serial.beginCalls == 1);
#if SHOT_STOPPER_ENABLE_JTAG == 1
  CHECK(usbSerialEnableSource == UsbSerialEnableSource::COMPILE_FLAG);
#else
  CHECK(usbSerialEnableSource == UsbSerialEnableSource::JUMPER);
#endif
  CHECK(publishedControlStatus.usbConsoleIo4Closed);
  CHECK(publishedControlStatus.usbSerialEnableSource ==
        usbSerialEnableSource);
}

void m08_recipe_copies_match_published_state() {
  resetHarness(false, true);
  RuntimeConfig copied = {};
  ShotPresetBank copiedBank = {};
  copyRuntimeConfig(&copied);
  copyPresetBank(&copiedBank);
  CHECK(copied.goalWeightG == runtimeConfig.goalWeightG);
  CHECK(copiedBank.activeId == presetBank.activeId);
  runtimeConfig.goalWeightG = 44;
  mutableActiveShotPreset(presetBank).goalWeightG = 44;
  commitLiveRuntimeConfig(runtimeConfig, RUNTIME_PERSIST_REASON_USER);
  RuntimeConfig after = {};
  ShotPresetBank afterBank = {};
  copyRuntimeConfig(&after);
  copyPresetBank(&afterBank);
  CHECK(after.goalWeightG == 44);
  CHECK(afterBank.activeId == presetBank.activeId);
  CHECK(findShotPreset(afterBank, afterBank.activeId) != nullptr);
  CHECK(findShotPreset(afterBank, afterBank.activeId)->goalWeightG == 44);
}

void m09_seqlock_yields_when_writer_active() {
  resetHarness(false, true);
  publishControlStatus();
  hostTaskYieldCalls = 0;
  controlStatusSeq = 1;
  ControlStatusSnapshot status = {};
  copyControlStatus(status);
  CHECK(hostTaskYieldCalls == kControlStatusSeqlockTries);
  CHECK(status.state == publishedControlStatus.state);
  controlStatusSeq = 0;

  hostTaskYieldCalls = 0;
  recipeSeq = 1;
  RuntimeConfig copied = {};
  copyRuntimeConfig(&copied);
  CHECK(hostTaskYieldCalls == kControlStatusSeqlockTries);
  CHECK(copied.goalWeightG == publishedRuntimeConfig.goalWeightG);
  recipeSeq = 0;
}

void m12_ble_companion_result_drop_is_counted() {
  resetHarness(false, true);
  BleCompanionResult dummy = {};
  while (xQueueSend(bleCompanionResultQueue, &dummy, 0) == pdTRUE) {
  }
  const uint32_t before = bleCompanionResultDropped;
  BleCompanionRequest request;
  request.sequence = 42;
  request.type = BleCompanionRequestType::SET_GOAL_WEIGHT;
  reportBleCompanionResult(request, true, BleCompanionRejectReason::NONE);
  CHECK(bleCompanionResultDropped == before + 1);
}

void s04b_shot_log_page_slice() {
  size_t start = 99;
  size_t pageCount = 99;
  CHECK(shotLogPageSlice(120, 0, 10, start, pageCount) == true);
  CHECK(start == 0);
  CHECK(pageCount == 10);
  CHECK(shotLogPageSlice(120, 110, 10, start, pageCount) == false);
  CHECK(start == 110);
  CHECK(pageCount == 10);
  CHECK(shotLogPageSlice(120, 115, 10, start, pageCount) == false);
  CHECK(start == 115);
  CHECK(pageCount == 5);
  CHECK(shotLogPageSlice(5, 0, 10, start, pageCount) == false);
  CHECK(start == 0);
  CHECK(pageCount == 5);
  CHECK(shotLogPageSlice(0, 0, 10, start, pageCount) == false);
  CHECK(start == 0);
  CHECK(pageCount == 0);
  CHECK(shotLogPageSlice(20, 50, 10, start, pageCount) == false);
  CHECK(start == 20);
  CHECK(pageCount == 0);
  CHECK(shotLogClampPageLimit(0) == 1);
  CHECK(shotLogClampPageLimit(10) == 10);
  CHECK(shotLogClampPageLimit(120) == 120);
  CHECK(shotLogClampPageLimit(999) == SHOT_LOG_CAPACITY);
  CHECK(SHOT_LOG_PAGE_DEFAULT == 10);
}

void s04f_shot_log_sort_date_and_rating() {
  CHECK(shotLogSortFromName("rating") == ShotLogSort::Rating);
  CHECK(shotLogSortFromName("date") == ShotLogSort::Date);
  CHECK(shotLogSortFromName(nullptr) == ShotLogSort::Date);
  CHECK(shotLogSortFromName("nope") == ShotLogSort::Date);
  CHECK(shotLogSortDirFromName("asc") == ShotLogSortDir::Asc);
  CHECK(shotLogSortDirFromName("desc") == ShotLogSortDir::Desc);
  CHECK(shotLogSortDirFromName(nullptr) == ShotLogSortDir::Desc);

  ShotLogRecord recs[5] = {};
  recs[0].id = 5;
  recs[0].extractionGuardEnabled = shotLogPackRating(0, 3);
  recs[1].id = 4;
  recs[1].extractionGuardEnabled = shotLogPackRating(0, 5);
  recs[2].id = 3;
  recs[2].extractionGuardEnabled = shotLogPackRating(0, 0);
  recs[3].id = 2;
  recs[3].extractionGuardEnabled = shotLogPackRating(0, 5);
  recs[4].id = 1;
  recs[4].extractionGuardEnabled = shotLogPackRating(0, 1);

  ShotLogRecord dateDesc[5];
  memcpy(dateDesc, recs, sizeof(recs));
  shotLogSortRecords(dateDesc, 5, ShotLogSort::Date, ShotLogSortDir::Desc);
  CHECK(dateDesc[0].id == 5);
  CHECK(dateDesc[4].id == 1);

  ShotLogRecord dateAsc[5];
  memcpy(dateAsc, recs, sizeof(recs));
  shotLogSortRecords(dateAsc, 5, ShotLogSort::Date, ShotLogSortDir::Asc);
  CHECK(dateAsc[0].id == 1);
  CHECK(dateAsc[4].id == 5);

  ShotLogRecord ratingDesc[5];
  memcpy(ratingDesc, recs, sizeof(recs));
  shotLogSortRecords(ratingDesc, 5, ShotLogSort::Rating, ShotLogSortDir::Desc);
  CHECK(ratingDesc[0].id == 4);
  CHECK(shotLogRating(ratingDesc[0].extractionGuardEnabled) == 5);
  CHECK(ratingDesc[1].id == 2);
  CHECK(shotLogRating(ratingDesc[1].extractionGuardEnabled) == 5);
  CHECK(ratingDesc[2].id == 5);
  CHECK(shotLogRating(ratingDesc[2].extractionGuardEnabled) == 3);
  CHECK(ratingDesc[3].id == 1);
  CHECK(shotLogRating(ratingDesc[3].extractionGuardEnabled) == 1);
  CHECK(ratingDesc[4].id == 3);
  CHECK(shotLogRating(ratingDesc[4].extractionGuardEnabled) == 0);

  ShotLogRecord ratingAsc[5];
  memcpy(ratingAsc, recs, sizeof(recs));
  shotLogSortRecords(ratingAsc, 5, ShotLogSort::Rating, ShotLogSortDir::Asc);
  CHECK(ratingAsc[0].id == 1);
  CHECK(shotLogRating(ratingAsc[0].extractionGuardEnabled) == 1);
  CHECK(ratingAsc[1].id == 5);
  CHECK(shotLogRating(ratingAsc[1].extractionGuardEnabled) == 3);
  CHECK(ratingAsc[2].id == 4);
  CHECK(shotLogRating(ratingAsc[2].extractionGuardEnabled) == 5);
  CHECK(ratingAsc[3].id == 2);
  CHECK(shotLogRating(ratingAsc[3].extractionGuardEnabled) == 5);
  CHECK(ratingAsc[4].id == 3);
  CHECK(shotLogRating(ratingAsc[4].extractionGuardEnabled) == 0);

  shotLogSortRecords(nullptr, 5, ShotLogSort::Rating, ShotLogSortDir::Desc);
  shotLogSortRecords(recs, 0, ShotLogSort::Rating, ShotLogSortDir::Desc);
  shotLogSortRecords(recs, 1, ShotLogSort::Rating, ShotLogSortDir::Desc);
  CHECK(recs[0].id == 5);
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

void s11_shot_log_record_stays_fixed_size() {
  CHECK(sizeof(ShotLogRecord) == 48);
}

void s12_shot_rating_pack_preserves_guards() {
  const uint8_t flags = shotLogPackGuardFlags(true, true);
  CHECK(shotLogFastGuardEnabled(flags));
  CHECK(shotLogSlowGuardEnabled(flags));
  CHECK(shotLogRating(flags) == 0);
  const uint8_t rated = shotLogPackRating(flags, 4);
  CHECK(shotLogFastGuardEnabled(rated));
  CHECK(shotLogSlowGuardEnabled(rated));
  CHECK(shotLogRating(rated) == 4);
  CHECK(shotLogPackRating(rated, 0) == flags);
  CHECK(shotLogRating(shotLogPackRating(flags, 9)) == 5);
}

void s12b_shot_log_update_rating() {
  resetHarness(false, true);
  shotLog.clear();
  ShotLogRecord record = {};
  record.durationDs = 120;
  record.extractionGuardEnabled = shotLogPackGuardFlags(true, false);
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(shotLog.updateRating(stored[0].id, 3));
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(shotLogRating(stored[0].extractionGuardEnabled) == 3);
  CHECK(shotLogFastGuardEnabled(stored[0].extractionGuardEnabled));
  CHECK(!shotLogSlowGuardEnabled(stored[0].extractionGuardEnabled));
  CHECK(!shotLog.updateRating(stored[0].id, 6));
  CHECK(!shotLog.updateRating(99999U, 1));
}

void s12c_last_shot_rating_survives_finalize_and_commit() {
  resetHarness(false, true);
  shotLog.clear();
  persistedLastShot = PersistedLastShot{};
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 7;
  persistedLastShot.rating = 4;
  persistLastShotSnapshot(persistedLastShot);

  PendingShotFinalize snapshot = {};
  snapshot.cycleId = 7;
  snapshot.durationDs = 300;
  snapshot.goalWeightG = 36;
  snapshot.startedWithScale = true;
  snapshot.automaticBrew = true;
  snapshot.finalState = StopperState::READY;
  snapshot.endReason = EndReason::SCALE_THRESHOLD;
  snapshot.bootId = shotLog.bootId();
  snapshot.logEligible = true;
  snapshot.firstDropDs = 50;

  commitPendingShotLog(snapshot, 36.1f, true, ActualWeightSource::POST_DRIP);
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(shotLogRating(stored[0].extractionGuardEnabled) == 4);
  CHECK(persistedLastShot.shotLogId == stored[0].id);

  persistLastShotFromFinalize(snapshot, 36.1f, true);
  CHECK(persistedLastShot.valid);
  CHECK(persistedLastShot.rating == 4);
  CHECK(persistedLastShot.shotLogId == stored[0].id);
  CHECK(persistedLastShot.cycleId == 7);
  CHECK(fabsf(persistedLastShot.currentWeightG - 36.1f) < 0.001f);
}

void s12d_rate_last_shot_and_history() {
  resetHarness(false, true);
  shotLog.clear();
  persistedLastShot = PersistedLastShot{};
  CHECK(!rateLastShot(3));
  persistedLastShot.valid = true;
  persistedLastShot.cycleId = 1;
  persistLastShotSnapshot(persistedLastShot);
  CHECK(rateLastShot(3));
  CHECK(persistedLastShot.rating == 3);
  CHECK(rateLastShot(0));
  CHECK(persistedLastShot.rating == 0);

  ShotLogRecord record = {};
  record.durationDs = 120;
  CHECK(shotLog.append(record));
  ShotLogRecord stored[1] = {};
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  persistedLastShot.shotLogId = stored[0].id;
  persistLastShotSnapshot(persistedLastShot);
  CHECK(rateLastShot(5));
  CHECK(persistedLastShot.rating == 5);
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(shotLogRating(stored[0].extractionGuardEnabled) == 5);
  CHECK(rateShotRecord(stored[0].id, 2));
  CHECK(persistedLastShot.rating == 2);
  CHECK(shotLog.copyNewestFirst(stored, 1) == 1);
  CHECK(shotLogRating(stored[0].extractionGuardEnabled) == 2);
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

void s19_shot_store_persist_failure_logs_once_until_success() {
  resetHarness(false, true);
  ShotLogRecord record = {};
  record.durationDs = 120;
  record.bootId = shotLog.bootId();
  CHECK(shotLog.append(record, false));
  CHECK(shotLog.dirty());
  g_hostFlashIoMutexAvailable = false;
  debugLog.clear();
  serviceShotStorePersistence();
  CHECK(debugEventExists(DebugCode::SHOT_LOG_PERSIST_FAILED,
                         static_cast<int32_t>(shotLog.count()), 0));
  CHECK(shotLog.dirty());
  debugLog.clear();
  serviceShotStorePersistence();
  CHECK(!debugEventExists(DebugCode::SHOT_LOG_PERSIST_FAILED));
  g_hostFlashIoMutexAvailable = true;
  serviceShotStorePersistence();
  CHECK(shotLog.dirty());
  hostMillis += SHOT_STORE_PERSIST_RETRY_MS;
  serviceShotStorePersistence();
  CHECK(!shotLog.dirty());
  CHECK(!shotLogPersistFailLatched);
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

void h01b_health_heap_low_restarts_only_when_ready_and_sustained() {
  resetHarness(false, true);
  reachReadyFromBoot();
  freeHeapBytes = HEALTH_HEAP_FREE_ALERT_BYTES - 1;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_ALERT_BYTES - 1;
  loopStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  scaleWorkerStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  serviceHealthThresholdAlerts(0);
  CHECK(healthHeapAlertLatched);
  CHECK(!safeRestartRequested);
  CHECK(!debugEventExists(DebugCode::HEALTH_HEAP_RESTART));

  hostMillis += HEALTH_HEAP_LOW_RESTART_MS - 1;
  serviceHealthThresholdAlerts(0);
  CHECK(!safeRestartRequested);

  hostMillis += 1;
  serviceHealthThresholdAlerts(0);
  CHECK(safeRestartRequested);
  CHECK(debugEventExists(DebugCode::HEALTH_HEAP_RESTART,
                         static_cast<int32_t>(freeHeapBytes),
                         static_cast<int32_t>(largestFreeHeapBlockBytes)));
  CHECK(healthHeapRestartLatched);

  safeRestartRequested = false;
  debugLog.clear();
  serviceHealthThresholdAlerts(0);
  CHECK(!debugEventExists(DebugCode::HEALTH_HEAP_RESTART));
  CHECK(!safeRestartRequested);

  resetHarness(false, true);
  reachReadyFromBoot();
  freeHeapBytes = HEALTH_HEAP_FREE_ALERT_BYTES - 1;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_ALERT_BYTES - 1;
  loopStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  scaleWorkerStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  serviceHealthThresholdAlerts(0);
  hostMillis += HEALTH_HEAP_LOW_RESTART_MS / 2;
  serviceHealthThresholdAlerts(0);
  freeHeapBytes = HEALTH_HEAP_FREE_CLEAR_BYTES;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_CLEAR_BYTES;
  serviceHealthThresholdAlerts(0);
  CHECK(!healthHeapAlertLatched);
  hostMillis += HEALTH_HEAP_LOW_RESTART_MS;
  serviceHealthThresholdAlerts(0);
  CHECK(!safeRestartRequested);

  resetHarness(false, true);
  reachReadyFromBoot();
  (void)startCycle();
  CHECK(session.active);
  freeHeapBytes = HEALTH_HEAP_FREE_ALERT_BYTES - 1;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_ALERT_BYTES - 1;
  loopStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  scaleWorkerStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  serviceHealthThresholdAlerts(0);
  hostMillis += HEALTH_HEAP_LOW_RESTART_MS;
  serviceHealthThresholdAlerts(0);
  CHECK(healthHeapAlertLatched);
  CHECK(!safeRestartRequested);
  CHECK(!debugEventExists(DebugCode::HEALTH_HEAP_RESTART));

  resetHarness(false, true);
  reachReadyFromBoot();
  maintenanceLease.active = true;
  freeHeapBytes = HEALTH_HEAP_FREE_ALERT_BYTES - 1;
  largestFreeHeapBlockBytes = HEALTH_HEAP_LARGEST_ALERT_BYTES - 1;
  loopStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  scaleWorkerStackMinWords = HEALTH_STACK_MIN_CLEAR_WORDS;
  serviceHealthThresholdAlerts(0);
  hostMillis += HEALTH_HEAP_LOW_RESTART_MS;
  serviceHealthThresholdAlerts(0);
  CHECK(!safeRestartRequested);
  CHECK(!debugEventExists(DebugCode::HEALTH_HEAP_RESTART));
}

void h02_hwmon_cpu_load_uses_idle_and_ema() {
  Hwmon monitor;
  monitor.begin();

  // Fully idle both cores over 5s → load 0.0
  monitor.hostSetIdleAccumUs(5000000ULL, 5000000ULL);
  HwmonSnapshot snap = monitor.sample(5000);
  CHECK(snap.cpuLoadValid);
  CHECK(snap.cpu0Busy >= 0.0f && snap.cpu0Busy <= 0.01f);
  CHECK(snap.cpu1Busy >= 0.0f && snap.cpu1Busy <= 0.01f);
  CHECK(snap.cpuLoad5s >= 0.0f && snap.cpuLoad5s <= 0.01f);
  CHECK(snap.cpuLoad1m >= 0.0f && snap.cpuLoad1m <= 0.01f);
  CHECK(snap.cpuMhz == 80U);

  // Fully busy both cores → load 2.0
  monitor.hostSetIdleAccumUs(0, 0);
  snap = monitor.sample(5000);
  CHECK(snap.cpuLoadValid);
  CHECK(snap.cpu0Busy >= 0.99f && snap.cpu0Busy <= 1.0f);
  CHECK(snap.cpu1Busy >= 0.99f && snap.cpu1Busy <= 1.0f);
  CHECK(snap.cpuLoad5s >= 1.99f && snap.cpuLoad5s <= 2.0f);
  // EMA moves toward 2 but stays below after one 5s step from 0
  CHECK(snap.cpuLoad1m > 0.05f && snap.cpuLoad1m < 2.0f);
  CHECK(snap.cpuLoad5m > 0.0f && snap.cpuLoad5m < snap.cpuLoad1m + 0.001f);

  // Half idle each core → load 1.0
  monitor.hostSetIdleAccumUs(2500000ULL, 2500000ULL);
  snap = monitor.sample(5000);
  CHECK(snap.cpuLoad5s >= 0.99f && snap.cpuLoad5s <= 1.01f);
  CHECK(snap.cpu0Busy >= 0.49f && snap.cpu0Busy <= 0.51f);
  CHECK(snap.cpu1Busy >= 0.49f && snap.cpu1Busy <= 0.51f);
}

void h03_task_profiler_start_stop_updates_snapshot() {
  resetHarness(false, false);
  TaskProfilerSnapshot snap;
  copyTaskProfiler(snap);
  CHECK(snap.state == TaskProfilerState::NEVER);
  CHECK(snap.stopReason == TaskProfilerStopReason::NONE);
  CHECK(snap.rowCount == 0);

  WebCommand start = {};
  start.type = WebCommandType::TASK_PROFILER_START;
  processWebCommand(start);
  copyTaskProfiler(snap);
  CHECK(snap.state == TaskProfilerState::FAILED);
  CHECK(snap.stopReason == TaskProfilerStopReason::CAPTURE_FAILED);

  WebCommand stop = {};
  stop.type = WebCommandType::TASK_PROFILER_STOP;
  processWebCommand(stop);
  copyTaskProfiler(snap);
  CHECK(snap.state == TaskProfilerState::FAILED);
  CHECK(snap.stopReason == TaskProfilerStopReason::CAPTURE_FAILED);
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
  runtimeConfig.minBbwBrewTimeMs = 26000;
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
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL_FAST);
  CHECK(localBuzzer.activePulseRate ==
        static_cast<uint8_t>(ExtendedPulseRate::FAST));
  const float maxThreshold = effectiveMaxStopThreshold();
  publishWeight(maxThreshold + 0.1f);
  publishWeight(maxThreshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::FAST_EXTRACTION_MAX_WEIGHT);
  CHECK(localBuzzer.activeCue != BuzzerCue::ABNORMAL_FAST);
}

void r51_extended_pulse_respects_alert_flag_and_scale_only() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::OFF);
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(!localBuzzer.looping);

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::FAST);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float scaleOnlyThreshold = effectiveStopThreshold();
  publishWeight(scaleOnlyThreshold + 0.1f);
  publishWeight(scaleOnlyThreshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(!localBuzzer.looping);
}

void r50_guard_extends_and_stops_at_min_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
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

void r56_guard_inhibits_bbw_weight_cut_before_min_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(session.extractionExtended);
  CHECK(session.endReason == EndReason::NONE);
}

void r57_guard_max_weight_cut_from_predicted_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
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
  shot.expectedEndS = 0.0f;
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::FAST_EXTRACTION_MAX_WEIGHT);
}

void r59_slow_guard_on_time_bbw_is_scale_threshold() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  reachSessionElapsed(32000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
  CHECK(!session.slowExtractionExtended);
}

void r60_slow_guard_cuts_at_max_time_when_above_floor() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(32.0f);
  publishWeight(32.1f);
  session.lastAcceptedWeightG = 32.1f;
  currentWeight = 32.1f;
  reachSessionElapsed(44000);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SLOW_EXTRACTION_MAX_TIME);
  CHECK(!session.slowExtractionExtended);
}

void r61_slow_guard_extends_and_stops_at_min_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(20.0f);
  publishWeight(20.1f);
  session.lastAcceptedWeightG = 20.1f;
  currentWeight = 20.1f;
  reachSessionElapsed(44000);
  CHECK(session.slowExtractionExtended);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL);
  CHECK(localBuzzer.activePulseRate ==
        static_cast<uint8_t>(ExtendedPulseRate::FAST));

  const float minThreshold = effectiveMinStopThreshold();
  publishWeight(minThreshold + 0.1f);
  publishWeight(minThreshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SLOW_EXTRACTION_MIN_WEIGHT);
}

void r61b_slow_extended_pulse_uses_slow_rate_setting() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::FAST);
  runtimeConfig.buzzerSlowExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::RAPID);
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(20.0f);
  publishWeight(20.1f);
  session.lastAcceptedWeightG = 20.1f;
  currentWeight = 20.1f;
  reachSessionElapsed(44000);
  CHECK(session.slowExtractionExtended);
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL);
  CHECK(localBuzzer.activePulseRate ==
        static_cast<uint8_t>(ExtendedPulseRate::RAPID));

  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::FAST);
  runtimeConfig.buzzerSlowExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::OFF);
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(20.0f);
  publishWeight(20.1f);
  session.lastAcceptedWeightG = 20.1f;
  currentWeight = 20.1f;
  reachSessionElapsed(44000);
  CHECK(session.slowExtractionExtended);
  CHECK(!localBuzzer.looping);
}

void r61c_extended_pulse_resumes_after_scale_lost_alert() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.autoToManualGuardEnabled = false;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(session.active);
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL_FAST);
  setScaleConnected(false);
  CHECK(localBuzzer.activeCue == BuzzerCue::SCALE_LOST);
  for (uint32_t step = 0; step < 80 && localBuzzer.busy(); ++step) {
    hostMillis += 40;
    hostServiceEspTimer(localBuzzer.phaseTimer);
    localBuzzer.service(hostMillis);
  }
  CHECK(!localBuzzer.busy());
  CHECK(session.active);
  CHECK(session.extractionExtended);
  serviceExtendedPulseAlert();
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL_FAST);
}

void r61d_extended_pulse_pending_cleared_when_unwanted() {
  resetHarness(false, false);
  const BuzzerToneCommand tare =
      deriveBuzzerTone(AlertEvent::TARE, false, 0, 0);
  CHECK(localBuzzer.requestTone(tare));
  CHECK(localBuzzer.activeCue == BuzzerCue::TARE);
  CHECK(startExtendedPulseTrain(0));
  CHECK(localBuzzer.pendingCue == BuzzerCue::ABNORMAL_FAST);
  runtimeConfig.alertOutputChannel =
      static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
  session.active = true;
  session.extractionExtended = true;
  serviceExtendedPulseAlert();
  CHECK(localBuzzer.pendingCue == BuzzerCue::NONE);
  CHECK(localBuzzer.activeCue == BuzzerCue::TARE);
  drainLocalBuzzer();
  CHECK(!localBuzzer.busy());
  CHECK(localBuzzer.activeCue == BuzzerCue::NONE);

  resetHarness(false, false);
  CHECK(localBuzzer.requestTone(tare));
  CHECK(startExtendedPulseTrain(0));
  CHECK(localBuzzer.pendingCue == BuzzerCue::ABNORMAL_FAST);
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::OFF);
  session.active = true;
  session.extractionExtended = true;
  serviceExtendedPulseAlert();
  CHECK(localBuzzer.pendingCue == BuzzerCue::NONE);
  drainLocalBuzzer();
  CHECK(!localBuzzer.busy());
}

void r61e_extended_pulse_restarts_when_rate_setting_changes() {
  resetHarness(false, false);
  CHECK(startExtendedPulseTrain(0));
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL_FAST);
  CHECK(localBuzzer.activePulseRate ==
        static_cast<uint8_t>(ExtendedPulseRate::FAST));
  runtimeConfig.buzzerExtendedPulseRate =
      static_cast<uint8_t>(ExtendedPulseRate::RAPID);
  session.active = true;
  session.extractionExtended = true;
  serviceExtendedPulseAlert();
  CHECK(localBuzzer.activeCue == BuzzerCue::ABNORMAL_FAST);
  CHECK(localBuzzer.activePulseRate ==
        static_cast<uint8_t>(ExtendedPulseRate::RAPID));
}

void r62_fast_extended_is_not_cut_by_slow() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  CHECK(!session.slowExtractionExtended);
  session.lastAcceptedWeightG = 20.0f;
  currentWeight = 20.0f;
  reachSessionElapsed(44000);
  CHECK(session.extractionExtended);
  CHECK(!session.slowExtractionExtended);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  const float maxThreshold = effectiveMaxStopThreshold();
  publishWeight(maxThreshold + 0.1f);
  publishWeight(maxThreshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::FAST_EXTRACTION_MAX_WEIGHT);
}

void r63_slow_guard_disabled_continues_past_max_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = false;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(20.0f);
  publishWeight(20.1f);
  session.lastAcceptedWeightG = 20.1f;
  currentWeight = 20.1f;
  reachSessionElapsed(44000);
  CHECK(stopperState == StopperState::BREW);
  CHECK(!session.slowExtractionExtended);
  CHECK(session.endReason == EndReason::NONE);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
}

void r64_slow_guard_min_weight_cut_from_predicted_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(20.0f);
  publishWeight(20.1f);
  session.lastAcceptedWeightG = 20.1f;
  currentWeight = 20.1f;
  reachSessionElapsed(44000);
  CHECK(session.slowExtractionExtended);
  CHECK(getRelaySafetySnapshot().closed);
  shot.expectedEndS = 0.0f;
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SLOW_EXTRACTION_MIN_WEIGHT);
}

void enableAccidentalTouchForTest() {
  runtimeConfig.avoidAccidentalTouchEnabled = true;
  mutableActiveShotPreset(presetBank).avoidAccidentalTouchEnabled = true;
}

void publishControlRamp(float startG, float endG, float stepG, uint32_t intervalMs,
                        uint32_t sequence) {
  (void)sequence;
  float weight = startG;
  while (weight <= endG + 0.0001f) {
    hostMillis += intervalMs;
    markScaleWorkerProgress();
    publishWeight(weight);
    weight += stepG;
  }
}

void ff01_classifier_seeking_touch_and_release() {
  FirstFlowState state;
  CHECK(stepFirstFlow(state, 0.35f, 100, 1, 0.0f) == FirstFlowClass::CANDIDATE);
  CHECK(stepFirstFlow(state, 0.40f, 200, 2, 0.0f) == FirstFlowClass::FIRE);
  CHECK(state.candidateMs == 100);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 0.4f, 100, 1, 0.0f) == FirstFlowClass::CANDIDATE);
  CHECK(stepFirstFlow(state, 0.9f, 200, 2, 0.0f) == FirstFlowClass::FIRE);
  CHECK(stepFirstFlow(state, 1.5f, 300, 3, 0.0f) == FirstFlowClass::FIRE);
  CHECK(stepFirstFlow(state, 2.4f, 400, 4, 0.0f) == FirstFlowClass::FIRE);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 3.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 3.1f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 3.05f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(state.phase == FirstFlowPhase::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 0.0f, 200, 2, 0.0f) == FirstFlowClass::NONE);
  CHECK(stepFirstFlow(state, 0.05f, 300, 3, 0.0f) == FirstFlowClass::NONE);
  CHECK(state.phase == FirstFlowPhase::SEEKING);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.2f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 0.0f, 300, 3, 0.0f) == FirstFlowClass::NONE);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.2f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.3f, 300, 3, 0.0f) == FirstFlowClass::FIRE);
  CHECK(state.candidateMs == 200);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 4.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 5.9f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 6.1f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 6.2f, 400, 4, 0.0f) == FirstFlowClass::FIRE);
  CHECK(state.candidateMs == 100);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 200.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 200.2f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 199.8f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 160.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 150.0f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 150.1f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 150.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 150.4f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 150.8f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 3.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 3.4f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 3.8f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 6.0f, 200, 2, 0.0f) == FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 6.1f, 300, 3, 0.0f) == FirstFlowClass::TOUCH);
}

void ff12_cupmin_parameter_gates_touch_and_residual() {
  FirstFlowState state;
  CHECK(stepFirstFlow(state, 15.0f, 100, 1, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 15.4f, 200, 2, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 15.8f, 300, 3, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 18.0f, 100, 1, 0.0f, 15.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 18.4f, 200, 2, 0.0f, 15.0f) ==
        FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 12.0f, 200, 2, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 15.0f, 300, 3, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.2f, 200, 2, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.3f, 300, 3, 0.0f, 10.0f) ==
        FirstFlowClass::FIRE);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f, 15.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.2f, 200, 2, 0.0f, 15.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.3f, 300, 3, 0.0f, 15.0f) ==
        FirstFlowClass::FIRE);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 8.0f, 100, 1, 0.0f, 5.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.2f, 200, 2, 0.0f, 5.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.3f, 300, 3, 0.0f, 5.0f) ==
        FirstFlowClass::TOUCH);

  resetFirstFlowState(state);
  CHECK(stepFirstFlow(state, 15.0f, 100, 1, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.2f, 200, 2, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
  CHECK(stepFirstFlow(state, 1.3f, 300, 3, 0.0f, 10.0f) ==
        FirstFlowClass::TOUCH);
}

void ff13_rinse_first_drop_goes_through_stopper() {
  resetHarness(false, true);
  reachReadyFromBoot();
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(session.active);
  CHECK(session.startedWithScale);
  CHECK(session.firstDropMs == 0);
  publishWeight(0.4f, hostMillis + 50, 1, 20);
  publishWeight(0.9f, hostMillis + 150, 1, 21);
  CHECK(session.firstDropMs != 0);
}

void ff14_no_scale_first_drop_does_not_skip_stopper() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachManualNoScaleState();
  CHECK(!session.startedWithScale);
  CHECK(session.firstDropMs == 0);
  (void)recordWeightSampleWithProvenance(0.4f, hostMillis + 50, 20, 1);
  (void)recordWeightSampleWithProvenance(0.9f, hostMillis + 150, 21, 1);
  CHECK(session.firstDropMs == 0);
}

void ff15_orchestrate_post_tare_holds_cup_transitions() {
  resetHarness(false, true);
  reachReadyFromBoot();
  seedCupPresence(80.0f);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  startCycle();
  CHECK(session.awaitingPostTareBaseline);
  publishWeight(0.0f, hostMillis, 1, 20);
  publishWeight(-80.0f, hostMillis + 50, 1, 40);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
}

void at01_classifier_startup_and_trend_math() {
  float times[WEIGHT_TREND_POINT_COUNT];
  float weights[WEIGHT_TREND_POINT_COUNT];
  for (size_t i = 0; i < WEIGHT_TREND_POINT_COUNT; ++i) {
    times[i] = static_cast<float>(i + 1);
    weights[i] = static_cast<float>(i + 1) * 2.0f;
  }
  CHECK(accidentalTouchTrendReady(times, weights, WEIGHT_TREND_POINT_COUNT));
  CHECK(classifyAccidentalTouch(AccidentalTouchPhase::STARTUP, times, weights, 2,
                               5.2f, 1.1f, true, 5.0f, 1.0f, nullptr, 0) ==
        AccidentalTouchClass::OK);
  CHECK(classifyAccidentalTouch(AccidentalTouchPhase::STARTUP, times, weights, 2,
                               13.0f, 1.1f, true, 5.0f, 1.0f, nullptr, 0) ==
        AccidentalTouchClass::TOUCH);
  const float pending[2] = {13.0f, 13.1f};
  CHECK(classifyAccidentalTouch(AccidentalTouchPhase::STARTUP, times, weights, 2,
                               13.2f, 1.3f, true, 5.0f, 1.0f, pending, 2) ==
        AccidentalTouchClass::SUSTAINED);
  CHECK(classifyAccidentalTouch(
            AccidentalTouchPhase::TREND, times, weights,
            WEIGHT_TREND_POINT_COUNT, 20.2f, 10.1f, true, 20.0f, 10.0f, nullptr,
            0) == AccidentalTouchClass::OK);
  CHECK(classifyAccidentalTouch(
            AccidentalTouchPhase::TREND, times, weights,
            WEIGHT_TREND_POINT_COUNT, 28.0f, 10.1f, true, 20.0f, 10.0f, nullptr,
            0) == AccidentalTouchClass::TOUCH);
  CHECK(classifyAccidentalTouch(AccidentalTouchPhase::STARTUP, times, weights, 2,
                               7.0f, 0.2f, true, 5.0f, -0.1f, nullptr, 0) ==
        AccidentalTouchClass::OK);
}

void at02_early_spike_skips_control_trajectory() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  startCycle();
  advanceToBrew();
  publishControlRamp(0.2f, 5.0f, 0.4f, 100, 10);
  CHECK(!session.accidentalTouchHolding);
  const size_t before = shot.datapoints;
  const float expectedEnd = shot.expectedEndS;
  const float lastAccepted = session.lastAcceptedWeightG;
  hostMillis += 100;
  publishWeight(lastAccepted + 8.0f);
  CHECK(session.accidentalTouchHolding);
  CHECK(shot.datapoints == before);
  CHECK(fabsf(shot.expectedEndS - expectedEnd) < 0.001f);
  CHECK(fabsf(session.lastAcceptedWeightG - lastAccepted) < 0.001f);
  CHECK(stopperState == StopperState::BREW);
}

void at03_late_spike_does_not_cut_then_recovers() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  runtimeConfig.goalWeightG = 36;
  mutableActiveShotPreset(presetBank).goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishControlRamp(1.0f, 20.0f, 1.0f, 200, 10);
  CHECK(session.accidentalTouchPhase == AccidentalTouchPhase::TREND);
  const float threshold = effectiveStopThreshold();
  hostMillis += 100;
  publishWeight(threshold + 8.0f);
  hostMillis += 100;
  publishWeight(threshold + 8.1f);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  CHECK(session.accidentalTouchHolding);
  hostMillis += 100;
  publishWeight(20.2f);
  CHECK(!session.accidentalTouchHolding);
  CHECK(stopperState == StopperState::BREW);
}

void at04_smooth_approach_cuts_on_two_samples() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  runtimeConfig.goalWeightG = 36;
  mutableActiveShotPreset(presetBank).goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  const float threshold = effectiveStopThreshold();
  publishControlRamp(1.0f, threshold - 0.4f, 0.4f, 100, 10);
  hostMillis += 100;
  publishWeight(threshold + 0.1f);
  hostMillis += 100;
  publishWeight(threshold + 0.2f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
}

void at05_spike_before_min_bbw_brew_time_does_not_enter_fast_extended() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  mutableActiveShotPreset(presetBank).fastExtractionGuardEnabled = true;
  mutableActiveShotPreset(presetBank).minBbwBrewTimeMs = 26000;
  mutableActiveShotPreset(presetBank).goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishControlRamp(1.0f, 20.0f, 1.0f, 200, 10);
  const float threshold = effectiveStopThreshold();
  hostMillis += 100;
  publishWeight(threshold + 6.0f);
  CHECK(!session.extractionExtended);
  CHECK(session.accidentalTouchHolding);
  CHECK(stopperState == StopperState::BREW);
}

void at06_fast_extended_spike_does_not_cut_until_sustained() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.avoidAccidentalTouchEnabled = false;
  runtimeConfig.fastExtractionGuardEnabled = true;
  runtimeConfig.maxRecoveryWeightG = 42.5f;
  runtimeConfig.minBbwBrewTimeMs = 26000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  runLoopAfter(22000);
  const float threshold = effectiveStopThreshold();
  publishWeight(threshold + 0.1f);
  publishWeight(threshold + 0.2f);
  CHECK(session.extractionExtended);
  session.config.avoidAccidentalTouchEnabled = true;
  runtimeConfig.avoidAccidentalTouchEnabled = true;
  const float maxThreshold = effectiveMaxStopThreshold();
  hostMillis += 100;
  publishWeight(maxThreshold + 0.5f);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.endReason == EndReason::NONE);
  CHECK(session.accidentalTouchHolding);
  hostMillis += 100;
  publishWeight(maxThreshold + 0.5f);
  hostMillis += 100;
  publishWeight(maxThreshold + 0.6f);
  hostMillis += 100;
  publishWeight(maxThreshold + 0.6f);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::FAST_EXTRACTION_MAX_WEIGHT);
}

void at07_feature_off_matches_legacy_threshold_cut() {
  resetHarness(false, true);
  reachReadyFromBoot();
  CHECK(!runtimeConfig.avoidAccidentalTouchEnabled);
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

void at08_retare_and_cup_remove_still_work_with_touch_guard() {
  resetHarness(false, true);
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  enableAccidentalTouchForTest();
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(retareWindowOpen());
  publishStableCupWeight(150.0f, 10);
  CHECK(session.retarePerformed);

  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  seedCupPresence(80.0f);
  startCycle();
  advanceToBrew();
  publishWeight(-3.0f, hostMillis, 1, 70);
  publishWeight(-3.1f, hostMillis + 1, 1, 71);
  runLoopAfter(1);
  CHECK(session.endReason == EndReason::CUP_REMOVED);
}

void at09_slew_recovery_advances_control_weight() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  startCycle();
  advanceToBrew();
  publishControlRamp(0.2f, 5.0f, 0.4f, 100, 10);
  const float lastAccepted = session.lastAcceptedWeightG;
  const uint32_t seq = session.lastAcceptedPacketSequence;
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  hostMillis += 100;
  publishWeight(lastAccepted + 35.0f, hostMillis, 0, seq + 1);
  CHECK(session.weightControlState == WeightControlState::VALIDATING);
  hostMillis += 100;
  publishWeight(lastAccepted + 35.5f, hostMillis, 0, seq + 2);
  hostMillis += 100;
  publishWeight(lastAccepted + 36.0f, hostMillis, 0, seq + 3);
  hostMillis += 100;
  publishWeight(lastAccepted + 36.5f, hostMillis, 0, seq + 4);
  CHECK(session.weightControlState == WeightControlState::ACTIVE);
  CHECK(!session.accidentalTouchHolding);
  CHECK(session.lastAcceptedWeightG > lastAccepted + 30.0f);
  hostMillis += 100;
  publishWeight(lastAccepted + 37.0f, hostMillis, 0, seq + 5);
  CHECK(session.lastAcceptedWeightG > lastAccepted + 36.0f);
}

void at10_holding_does_not_block_slow_extended_at_max_time() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  runtimeConfig.slowExtractionGuardEnabled = true;
  runtimeConfig.minRecoveryWeightG = 30.0f;
  runtimeConfig.maxBbwBrewTimeMs = 44000;
  runtimeConfig.goalWeightG = 36;
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  publishWeight(20.0f);
  publishWeight(20.1f);
  session.lastAcceptedWeightG = 20.1f;
  currentWeight = 20.1f;
  hostMillis += 100;
  publishWeight(28.0f);
  CHECK(session.accidentalTouchHolding);
  CHECK(stopperState == StopperState::BREW);
  reachSessionElapsed(44000);
  CHECK(session.slowExtractionExtended);
  CHECK(stopperState == StopperState::BREW);
}

void at11_pre_cycle_anchor_does_not_treat_first_pour_as_touch() {
  resetHarness(false, true);
  reachReadyFromBoot();
  enableAccidentalTouchForTest();
  runtimeConfig.autoTare = false;
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  currentWeightSequence = 1;
  startCycle();
  advanceToBrew();
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightG = 0.0f;
  session.lastAcceptedWeightAtMs = session.startedAtMs - ACTIVATOR_DEBOUNCE_MS;
  hostMillis += 300;
  publishWeight(2.0f);
  CHECK(!session.accidentalTouchHolding);
  CHECK(session.lastAcceptedWeightG > 1.5f);
}

void startFirstFlowBrew() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
}

void ff02_chorrito_fires_on_second_sample() {
  startFirstFlowBrew();
  publishWeight(0.4f, hostMillis + 50, 1, 20);
  CHECK(session.firstDropMs == 0);
  publishWeight(0.9f, hostMillis + 150, 1, 21);
  CHECK(session.firstDropMs != 0);
  publishWeight(1.5f, hostMillis + 250, 1, 22);
  publishWeight(2.4f, hostMillis + 350, 1, 23);
  CHECK(session.firstDropMs != 0);
}

void ff03_finger_hold_does_not_fire() {
  startFirstFlowBrew();
  publishWeight(3.0f, hostMillis + 50, 1, 20);
  publishWeight(3.1f, hostMillis + 150, 1, 21);
  publishWeight(3.05f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs == 0);
  CHECK(session.firstFlow.phase == FirstFlowPhase::TOUCH);
}

void ff04_finger_release_to_zero_does_not_fire() {
  startFirstFlowBrew();
  publishWeight(8.0f, hostMillis + 50, 1, 20);
  publishWeight(0.0f, hostMillis + 150, 1, 21);
  publishWeight(0.05f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs == 0);
}

void ff05_bounce_through_residual_does_not_fire() {
  startFirstFlowBrew();
  publishWeight(8.0f, hostMillis + 50, 1, 20);
  publishWeight(1.2f, hostMillis + 150, 1, 21);
  publishWeight(0.0f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs == 0);
}

void ff06_touch_during_drops_fires_on_residual() {
  startFirstFlowBrew();
  publishWeight(8.0f, hostMillis + 50, 1, 20);
  CHECK(session.firstDropMs == 0);
  publishWeight(1.2f, hostMillis + 150, 1, 21);
  CHECK(session.firstDropMs == 0);
  publishWeight(1.3f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs != 0);
}

void ff07_gush_then_flow_backdates_to_jump() {
  startFirstFlowBrew();
  const uint32_t jumpAtMs = hostMillis + 50;
  publishWeight(4.0f, jumpAtMs, 1, 20);
  publishWeight(5.9f, jumpAtMs + 100, 1, 21);
  publishWeight(6.1f, jumpAtMs + 200, 1, 22);
  CHECK(session.firstDropMs == 0);
  publishWeight(6.2f, jumpAtMs + 300, 1, 23);
  CHECK(session.firstDropMs == jumpAtMs);
}

void ff08_coffee_during_post_tare_grace_fires() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  CHECK(session.awaitingPostTareBaseline || session.scaleBaselineReady);
  CHECK(executeNextScaleCommand());
  // Empty-pan 0 g already accepted the baseline. A late combined start must
  // not re-arm post-tare hold, or a following cup/coffee sample is swallowed.
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(session.scaleBaselineReady);
  publishWeight(0.4f, hostMillis + 50, 1, 20);
  publishWeight(0.8f, hostMillis + 150, 1, 21);
  publishWeight(1.2f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs != 0);
  CHECK(session.scaleBaselineG < FIRST_DROP_THRESHOLD_G);
}

void ff09_stream_past_half_goal_still_fires() {
  startFirstFlowBrew();
  runtimeConfig.goalWeightG = 36;
  session.config.goalWeightG = 36;
  publishWeight(0.4f, hostMillis + 50, 1, 20);
  publishWeight(0.9f, hostMillis + 150, 1, 21);
  CHECK(session.firstDropMs != 0);
  const uint32_t firstDropMs = session.firstDropMs;
  publishWeight(19.0f, hostMillis + 250, 1, 22);
  publishWeight(19.4f, hostMillis + 350, 1, 23);
  CHECK(session.firstDropMs == firstDropMs);
}

void ff10_control_ramp_records_first_flow() {
  startFirstFlowBrew();
  publishControlRamp(0.2f, 5.0f, 0.4f, 100, 10);
  CHECK(session.firstDropMs != 0);
}

void ff11_cup_and_finger_are_not_first_drop() {
  startFirstFlowBrew();
  publishWeight(200.0f, hostMillis + 50, 1, 20);
  publishWeight(200.2f, hostMillis + 150, 1, 21);
  publishWeight(199.8f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs == 0);
  CHECK(session.firstFlow.phase == FirstFlowPhase::TOUCH);

  startFirstFlowBrew();
  publishWeight(3.0f, hostMillis + 50, 1, 20);
  publishWeight(3.4f, hostMillis + 150, 1, 21);
  publishWeight(3.8f, hostMillis + 250, 1, 22);
  CHECK(session.firstDropMs == 0);
  CHECK(session.firstFlow.phase == FirstFlowPhase::TOUCH);
}

void rt14_cup_overshoot_still_retares() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(160.0f, baseMs, 1, 10);
  publishWeight(150.0f, baseMs + 100U, 1, 11);
  publishWeight(150.1f, baseMs + 200U, 1, 12);
  CHECK(session.firstDropMs == 0);
  publishStableCupWeight(150.0f, 20);
  CHECK(session.retarePerformed);
  CHECK(session.firstDropMs == 0);
  CHECK(!session.flowDuringRetare);
}

void rt15_cup_settle_creep_still_retares() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  const uint32_t baseMs = hostMillis + 1000U;
  publishWeight(150.0f, baseMs, 1, 10);
  publishWeight(150.4f, baseMs + 150U, 1, 11);
  publishWeight(150.8f, baseMs + 300U, 1, 12);
  CHECK(session.firstDropMs == 0);
  CHECK(session.retarePerformed);
  CHECK(!session.flowDuringRetare);
}

void prepareOpenRetareWindow(float minCupG) {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  runtimeConfig.minimumCupWeightG = minCupG;
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
}

void expectRetareWithoutFirstDrop() {
  CHECK(session.retarePerformed);
  CHECK(session.firstDropMs == 0);
  CHECK(!session.flowDuringRetare);
}

void rt16_configured_min_cup_weight_retares() {
  prepareOpenRetareWindow(10.0f);
  publishStableCupWeight(15.0f, 10);
  expectRetareWithoutFirstDrop();

  prepareOpenRetareWindow(10.0f);
  publishStableCupWeight(20.0f, 10);
  expectRetareWithoutFirstDrop();

  prepareOpenRetareWindow(10.0f);
  publishStableCupWeight(10.5f, 10);
  expectRetareWithoutFirstDrop();

  prepareOpenRetareWindow(15.0f);
  publishStableCupWeight(18.0f, 10);
  expectRetareWithoutFirstDrop();

  prepareOpenRetareWindow(15.0f);
  publishStableCupWeight(12.0f, 10);
  CHECK(!session.retarePerformed);
  CHECK(session.firstDropMs == 0);

  prepareOpenRetareWindow(20.0f);
  publishStableCupWeight(25.0f, 10);
  expectRetareWithoutFirstDrop();
}

void rt17_light_cup_landing_and_overshoot_retares() {
  prepareOpenRetareWindow(10.0f);
  const uint32_t landMs = hostMillis + 1000U;
  publishWeight(12.0f, landMs, 1, 10);
  publishWeight(15.0f, landMs + 100U, 1, 11);
  CHECK(session.firstDropMs == 0);
  publishStableCupWeight(15.0f, 20);
  expectRetareWithoutFirstDrop();

  prepareOpenRetareWindow(10.0f);
  const uint32_t overshootMs = hostMillis + 1000U;
  publishWeight(18.0f, overshootMs, 1, 10);
  publishWeight(15.0f, overshootMs + 100U, 1, 11);
  publishWeight(15.1f, overshootMs + 200U, 1, 12);
  CHECK(session.firstDropMs == 0);
  publishStableCupWeight(15.0f, 20);
  expectRetareWithoutFirstDrop();
}

void rt18_late_combined_start_does_not_rearm_after_retare() {
  prepareOpenRetareWindow(10.0f);
  publishStableCupWeight(15.0f, 10);
  expectRetareWithoutFirstDrop();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(!session.awaitingPostTareBaseline);
  ScaleEvent startResult;
  startResult.type = ScaleEventType::TIMER_START_RESULT;
  startResult.cycleId = session.id;
  startResult.commandAttempted = true;
  startResult.writeSucceeded = true;
  startResult.usedCombinedTareStart = true;
  CHECK(publishScaleEvent(startResult, true));
  processScaleWorkerEvents();
  CHECK(!session.awaitingPostTareBaseline);
}

void rt19_late_cup_then_zero_lift_allows_next_shot_retare() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  waitForRetareEnded();
  CHECK(!session.retarePerformed);
  publishStableCupWeight(150.0f, 10);
  CHECK(!session.retarePerformed);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  reachBrewState();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(!session.active);
  publishWeight(0.0f, hostMillis, 1, 80);
  publishWeight(0.0f, hostMillis + 50, 1, 81);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  publishStableCupWeight(150.0f, 90);
  CHECK(session.retarePerformed);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 1);
}

void rt20_late_combined_start_does_not_rearm_when_cup_absent() {
  prepareOpenRetareWindow(10.0f);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  CHECK(!session.awaitingPostTareBaseline);
  ScaleEvent startResult;
  startResult.type = ScaleEventType::TIMER_START_RESULT;
  startResult.cycleId = session.id;
  startResult.commandAttempted = true;
  startResult.writeSucceeded = true;
  startResult.usedCombinedTareStart = true;
  CHECK(publishScaleEvent(startResult, true));
  processScaleWorkerEvents();
  CHECK(!session.awaitingPostTareBaseline);
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  publishStableCupWeight(15.0f, 10);
  expectRetareWithoutFirstDrop();
}

void rt21_empty_pan_at_cycle_start_resyncs_stuck_present() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.autoRetare = true;
  runtimeConfig.bbwProtectionMs = minimumBbwProtectionMs(runtimeConfig);
  startCycle();
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  waitForRetareEnded();
  publishStableCupWeight(150.0f, 10);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  CHECK(!session.retarePerformed);
  reachBrewState();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(!session.active);
  CHECK(cupPresenceState() == CupPresenceState::PRESENT);
  currentWeight = 0.0f;
  currentWeightReceivedAtMs = hostMillis;
  ++currentWeightSequence;
  startCycle();
  CHECK(cupPresenceState() == CupPresenceState::ABSENT);
  CHECK(executeNextScaleCommand());
  establishPostTareBaseline();
  CHECK(retareWindowOpen());
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  publishStableCupWeight(150.0f, 90);
  CHECK(session.retarePerformed);
  CHECK(commandCount(ScaleCommandType::TARE_ONLY) == 1);
}

void pm01_original_bbw_release_after_rinse_keeps_machine_running() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  startCycle();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(machineHidesPhysicalStop());
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(machineHidesPhysicalStop());
}

void pm02_original_bbw_release_inside_rinse_is_rinse() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
}

void pm03_original_no_scale_paddle_off_ends_shot() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  startCycle();
  reachManualNoScaleState();
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm04_original_timer_only_paddle_off_ends_shot() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  runtimeConfig.timerOnly = true;
  startCycle();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm05_original_bbw_promote_then_off_ends_like_natural() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(machineCyclePromotedToNaturalForTest());
  CHECK(!machineHidesPhysicalStop());
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm06_original_bbw_hold_blocks_auto_stop_until_release() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm07_original_bbw_hard_limit_while_held() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  startCycle();
  advanceToBrew();
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::GLOBAL_LIMIT);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm08_natural_paddle_off_after_rinse_still_ends_shot() {
  resetHarness(false, true);
  reachReadyFromBoot();
  CHECK(runtimeConfig.paddleMode == static_cast<uint8_t>(PaddleMode::NATURAL));
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm09_apply_config_accepts_original_paddle_mode() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  processWebCommand(update);
  CHECK(runtimeConfig.paddleMode == static_cast<uint8_t>(PaddleMode::ORIGINAL));
}

void configureOriginalBbwShortWall() {
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  runtimeConfig.operationalWallMs = 10000;
  runtimeConfig.autoToManualGuardEnabled = false;
}

void advanceToOriginalBbwOperationalWall() {
  const uint32_t wallAtMs =
      session.circuitClosedAtMs + session.config.operationalWallMs;
  CHECK(hostMillis <= wallAtMs);
  runLoopAfter(wallAtMs - hostMillis);
}

void pm10_original_bbw_hold_skips_operational_wall() {
  resetHarness(false, true);
  reachReadyFromBoot();
  configureOriginalBbwShortWall();
  startCycle();
  advanceToBrew();
  CHECK(machineCycleHardMaxArmedForTest());
  CHECK(getRelaySafetySnapshot().operationalLimitMs == HARD_MAX_CIRCUIT_CLOSED_MS);
  advanceToOriginalBbwOperationalWall();
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(session.endReason == EndReason::NONE);
}

void pm11_original_bbw_release_honors_operational_wall() {
  resetHarness(false, true);
  reachReadyFromBoot();
  configureOriginalBbwShortWall();
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(machineHidesPhysicalStop());
  advanceToOriginalBbwOperationalWall();
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(session.endReason == EndReason::CONFIGURED_WALL_LIMIT);
  runLoopAfter(1);
  CHECK(stopperState == StopperState::READY);
}

void pm12_auto_bbw_release_after_rinse_keeps_machine_running() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  startCycle();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(machineHidesPhysicalStop());
  CHECK(!machineCycleHardMaxArmedForTest());
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(machineHidesPhysicalStop());
}

void pm13_auto_bbw_hold_allows_auto_stop() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm14_auto_bbw_off_on_off_stays_brewing() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(machineHidesPhysicalStop());
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(!machineCyclePromotedToNaturalForTest());
  CHECK(machineHidesPhysicalStop());
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(machineHidesPhysicalStop());
}

void pm15_auto_no_scale_paddle_off_ends_shot() {
  resetHarness(false, false);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  startCycle();
  reachManualNoScaleState();
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm16_auto_timer_only_paddle_off_ends_shot() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  runtimeConfig.timerOnly = true;
  startCycle();
  advanceToBrew();
  CHECK(stopperState == StopperState::BREW);
  CHECK(!machineHidesPhysicalStop());
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm17_auto_bbw_release_inside_rinse_is_rinse() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  const uint32_t rawOnAt = startCycle();
  releaseAtPhysicalDuration(rawOnAt, runtimeConfig.rinseGestureMs);
  CHECK(stopperState == StopperState::RINSE);
  CHECK(getRelaySafetySnapshot().closed);
}

void pm18_auto_bbw_auto_stop_with_paddle_off_goes_ready() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  startCycle();
  advanceToBrew();
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::SCALE_THRESHOLD);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm19_apply_config_accepts_auto_paddle_mode() {
  resetHarness(false, false);
  reachReadyFromBoot();
  WebCommand update;
  update.type = WebCommandType::APPLY_CONFIG;
  update.config = runtimeConfig;
  update.config.paddleMode = static_cast<uint8_t>(PaddleMode::AUTO);
  processWebCommand(update);
  CHECK(runtimeConfig.paddleMode == static_cast<uint8_t>(PaddleMode::AUTO));
}

void pm20_original_on_off_on_promotes_once_without_extra_polls() {
  resetHarness(false, true);
  reachReadyFromBoot();
  runtimeConfig.paddleMode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
  startCycle();
  advanceToBrew();
  CHECK(!machineCyclePromotedToNaturalForTest());
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::BREW);
  CHECK(!machineCyclePromotedToNaturalForTest());
  setRawPaddle(true);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(machineCyclePromotedToNaturalForTest());
  CHECK(machineLastIntention().holdActive);
  (void)machineLastIntention();
  (void)machineLastIntention();
  CHECK(machineCyclePromotedToNaturalForTest());
  CHECK(stopperState == StopperState::BREW);
  CHECK(getRelaySafetySnapshot().closed);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
  CHECK(session.endReason == EndReason::ACTIVATOR);
  CHECK(!getRelaySafetySnapshot().closed);
}

void pm21_hold_with_scale_keeps_k1_closed_for_five_seconds() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
  runLoopAfter(5000);
  CHECK(stopperState == StopperState::BREW);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
}

void pm22_already_closed_rewrites_pin_if_gpio_dropped() {
  resetHarness(false, false);
  reachReadyFromBoot();
  CHECK(setMachineCircuitClosed(true, 5000));
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
  hostPinLevel[RELAY_GPIO] = RELAY_OPEN_LEVEL;
  CHECK(setMachineCircuitClosed(true, 5000));
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
  CHECK(debugEventExists(DebugCode::RELAY_GPIO_DESYNC));
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().fault == RelaySafetyFault::NONE);

  hostPinLevel[RELAY_GPIO] = RELAY_OPEN_LEVEL;
  serviceRelaySafety();
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().fault == RelaySafetyFault::NONE);
}

void pm23_weight_cut_does_not_reclose_while_paddle_held() {
  resetHarness(false, true);
  reachReadyFromBoot();
  startCycle();
  advanceToBrew();
  endBbwProtectionForTests();
  shot.expectedEndS = 1.0f;
  runLoopAfter(1000);
  loop();
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
#if SHOT_STOPPER_MACHINE_TYPE == 0
  CHECK(paddleReturnReminderActive);
#endif
  const size_t closedWrites = hostRelayClosedWrites;
  runLoopAfter(200);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedWrites);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
}

void pm24_no_scale_hard_limit_does_not_reclose_while_held() {
  resetHarness(false, false);
  reachReadyFromBoot();
  startCycle();
  reachManualNoScaleState();
  runLoopAfter(runtimeConfig.rinseGestureMs + 1);
  reachSessionElapsed(HARD_MAX_CIRCUIT_CLOSED_MS);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  const size_t closedWrites = hostRelayClosedWrites;
  runLoopAfter(200);
  CHECK(stopperState == StopperState::REQUIRES_OFF);
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostRelayClosedWrites == closedWrites);
  setRawPaddle(false);
  runLoopAfter(ACTIVATOR_DEBOUNCE_MS);
  CHECK(stopperState == StopperState::READY);
}

void pm25_web_start_paddle_off_keeps_k1_closed_with_scale() {
  resetHarness(false, true);
  reachReadyFromBoot();
  WebCommand on = webControlCommand(WebCommandType::REMOTE_ON);
  processWebCommand(on);
  CHECK(session.active);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
  runLoopAfter(500);
  CHECK(getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_CLOSED_LEVEL);
}

void pm26_blocked_no_scale_hold_does_not_close_k1() {
  resetHarness(false, false);
  enableNoScaleShotGuardForTest();
  reachReadyFromBoot();
  attemptBlockedNoScaleStart();
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

using TestFunction = void (*)();

struct TestCase {
  const char *id;
  TestFunction function;
};

const TestCase testCases[] = {
    {"T01", t01_boot_with_paddle_off},
    {"T02", t02_boot_with_activator_on},
    {"T03", t03_sustained_on_enters_brew_once},
    {"T04", t04_exact_rinse_boundary_and_duration},
    {"T04B", t04b_rinse_disabled_short_on_off_is_not_rinse},
    {"T04C", t04c_rinse_demote_does_not_learn_offset},
    {"T29", t29_rinse_demote_begin_fail_does_not_enter_rinse},
    {"T05", t05_release_between_rinse_and_brew_is_short_shot},
    {"T06", t06_paddle_off_during_brew},
    {"T07", t07_scale_prediction_requires_release_after_stop},
    {"T08", t08_on_during_rinse_is_ignored},
    {"T09", t09_rinse_ending_on_requires_off},
    {"T10", t10_paddle_bounce_does_not_start_cycle},
    {"T11", t11_ble_loss_suspends_brew_without_late_stop},
    {"T12", t12_global_limit_opens_manual_and_brew_cycles},
    {"T13", t13_reset_path_starts_with_relay_open},
    {"T14", t14_automatic_stop_stays_open_while_activator_on},
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
    {"PM01", pm01_original_bbw_release_after_rinse_keeps_machine_running},
    {"PM02", pm02_original_bbw_release_inside_rinse_is_rinse},
    {"PM03", pm03_original_no_scale_paddle_off_ends_shot},
    {"PM04", pm04_original_timer_only_paddle_off_ends_shot},
    {"PM05", pm05_original_bbw_promote_then_off_ends_like_natural},
    {"PM06", pm06_original_bbw_hold_blocks_auto_stop_until_release},
    {"PM07", pm07_original_bbw_hard_limit_while_held},
    {"PM08", pm08_natural_paddle_off_after_rinse_still_ends_shot},
    {"PM09", pm09_apply_config_accepts_original_paddle_mode},
    {"PM10", pm10_original_bbw_hold_skips_operational_wall},
    {"PM11", pm11_original_bbw_release_honors_operational_wall},
    {"PM12", pm12_auto_bbw_release_after_rinse_keeps_machine_running},
    {"PM13", pm13_auto_bbw_hold_allows_auto_stop},
    {"PM14", pm14_auto_bbw_off_on_off_stays_brewing},
    {"PM15", pm15_auto_no_scale_paddle_off_ends_shot},
    {"PM16", pm16_auto_timer_only_paddle_off_ends_shot},
    {"PM17", pm17_auto_bbw_release_inside_rinse_is_rinse},
    {"PM18", pm18_auto_bbw_auto_stop_with_paddle_off_goes_ready},
    {"PM19", pm19_apply_config_accepts_auto_paddle_mode},
    {"PM20", pm20_original_on_off_on_promotes_once_without_extra_polls},
    {"PM21", pm21_hold_with_scale_keeps_k1_closed_for_five_seconds},
    {"PM22", pm22_already_closed_rewrites_pin_if_gpio_dropped},
    {"PM23", pm23_weight_cut_does_not_reclose_while_paddle_held},
    {"PM24", pm24_no_scale_hard_limit_does_not_reclose_while_held},
    {"PM25", pm25_web_start_paddle_off_keeps_k1_closed_with_scale},
    {"PM26", pm26_blocked_no_scale_hold_does_not_close_k1},
    {"R01", r01_transient_disconnect_suspends_weight_control},
    {"R02", r02_stalled_scale_worker_suspends_until_validated},
    {"R03", r03_non_finite_weights_cannot_corrupt_state_or_offset},
    {"R04", r04_scale_commands_execute_once_and_report_results},
    {"R05", r05_regression_uses_last_ten_valid_samples},
    {"R06", r06_hard_timer_opens_circuit_without_control_loop},
    {"R07", r07_timing_remains_correct_across_millis_wrap},
    {"R08", r08_full_command_queue_forces_safe_manual_cycle},
    {"R09", r09_stop_is_not_retried_after_disconnect_before_execution},
    {"R10", r10_relay_cannot_close_when_hard_timer_cannot_arm},
    {"R11", r11_final_shot_analysis_updates_only_valid_offset},
    {"R12", r12_scale_worker_service_publishes_weight_and_detects_failure},
    {"R12b", r12b_discovery_clears_stale_connected_link_snapshot},
    {"R12c", r12c_connected_link_rssi_samples_and_clears},
    {"R13", r13_full_queue_prevents_stop_without_delaying_relay_open},
    {"R14", r14_invalid_runtime_configuration_is_transactionally_rejected},
    {"R15", r15_gptimer_opens_circuit_without_arduino_or_esp_timer_tasks},
    {"R16", r16_timeout_during_arm_transaction_can_never_close_circuit},
    {"R17", r17_gptimer_arm_failure_prevents_relay_energization},
    {"R18", r18_watchdog_fault_opens_circuit_and_requests_safe_restart},
    {"R18b", r18b_ota_watchdog_restore_failure_requests_safe_restart},
    {"R19", r19_reset_during_close_reopens_without_recovery_lockout},
    {"R19b", r19b_panic_boot_is_ready_for_webui_and_next_circuit_cycle},
    {"R20", r20_three_unsafe_resets_are_latched_as_a_boot_loop},
    {"R20b", r20b_reset_history_keeps_reason_and_previous_uptime},
    {"R20c", r20c_reset_uptime_checkpoint_is_no_more_frequent_than_one_minute},
    {"R20d", r20d_clear_reset_history_keeps_current_reset_reason},
    {"R21", r21_automatic_control_requires_fresh_weight},
    {"R22", r22_confirmed_implausible_weight_does_not_stop},
    {"R23", r23_maintenance_is_canceled_fail_open_by_physical_paddle},
    {"R24", r24_web_control_is_available_without_session_owner},
    {"R25", r25_critical_scale_mailbox_never_blocks_and_keeps_latest},
    {"R26", r26_remote_timer_stop_retries_after_full_queue},
    {"R27", r27_platform_clock_failure_prevents_circuit_close},
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
    {"R56", r56_guard_inhibits_bbw_weight_cut_before_min_time},
    {"R57", r57_guard_max_weight_cut_from_predicted_time},
    {"R58", r58_extended_shot_does_not_learn_weight_offset},
    {"R59", r59_slow_guard_on_time_bbw_is_scale_threshold},
    {"R60", r60_slow_guard_cuts_at_max_time_when_above_floor},
    {"R61", r61_slow_guard_extends_and_stops_at_min_weight},
    {"R61b", r61b_slow_extended_pulse_uses_slow_rate_setting},
    {"R61c", r61c_extended_pulse_resumes_after_scale_lost_alert},
    {"R61d", r61d_extended_pulse_pending_cleared_when_unwanted},
    {"R61e", r61e_extended_pulse_restarts_when_rate_setting_changes},
    {"R62", r62_fast_extended_is_not_cut_by_slow},
    {"R63", r63_slow_guard_disabled_continues_past_max_time},
    {"R64", r64_slow_guard_min_weight_cut_from_predicted_time},
    {"FF01", ff01_classifier_seeking_touch_and_release},
    {"AT01", at01_classifier_startup_and_trend_math},
    {"AT02", at02_early_spike_skips_control_trajectory},
    {"AT03", at03_late_spike_does_not_cut_then_recovers},
    {"AT04", at04_smooth_approach_cuts_on_two_samples},
    {"AT05", at05_spike_before_min_bbw_brew_time_does_not_enter_fast_extended},
    {"AT06", at06_fast_extended_spike_does_not_cut_until_sustained},
    {"AT07", at07_feature_off_matches_legacy_threshold_cut},
    {"AT08", at08_retare_and_cup_remove_still_work_with_touch_guard},
    {"AT09", at09_slew_recovery_advances_control_weight},
    {"AT10", at10_holding_does_not_block_slow_extended_at_max_time},
    {"AT11", at11_pre_cycle_anchor_does_not_treat_first_pour_as_touch},
    {"FF02", ff02_chorrito_fires_on_second_sample},
    {"FF03", ff03_finger_hold_does_not_fire},
    {"FF04", ff04_finger_release_to_zero_does_not_fire},
    {"FF05", ff05_bounce_through_residual_does_not_fire},
    {"FF06", ff06_touch_during_drops_fires_on_residual},
    {"FF07", ff07_gush_then_flow_backdates_to_jump},
    {"FF08", ff08_coffee_during_post_tare_grace_fires},
    {"FF09", ff09_stream_past_half_goal_still_fires},
    {"FF10", ff10_control_ramp_records_first_flow},
    {"FF11", ff11_cup_and_finger_are_not_first_drop},
    {"FF12", ff12_cupmin_parameter_gates_touch_and_residual},
    {"FF13", ff13_rinse_first_drop_goes_through_stopper},
    {"FF14", ff14_no_scale_first_drop_does_not_skip_stopper},
    {"FF15", ff15_orchestrate_post_tare_holds_cup_transitions},
    {"R65", r65_slow_extended_shot_does_not_learn_weight_offset},
    {"R32", r32_old_connection_generation_cannot_update_weight},
    {"R33", r33_weight_mailbox_keeps_latest_without_consumer_gap},
    {"R33b", r33b_stream_gap_counts_connected_inter_packet_silence},
    {"R33c", r33c_weight_update_interval_is_smoothed_and_reset_safely},
    {"R34", r34_suspended_control_recovers_after_three_attributed_samples},
    {"R35", r35_connected_without_weight_stream_is_not_available},
    {"R36", r36_recovered_stale_metrics_count_connected_gaps_only},
    {"R41", r41_negative_weight_in_range_starts_automatic_cycle},
    {"CP01", cp01_zero_pre_tare_weight_blocks_brew},
    {"CP02", cp02_negative_pre_tare_weight_blocks_brew},
    {"CP03", cp03_positive_pre_tare_weight_starts_brew},
    {"CP04", cp04_require_cup_off_allows_zero_start},
    {"CP05", cp05_rinse_without_cup_still_starts},
    {"CP06", cp06_cup_removed_stops_during_bbw_protection},
    {"CP07", cp07_zero_after_tare_does_not_stop},
    {"CP08", cp08_stop_if_cup_removed_off_keeps_negative_weight},
    {"CP09", cp09_blocked_cup_start_is_silent_when_alerts_off},
    {"CP10", cp10_web_paddle_blocks_cup_start_with_double_beep},
    {"CP11", cp11_master_off_allows_zero_pre_tare_start},
    {"CP12", cp12_master_off_does_not_stop_on_negative_weight},
    {"CP13", cp13_pre_tare_negative_packets_do_not_abort},
    {"CP14", cp14_post_tare_noise_does_not_stop},
    {"CP15", cp15_timer_only_ignores_negative_weight},
    {"CP16", cp16_small_negative_noise_does_not_stop},
    {"CP17", cp17_weight_at_removed_threshold_stops},
    {"CP18", cp18_custom_removed_threshold_is_honored},
    {"CP19", cp19_weight_below_present_threshold_blocks_brew},
    {"CP20", cp20_weight_at_present_threshold_starts_brew},
    {"CP21", cp21_custom_present_threshold_is_honored},
    {"CF01", cup_fsm_boot_zero_is_absent},
    {"CF02", cup_fsm_stable_min_cup_is_present},
    {"CF03", cup_fsm_noise_does_not_remove},
    {"CF04", cup_fsm_spike_does_not_place},
    {"CF05", cup_fsm_tare_does_not_change_state},
    {"CF06", cup_fsm_put_back_without_tare_is_present},
    {"CF07", cup_fsm_disconnect_does_not_emit_removed},
    {"CF08", cup_fsm_rinse_does_not_freeze_presence},
    {"CF09", cup_fsm_untared_lift_to_zero_is_removed},
    {"R42", r42_weight_below_automation_min_stays_manual},
    {"R43", r43_post_tare_baseline_accepts_zero_after_pre_tare_weight},
    {"R54", r54_post_tare_baseline_keeps_weight_control},
    {"R66", r66_post_tare_rejected_stream_does_not_enforce_atm},
    {"R67", r67_observed_silence_enforces_atm},
    {"R68", r68_stable_accepted_weight_does_not_suspend},
    {"R69", r69_att_command_harvests_pending_weight},
    {"R44", r44_first_shot_after_reconnect_enters_brew},
    {"R45", r45_slew_rejection_emits_specific_debug_code},
    {"ST01", st01_cycle_elapsed_follows_circuit_immediately},
    {"ST02", st02_scale_timer_stop_waits_until_display_catches_internal},
    {"ST03", st03_scale_timer_stop_when_display_already_at_internal},
    {"ST04", st04_scale_timer_stop_extra_delay_applies_after_catchup},
    {"ST05", st05_scale_timer_stop_catchup_times_out},
    {"ST06", st06_scale_timer_stop_without_valid_timer_is_immediate},
    {"ST07", st07_scale_timer_stop_waits_for_start_before_queueing_stop},
    {"ST08", st08_scale_timer_stop_extra_delay_applies_without_valid_timer},
    {"RT01", rt01_late_cup_triggers_single_retare},
    {"RT02", rt02_sub_minimum_stable_cup_is_ignored},
    {"RT03", rt03_spike_without_stable_cup_does_not_retare},
    {"RT04", rt04_heavy_cup_does_not_stop_during_retare},
    {"RT05", rt05_bbw_protection_timeout_enables_stop_without_beep},
    {"RT06", rt06_first_drops_beep_after_bbw_protection_timeout},
    {"RT07", rt07_auto_retare_off_skips_retare_window},
    {"RT09", rt09_coffee_during_retare_beep_on_first_drop_not_at_retare_end},
    {"RT10", rt10_first_drops_beep_during_bbw_protection},
    {"RT11", rt11_late_retare_records_first_drops_and_keeps_weight_control},
    {"RT12", rt12_early_retare_then_first_drops_are_recorded},
    {"RT13", rt13_auto_tare_off_skips_automatic_retare},
    {"RT14", rt14_cup_overshoot_still_retares},
    {"RT15", rt15_cup_settle_creep_still_retares},
    {"RT16", rt16_configured_min_cup_weight_retares},
    {"RT17", rt17_light_cup_landing_and_overshoot_retares},
    {"RT18", rt18_late_combined_start_does_not_rearm_after_retare},
    {"RT19", rt19_late_cup_then_zero_lift_allows_next_shot_retare},
    {"RT20", rt20_late_combined_start_does_not_rearm_when_cup_absent},
    {"RT21", rt21_empty_pan_at_cycle_start_resyncs_stuck_present},
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
    {"W04b", w04b_select_best_sta_ap_prefers_strongest_matching_bssid},
    {"W05", w05_config_is_blocked_while_brewing_early},
    {"W06", w06_config_is_blocked_while_brewing},
    {"W07", w07_config_is_blocked_while_rinsing},
    {"W08", w08_config_is_blocked_during_manual_cycle},
    {"W09", w09_valid_config_applies_only_from_ready},
    {"W10", w10_cycle_configuration_snapshot_is_immutable},
    {"W11", w11_operational_timer_opens_without_control_loop},
    {"W11b", w11b_timer_only_natural_skips_operational_wall},
    {"W11c", w11c_timer_only_original_skips_operational_wall},
    {"W11d", w11d_timer_only_auto_skips_operational_wall},
    {"W12", w12_hard_limit_cannot_be_configured_above_sixty_seconds},
    {"W45", w45_bbw_protection_retare_relation_is_validated},
    {"W46", w46_status_reports_uptime_since_boot},
    {"W47", w47_status_reports_live_scale_weight_and_timer},
    {"W13", w13_virtual_paddle_uses_normal_state_machine},
    {"W14", w14_physical_motion_overrides_web_control},
    {"W15", w15_web_rinse_starts_scale_timer},
    {"W16", w16_web_stop_during_rinse_preserves_rearm},
    {"W17", w17_web_session_stop_is_a_safe_stop},
    {"W18", w18_web_stop_can_end_a_physical_brew_only_by_opening},
    {"W19", w19_web_start_is_rejected_outside_ready},
    {"W20", w20_restart_waits_while_active},
    {"W20b", w20b_planned_esp_restart_waits_for_shot},
    {"W21", w21_network_change_is_rejected_while_active},
    {"W22", w22_timer_only_disables_predictive_stop},
    {"W23", w23_combined_tare_command_uses_cycle_snapshot},
    {"W24", w24_debug_ring_is_bounded_and_ordered},
    {"W25", w25_weight_samples_do_not_fill_debug_log},
    {"W25B", w25b_log_levels_and_cycle_events_reach_ring},
    {"W25C", w25c_ring_retain_none_skips_ring_and_error_filters},
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
    {"W39", w39_history_mutation_blocked_while_brew_rf_active},
    {"W38", w38_scale_connected_led_tracks_link_and_setting},
    {"W44", w44_paddle_return_reminder_stops_after_fifteen_minutes},
    {"W50", w50_local_buzzer_plays_triple_pattern_non_blocking},
    {"W50b", w50b_buzzer_phase_timer_holds_triple_rhythm_without_loop},
    {"W50e", w50e_buzzer_phase_timer_holds_double_as_truncated_triple},
    {"W50c", w50c_buzzer_phase_timer_advances_despite_loop_stall},
    {"W50d", w50d_recovery_buzzer_patterns_have_exact_timings},
    {"W51", w51_local_buzzer_echo_inverted_on_scale_lost_during_bbw},
    {"W51b", w51b_scale_lost_echo_inverted_when_idle},
    {"W51c", w51c_scale_lost_silent_when_flag_off},
    {"W52", w52_local_buzzer_triple_on_manual_bbw_without_scale},
    {"W53", w53_local_buzzer_silent_when_bbw_off_without_scale},
    {"NS01", ns01_armed_blocks_first_bbw_no_scale_shot},
    {"NS02", ns02_idle_allows_second_bbw_no_scale_shot},
    {"NS03", ns03_bbw_off_does_not_block},
    {"NS04", ns04_scale_available_rearms},
    {"NS05", ns05_cooldown_rearms_from_idle},
    {"NS06", ns06_finished_shot_extends_cooldown},
    {"NS07", ns07_web_rinse_consumes_guard},
    {"NS08", ns08_blocked_beep_respects_alert_checkbox},
    {"NS09", ns09_armed_rinse_gesture_runs_and_consumes_guard},
    {"NS10", ns10_idle_rinse_gesture_does_not_rearm},
    {"NS11", ns11_web_rinse_with_scale_keeps_armed},
    {"NS12", ns12_failed_rinse_does_not_consume_guard},
    {"NS13", ns13_require_scale_blocks_repeated_starts},
    {"NS14", ns14_require_scale_blocks_rinse},
    {"NS15", ns15_require_scale_is_inactive_when_bbw_off},
    {"NS16", ns16_require_scale_reconnect_while_held_needs_new_gesture},
    {"NS17", ns17_require_scale_triple_cycle_temporarily_allows},
    {"NS17b", ns17b_require_scale_double_cycle_stays_blocked},
    {"NS18", ns18_require_scale_triple_cycle_must_fit_window},
    {"W54", w54_local_buzzer_triple_on_auto_to_manual_guard_end},
    {"W55", w55_local_buzzer_queues_second_triple_while_busy},
    {"W56", w56_atm_beep_queued_when_scale_lost_after_deadline},
    {"W57", w57_paddle_return_reminder_falls_back_to_scale_when_piezo_not_ready},
    {"W58", w58_paddle_return_reminder_does_not_advance_interval_when_muted},
    {"W59", w59_local_buzzer_plays_short_long_and_double_patterns},
    {"W60", w60_web_buzzer_test_plays_requested_pattern},
    {"W61", w61_web_buzzer_test_rejected_while_active},
    {"W62", w62_local_buzzer_drive_matches_compile_flag},
    {"W99", w99_rtttl_parser_decodes_notes_and_rests},
    {"W99b", w99b_passive_cue_drives_ledc_note_frequency},
    {"W99c", w99c_select_alert_sink_preserves_channel_rules},
    {"W99d", w99d_pending_rtttl_starts_from_preparsed_catalog},
    {"W99e", w99e_recovery_cue_bypasses_mute_via_pipeline},
    {"W99f", w99f_bullseye_rtttl_is_bounded_and_plays_without_allocation},
    {"W99g", w99g_bullseye_requires_one_second_of_exact_fresh_samples},
    {"W99h", w99h_bullseye_service_runs_only_in_buzzer_only_mode},
    {"W99i", w99i_bullseye_test_never_queues_mutable_custom_notes},
    {"W63", w63_scale_priority_paddle_uses_scale_when_connected},
    {"W64", w64_buzzer_only_first_drop_uses_local_buzzer},
    {"W65", w65_scale_only_mutes_scale_lost},
    {"W66", w66_web_bookoo_debug_dispatches_actions},
    {"W67", w67_web_bookoo_debug_rejected_while_active},
    {"W68", w68_web_bookoo_debug_rejected_when_disconnected},
    {"W69", w69_web_bookoo_debug_rejected_when_not_generic},
    {"W70", w70_bookoo_connect_mutes_in_buzzer_only},
    {"W71", w71_bookoo_connect_sets_volume_in_scale_priority},
    {"W72", w72_bookoo_connect_skips_disabled_volume_and_non_bookoo},
    {"W73", w73_apply_config_buzzer_only_sends_bookoo_silence},
    {"W74", w74_apply_config_enabling_mute_sends_silence_only_in_buzzer_only},
    {"W74b", w74b_sound_alert_master_mutes_and_cancels_all_routes},
    {"W75", w75_bookoo_discovery_connect_applies_beep_policy},
    {"W76", w76_buzzer_only_start_beeps_at_circuit_not_ble_result},
    {"W77", w77_scale_priority_disconnected_beeps_on_circuit_without_ble},
    {"W78", w78_scale_priority_connected_start_beeps_at_circuit},
    {"W79", w79_buzzer_only_stop_beeps_before_timer_stop_result},
    {"W79b", w79b_stop_beeps_at_circuit_while_scale_timer_stop_still_pending},
    {"W79c", w79c_rinse_without_scale_beeps_at_circuit_open_and_close},
    {"W79d", w79d_scale_only_start_and_stop_still_use_local_circuit_beeps},
    {"W80", w80_buzzer_only_retare_beeps_before_tare_result},
    {"W81", w81_scale_priority_failed_start_falls_back_after_disconnect},
    {"W94", w94_eclair_scale_priority_uses_local_alerts},
    {"W95", w95_eclair_scale_only_omits_unsupported_alerts},
    {"W97", w97_eclair_buzzer_only_never_queues_scale_beeps},
    {"W82", w82_pulse_train_loops_until_deadline_or_stopIf},
    {"W83", w83_web_buzzer_test_pulse_uses_same_train_for_3s},
    {"W84", w84_pulse_train_yields_to_triple},
    {"W84b", w84b_echo_inverted_upgrades_weak_pending},
    {"W85", w85_debug_pulse_rates_use_same_on_ms_and_3s},
    {"W86", w86_config_applies_to_ram_immediately_and_coalesces},
    {"W87", w87_nvs_fail_keeps_ram_and_requeues},
    {"W88", w88_save_network_flush_includes_live_runtime},
    {"W89", w89_restart_flush_includes_live_and_aborts_on_fail},
    {"W91", w91_chime_sequence_uses_irregular_note_timings},
    {"W92", w92_parse_sequence_pattern_ids},
    {"W93", w93_scale_connected_echo_on_rising_edge},
    {"W94", w94_scale_connected_silent_when_flag_off_or_scale_only},
    {"W95", w95_web_buzzer_test_plays_chime_sequence},
    {"W96", w96_echo_inverted_uses_long_bookend_tones},
    {"W98", w98_buzzer_sequences_start_and_end_with_sound},
    {"D01", d01_idle_scan_stays_enabled_between_ticks},
    {"D13", d13_idle_delays_relax_without_scale},
    {"D14", d14_control_status_publishes_on_cycle_edge},
    {"D15", d15_companion_publish_throttles_without_scale},
    {"D02", d02_first_mode_uses_name_scan},
    {"D03", d03_scan_start_failed_retries_immediately},
    {"D04", d04_full_cache_keeps_directed_scan},
    {"D05", d05_hci_watchdog_force_restarts_same_filter},
    {"D05b", d05b_scan_intensity_change_restarts_gap},
    {"D05c", d05c_scan_intensity_hci_presets},
    {"D16", d16_scale_connect_debug_reports_phases},
    {"D06", d06_forget_pauses_discovery_for_30s},
    {"D07", d07_prefer_falls_back_after_grace},
    {"D08", d08_select_none_clears_without_pause},
    {"D09", d09_first_mode_connects_seen_advertisement},
    {"D12", d12_advertisement_history_does_not_dirty_persist},
    {"D10", d10_companion_pauses_while_scale_connected_or_connecting},
    {"D10b", d10b_companion_pauses_for_hunt_window_after_scan_start},
    {"D11", d11_select_preferred_is_noop_when_unchanged},
    {"S01", s01_shot_log_filters_short_and_rinse},
    {"S01b", s01b_shot_log_stop_detail_names_end_reasons},
    {"S02", s02_shot_log_appends_after_drip_delay},
    {"S02E", s02e_shot_log_appends_auto_bbw_after_drip_delay},
    {"S02F", s02f_shot_log_skips_sub_one_gram_weight},
    {"S02G", s02g_full_cycle_sub_one_gram_updates_last_shot_not_history},
    {"S02C", s02c_shot_curve_samples_on_two_second_grid_and_latches_slow},
    {"S02D", s02d_shot_curve_latches_first_drop_fast_and_atm},
    {"S02H", s02h_fast_guard_keeps_sampling_and_settled_weight_replaces_endpoint},
    {"S02I", s02i_normal_and_slow_cuts_use_settled_curve_endpoint},
    {"S02B", s02b_drip_delay_is_snapshotted_and_honors_boundaries},
    {"S17", s17_new_cycle_commits_pending_log_as_last_known},
    {"W90", w90_save_unknown_preset_id_does_not_overwrite_active},
    {"S03", s03_shot_log_clear_empties_records},
    {"S14", s14_last_shot_persists_manual_cycle},
    {"S14c", s14c_last_shot_keeps_no_scale_duration},
    {"S14b", s14b_rinse_does_not_overwrite_last_shot},
    {"S14d", s14d_web_stop_during_rinse_does_not_overwrite_last_shot},
    {"S15", s15_last_shot_persists_after_drip_when_eligible},
    {"S15b", s15b_cup_off_after_end_keeps_last_known_actual},
    {"S15c", s15c_last_shot_prefers_last_accepted_over_cup_off},
    {"S16", s16_last_shot_clear_empties_snapshot},
    {"S16b", s16b_factory_reset_hides_last_shot_on_status},
    {"S18", s18_last_shot_keeps_no_scale_guard_from_cycle},
    {"S04", s04_shot_log_remove_by_id},
    {"S04c", s04c_delete_shot_record_removes_log_and_curve},
    {"S04d", s04d_delete_shot_record_ok_without_curve},
    {"S04e", s04e_delete_shot_record_keeps_log_if_curve_remove_fails},
    {"B01", b01_scale_worker_requires_ble_stack},
    {"B02", b02_setup_degrades_without_ble},
#if SHOT_STOPPER_ENABLE_JTAG == 1
    {"B03", b03_jtag_build_starts_serial_without_jumper},
#else
    {"B03", b03_usb_console_stays_off_without_jumper},
#endif
    {"B04", b04_usb_console_starts_when_jumper_held},
    {"M08", m08_recipe_copies_match_published_state},
    {"M09", m09_seqlock_yields_when_writer_active},
    {"M12", m12_ble_companion_result_drop_is_counted},
    {"S04b", s04b_shot_log_page_slice},
    {"S04f", s04f_shot_log_sort_date_and_rating},
    {"S06", s06_shot_log_local_sec_from_utc},
    {"S07", s07_shot_log_stores_fixed_wall_time},
    {"S08", s08_shot_log_without_sync_has_no_wall_time},
    {"S11", s11_shot_log_record_stays_fixed_size},
    {"S12", s12_shot_rating_pack_preserves_guards},
    {"S12b", s12b_shot_log_update_rating},
    {"S12c", s12c_last_shot_rating_survives_finalize_and_commit},
    {"S12d", s12d_rate_last_shot_and_history},
    {"S13", s13_persist_debug_messages_identify_origin},
    {"S19", s19_shot_store_persist_failure_logs_once_until_success},
    {"H01", h01_health_threshold_alerts_fire_once_per_crossing},
    {"H01b", h01b_health_heap_low_restarts_only_when_ready_and_sustained},
    {"H02", h02_hwmon_cpu_load_uses_idle_and_ema},
    {"H03", h03_task_profiler_start_stop_updates_snapshot},
    {"N01", n01_wall_clock_tracks_utc_from_anchor},
    {"N01b", n01b_wall_clock_survives_millis_wrap},
    {"N01c", n01c_wall_clock_cancel_syncing_restores_anchor},
    {"N02", n02_ntp_hostname_validation},
    {"N03", n03_unsynced_retry_is_fifteen_seconds},
    {"RF01", rf01_coex_is_always_bt},
    {"N04", n04_brew_start_requests_ntp_when_unsynced},
    {"N05", n05_rinse_start_requests_ntp_when_unsynced},
    {"N06", n06_synced_clock_skips_activity_ntp_request},
    {"N07", n07_syncing_clock_skips_activity_ntp_request},
    {"N08", n08_web_rinse_requests_ntp_when_unsynced},
    {"SC01", sc01_hello_replies_how_are_you},
    {"SC02", sc02_factory_reset_rejected_while_active},
    {"SC03", sc03_set_wifi_queues_save_network},
    {"SC04", sc04_clear_shots_empties_log},
    {"SC05", sc05_serial_cli_parser_covers_supported_commands},
    {"SC06", sc06_serial_cli_feed_completes_on_crlf},
    {"SC07", sc07_reset_device_password_and_clear_wifi_queue},
    {"SC08", sc08_set_device_password_queues_change},
    {"SC09", sc09_serial_debug_toggles_without_ready},
    {"SC10", sc10_help_prints_one_line_per_command},
    {"SC11", sc11_reboot_queues_restart_when_ready},
    {"SC12", sc12_reboot_waits_while_active},
    {"SC13", sc13_debug_full_and_off_during_cycle},
    {"SC14", sc14_network_actions_queue_without_ready},
    {"SC15", sc15_status_printers_use_dump_views},
    {"SC16", sc16_debug_status_and_log_dump},
    {"BC01", bc01_ble_companion_validates_and_applies_recipe_writes},
    {"BC02", bc02_ble_companion_rejects_config_while_active_but_allows_ap},
    {"BC03", bc03_ble_companion_rejects_legacy_reset_bbw_value},
    {"BC04", bc04_ble_companion_enablement_is_next_boot_only},
    {"BC05", bc05_ble_scan_intensity_applies_live_without_restart},
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
