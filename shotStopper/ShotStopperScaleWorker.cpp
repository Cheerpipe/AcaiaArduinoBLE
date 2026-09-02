#if !defined(SHOT_STOPPER_SCALE_WORKER_IN_ORCHESTRATOR)

#include "ShotStopperScaleWorker.h"

#if defined(SHOT_STOPPER_HOST_TEST)
#include "tests/shot_stopper_host_stubs.h"
#else
#include <ArduinoBLE.h>
#include "ShotStopperNetwork.h"
#include "ShotStopperBleCompanion.h"
#include "ShotStopperWatchdog.h"
#include "ShotStopperHardwareTimer.h"
#endif

#include "ShotStopperAlert.h"
#include "ShotStopperRfCoex.h"
#include "ShotStopperSafety.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>

#endif  // !SHOT_STOPPER_SCALE_WORKER_IN_ORCHESTRATOR

#if !defined(SHOT_STOPPER_SCALE_WORKER_IN_ORCHESTRATOR)
// Orchestrator symbols live in the global namespace (shotStopper.cpp).
uint32_t elapsedMs(uint32_t sinceMs);
void addDebugEvent(shotstopper::DebugCategory category, shotstopper::DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0);
void serialTrace(shotstopper::LogLevel level, const char *message);
void serialTracef(shotstopper::LogLevel level, const char *fmt, ...);
void logEmit(shotstopper::LogLevel level, shotstopper::DebugCategory category,
             shotstopper::DebugCode code, int32_t argument1 = 0,
             int32_t argument2 = 0);
void feedOrTripCurrentTaskWatchdog();
bool feedCurrentTaskWatchdog();
void reportTaskWatchdogFault();
shotstopper::RelaySafetySnapshot getRelaySafetySnapshot();
void commitLiveRuntimeConfig(const shotstopper::RuntimeConfig &composed,
                             int32_t reasonBits);
void resetCupPresence();
bool emitAlert(shotstopper::AlertEvent event, uint32_t cycleId = 0);
shotstopper::BleCompanionStatusSnapshot copyBleCompanionStatus();
void copyBleCompanionRuntimeSnapshot(
    shotstopper::BleCompanionRuntimeSnapshot &output);
void publishBleCompanionStatus(shotstopper::BleCompanionStatusSnapshot status);
bool bleCompanionProfileAllocated();
bool enqueueBleCompanionRequest(const shotstopper::BleCompanionRequest &request);
bool bleCompanionStatusUnchanged(
    const shotstopper::BleCompanionStatusSnapshot &a,
    const shotstopper::BleCompanionStatusSnapshot &b);
bool bleCompanionStatusShouldPublish(bool scaleLinked, bool changed,
                                     uint32_t lastPublishMs, uint32_t nowMs);
extern bool firmwareInitializationComplete;
extern QueueHandle_t bleCompanionRequestQueue;
extern QueueHandle_t bleCompanionResultQueue;
extern shotstopper::RuntimeConfig runtimeConfig;
bool soundAlertsEnabled();
shotstopper::AlertOutputChannel currentAlertOutputChannel();
#if !defined(SHOT_STOPPER_HOST_TEST)
extern shotstopper::ShotStopperNetwork networkManager;
extern shotstopper::ShotStopperBleCompanion *bleCompanion;
#endif
#endif

namespace shotstopper {

constexpr bool DEBUG = false;

EspressoScaleBLE scale(DEBUG);
TaskHandle_t scaleWorkerTaskHandle = nullptr;
QueueHandle_t scaleCommandQueue = nullptr;
QueueHandle_t scaleEventQueue = nullptr;
portMUX_TYPE scaleLinkMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scalePreferredMacMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleBeepMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleDebugMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleCriticalEventMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleWeightEventMux = portMUX_INITIALIZER_UNLOCKED;

ScaleLinkState scaleLinkState = ScaleLinkState::DISCONNECTED;
bool scaleConnecting = false;
bool pendingScaleConnectIdleSync = false;
uint32_t scaleDisconnectSequence = 0;
uint32_t scaleConnectionGeneration = 0;
uint32_t scalePacketSequence = 0;
uint32_t scalePacketGaps = 0;
uint32_t lastScalePacketGapLogMs = 0;
uint32_t lastScaleWeightAtMs = 0;
uint32_t scaleWeightUpdateIntervalMs = 0;
uint32_t scaleRejectedPackets = 0;
uint32_t scaleReconnects = 0;
uint8_t scaleLastDisconnectReason = 0;
bool scaleTimerValid = false;
uint32_t scaleTimerMs = 0;
uint32_t scaleTimerAgeMs = 0;
char scaleProtocolName[20] = "none";
ScaleFeatureSet scaleLinkFeatures = {};
bool scaleLinkRssiValid = false;
int8_t scaleLinkRssi = 0;
uint32_t lastScaleLinkRssiSampleMs = 0;
char scalePreferredMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
char scalePreferredName[PREFERRED_SCALE_NAME_CAPACITY] = {};
ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY] = {};
uint32_t scaleHistorySeq = 0;
bool scalePreferredMacDirty = false;
uint32_t scaleDiscoveryPausedUntilMs = 0;
uint32_t scalePreferredDirectedResetGeneration = 0;
uint32_t scaleWorkerProgressAtMs = 0;
uint32_t scaleEventsDropped = 0;
uint32_t scaleWorkerStackMinWords = 0;
ScaleEvent scaleCriticalEvent;
bool scaleCriticalEventPending = false;
ScaleEvent scaleTimerStartEvent;
bool scaleTimerStartEventPending = false;
ScaleEvent scaleWeightEvent;
bool scaleWeightEventPending = false;
bool scaleBeepPending = false;
uint32_t scaleBeepCycleId = 0;
bool scaleDebugPending = false;
BookooDebugAction scaleDebugAction = BookooDebugAction::START;
uint8_t scaleDebugBeepLevel = 0;
bool scalePaddleReturnReminderBeepPending = false;
bool scaleCompletionBeepPending = false;
uint16_t scaleScanAppliedInterval = 0;
uint16_t scaleScanAppliedWindow = 0;
uint32_t scaleHuntRfUntilMs = 0;
bool scaleLoggedGattConnecting = false;
uint8_t scaleLoggedGattConnectAttempts = 0;
bool scaleDiscoveryDirected = false;
std::atomic<uint8_t> liveBleScanIntensityRaw{
    static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE)};
bool bookooConnectBeepPending = false;
bool bookooConnectBeepSawWeight = false;
uint32_t bookooConnectBeepArmedAtMs = 0;
bool bleStackReady = false;

static uint32_t scaleCommandDropCount = 0;

void scaleWorkerLoadPreferred(const char *mac, const char *name,
                              const ScaleHistoryEntry *history) {
  portENTER_CRITICAL(&scalePreferredMacMux);
  if (mac != nullptr && validPreferredScaleMac(mac)) {
    copyCString(scalePreferredMac, sizeof(scalePreferredMac), mac);
    canonicalizePreferredScaleMac(scalePreferredMac, sizeof(scalePreferredMac));
  }
  if (name != nullptr && validPreferredScaleName(name)) {
    copyCString(scalePreferredName, sizeof(scalePreferredName), name);
  }
  if (history != nullptr) {
    memcpy(scaleHistory, history, sizeof(scaleHistory));
  }
  scaleHistorySeq = 0;
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (scaleHistory[i].mac[0] != '\0') {
      canonicalizePreferredScaleMac(scaleHistory[i].mac,
                                    sizeof(scaleHistory[i].mac));
    }
    if (scaleHistory[i].lastSeenSeq > scaleHistorySeq) {
      scaleHistorySeq = scaleHistory[i].lastSeenSeq;
    }
  }
  seedScaleHistoryFromPreferred(scaleHistory, scaleHistorySeq,
                                scalePreferredMac, scalePreferredName);
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

bool scaleWorkerCopyPreferredIfDirty(char *mac, char *name,
                                     ScaleHistoryEntry *history) {
  bool dirty = false;
  portENTER_CRITICAL(&scalePreferredMacMux);
  dirty = scalePreferredMacDirty;
  if (dirty) {
    if (mac != nullptr) {
      memcpy(mac, scalePreferredMac, sizeof(scalePreferredMac));
    }
    if (name != nullptr) {
      memcpy(name, scalePreferredName, sizeof(scalePreferredName));
    }
    if (history != nullptr) {
      memcpy(history, scaleHistory, sizeof(scaleHistory));
    }
  }
  portEXIT_CRITICAL(&scalePreferredMacMux);
  return dirty;
}

void scaleWorkerClearPreferredDirty() {
  portENTER_CRITICAL(&scalePreferredMacMux);
  scalePreferredMacDirty = false;
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

bool scaleWorkerTakeConnectedEdge() {
  bool edge = false;
  portENTER_CRITICAL(&scaleLinkMux);
  edge = pendingScaleConnectIdleSync;
  pendingScaleConnectIdleSync = false;
  portEXIT_CRITICAL(&scaleLinkMux);
  return edge;
}

void cancelScaleCompletionBeepMailbox() {
  portENTER_CRITICAL(&scaleBeepMux);
  scaleCompletionBeepPending = false;
  portEXIT_CRITICAL(&scaleBeepMux);
}

void cancelOperationalScaleBeeps() {
  cancelScaleCompletionBeepMailbox();
  cancelScalePaddleReturnReminderBeep();
  portENTER_CRITICAL(&scaleBeepMux);
  scaleBeepPending = false;
  scaleBeepCycleId = 0;
  portEXIT_CRITICAL(&scaleBeepMux);
}

ScaleLinkSnapshot getScaleLinkSnapshot() {
  ScaleLinkSnapshot snapshot = {};
  portENTER_CRITICAL(&scaleLinkMux);
  snapshot.state = scaleLinkState;
  snapshot.connecting = scaleConnecting;
  snapshot.disconnectSequence = scaleDisconnectSequence;
  snapshot.connectionGeneration = scaleConnectionGeneration;
  snapshot.packetSequence = scalePacketSequence;
  snapshot.packetGaps = scalePacketGaps;
  snapshot.weightUpdateIntervalMs = scaleWeightUpdateIntervalMs;
  snapshot.rejectedPackets = scaleRejectedPackets;
  snapshot.reconnects = scaleReconnects;
  snapshot.lastDisconnectReason = scaleLastDisconnectReason;
  snapshot.workerProgressAtMs = scaleWorkerProgressAtMs;
  snapshot.timerValid = scaleTimerValid;
  snapshot.timerMs = scaleTimerMs;
  snapshot.timerAgeMs = scaleTimerAgeMs;
  memcpy(snapshot.protocolName, scaleProtocolName, sizeof(snapshot.protocolName));
  snapshot.features = scaleLinkFeatures;
  snapshot.rssiValid = scaleLinkRssiValid;
  snapshot.rssi = scaleLinkRssi;
  portEXIT_CRITICAL(&scaleLinkMux);
  return snapshot;
}

void setScaleLinkState(ScaleLinkState state) {
  const uint32_t progressAtMs = millis();
  ScaleLinkState previous;
  portENTER_CRITICAL(&scaleLinkMux);
  previous = scaleLinkState;
  if (scaleLinkState == ScaleLinkState::CONNECTED &&
      state == ScaleLinkState::DISCONNECTED) {
    ++scaleDisconnectSequence;
    lastScaleWeightAtMs = 0;
    scaleWeightUpdateIntervalMs = 0;
  }
  if (scaleLinkState != ScaleLinkState::CONNECTED &&
      state == ScaleLinkState::CONNECTED) {
    ++scaleConnectionGeneration;
    if (scaleConnectionGeneration == 0) {
      scaleConnectionGeneration = 1;
    }
    pendingScaleConnectIdleSync = true;
  }
  scaleLinkState = state;
  if (state == ScaleLinkState::CONNECTED) {
    scaleConnecting = false;
  }
  scaleWorkerProgressAtMs = progressAtMs;
  if (state != ScaleLinkState::CONNECTED) {
    scaleTimerValid = false;
    scaleTimerMs = 0;
    scaleTimerAgeMs = 0;
    scaleLinkFeatures = scaleFeatureSetNone();
    scaleLinkRssiValid = false;
    scaleLinkRssi = 0;
    lastScaleLinkRssiSampleMs = 0;
  }
  portEXIT_CRITICAL(&scaleLinkMux);
  if (previous != state) {
    addDebugEvent(DebugCategory::SCALE,
                  state == ScaleLinkState::CONNECTED
                      ? DebugCode::SCALE_CONNECTED
                      : DebugCode::SCALE_DISCONNECTED,
                  state == ScaleLinkState::CONNECTED
                      ? 0
                      : static_cast<int32_t>(scaleLastDisconnectReason));
    if (state == ScaleLinkState::CONNECTED &&
        runtimeConfig.buzzerScaleConnectedBeep) {
      emitAlert(AlertEvent::SCALE_CONNECTED);
    } else if (previous == ScaleLinkState::CONNECTED &&
               state != ScaleLinkState::CONNECTED &&
               runtimeConfig.buzzerScaleLostBeep) {
      emitAlert(AlertEvent::SCALE_LOST);
    }
  }
  if (previous == ScaleLinkState::CONNECTED &&
      state != ScaleLinkState::CONNECTED) {
    resetCupPresence();
  }
}

void markScaleWorkerProgress() {
  const uint32_t progressAtMs = millis();
  portENTER_CRITICAL(&scaleLinkMux);
  scaleWorkerProgressAtMs = progressAtMs;
  portEXIT_CRITICAL(&scaleLinkMux);
}

uint32_t scaleWorkerTickDelayMs() {
  if (scale.isLinkUp() || scale.isConnecting()) {
    return 1;
  }
  if (scaleCommandQueue != nullptr &&
      uxQueueMessagesWaiting(scaleCommandQueue) > 0) {
    return 1;
  }
  return SCALE_WORKER_NO_SCALE_DELAY_MS;
}

bool enqueueScaleCommand(const ScaleCommand &command, bool toFront) {
  if (scaleCommandQueue == nullptr) {
    return false;
  }

  BaseType_t queued = pdFALSE;
  if (toFront) {
    queued = xQueueSendToFront(scaleCommandQueue, &command, 0);
  } else {
    queued = xQueueSend(scaleCommandQueue, &command, 0);
  }
  if (queued == pdTRUE) {
    return true;
  }
  ++scaleCommandDropCount;
  serialTrace(LogLevel::WARNING, "Scale command queue full");
  return false;
}

bool publishScaleEvent(const ScaleEvent &event, bool critical) {
  if (event.type == ScaleEventType::WEIGHT) {
    ScaleEvent stamped = event;
    uint32_t streamGapMs = 0;
    portENTER_CRITICAL(&scaleLinkMux);
    if (stamped.connectionGeneration == 0) {
      stamped.connectionGeneration = scaleConnectionGeneration;
    }
    if (stamped.packetSequence == 0) {
      ++scalePacketSequence;
      if (scalePacketSequence == 0) {
        scalePacketSequence = 1;
      }
      stamped.packetSequence = scalePacketSequence;
    }
    if (firmwareInitializationComplete &&
        scaleLinkState == ScaleLinkState::CONNECTED &&
        lastScaleWeightAtMs != 0 &&
        static_cast<int32_t>(stamped.receivedAtMs - lastScaleWeightAtMs) > 0) {
      const uint32_t dt = stamped.receivedAtMs - lastScaleWeightAtMs;
      if (dt > SCALE_STREAM_GAP_MS) {
        ++scalePacketGaps;
        streamGapMs = dt;
        scaleWeightUpdateIntervalMs = 0;
      } else if (scaleWeightUpdateIntervalMs == 0) {
        scaleWeightUpdateIntervalMs = dt;
      } else {
        // Low-cost EWMA over roughly eight updates. Keeping this integer-only
        // makes the diagnostic passive even on the scale worker's hot path.
        scaleWeightUpdateIntervalMs =
            (scaleWeightUpdateIntervalMs * 7U + dt + 4U) / 8U;
      }
    }
    lastScaleWeightAtMs = stamped.receivedAtMs;
    portEXIT_CRITICAL(&scaleLinkMux);

    portENTER_CRITICAL(&scaleWeightEventMux);
    scaleWeightEvent = stamped;
    scaleWeightEventPending = true;
    portEXIT_CRITICAL(&scaleWeightEventMux);
    if (streamGapMs != 0) {
      const uint32_t nowMs = millis();
      if (lastScalePacketGapLogMs == 0 ||
          static_cast<uint32_t>(nowMs - lastScalePacketGapLogMs) >=
              SCALE_PACKET_GAP_LOG_MIN_MS) {
        lastScalePacketGapLogMs = nowMs;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_PACKET_GAP,
                      static_cast<int32_t>(stamped.packetSequence),
                      static_cast<int32_t>(streamGapMs));
      }
    }
    return true;
  }

  if (critical) {
    // Weight events use their own overwrite mailbox. Command results normally
    // use this FIFO; distinct START and STOP fallback slots ensure those two
    // acknowledgements cannot overwrite each other when the FIFO is full.
    if (scaleEventQueue != nullptr &&
        xQueueSend(scaleEventQueue, &event, 0) == pdTRUE) {
      return true;
    }
    portENTER_CRITICAL(&scaleCriticalEventMux);
    ScaleEvent *fallback = &scaleCriticalEvent;
    bool *fallbackPending = &scaleCriticalEventPending;
    if (event.type == ScaleEventType::TIMER_START_RESULT) {
      fallback = &scaleTimerStartEvent;
      fallbackPending = &scaleTimerStartEventPending;
    }
    if (*fallbackPending) {
      ++scaleEventsDropped;
    }
    *fallback = event;
    *fallbackPending = true;
    portEXIT_CRITICAL(&scaleCriticalEventMux);
    return true;
  }
  if (scaleEventQueue == nullptr) {
    ++scaleEventsDropped;
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_EVENT_DROPPED,
                  static_cast<int32_t>(event.type));
    return false;
  }
  if (xQueueSend(scaleEventQueue, &event, 0) != pdTRUE) {
    ++scaleEventsDropped;
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_EVENT_DROPPED,
                  static_cast<int32_t>(event.type));
    return false;
  }
  return true;
}

void updateWorkerLinkState() {
  const bool timerValid = scale.hasTimer();
  const uint32_t timerMs = timerValid ? scale.getTimerMs() : 0;
  const uint32_t timerAgeMs = timerValid ? scale.lastTimerAgeMs() : 0;
  const bool connecting = scale.isConnecting() && !scale.isLinkUp();
  portENTER_CRITICAL(&scaleLinkMux);
  scaleConnecting = connecting;
  scaleRejectedPackets = scale.rejectedPacketCount();
  scaleReconnects = scale.reconnectCount();
  scaleLastDisconnectReason =
      static_cast<uint8_t>(scale.lastDisconnectReason());
  copyCString(scaleProtocolName, sizeof(scaleProtocolName),
              scale.connectedProtocolName());
  scaleLinkFeatures = scale.isLinkUp() ? scale.features()
                                       : scaleFeatureSetNone();
  scaleTimerValid = timerValid;
  scaleTimerMs = timerMs;
  scaleTimerAgeMs = timerAgeMs;
  portEXIT_CRITICAL(&scaleLinkMux);
  setScaleLinkState(scale.isLinkUp() ? ScaleLinkState::CONNECTED
                                        : ScaleLinkState::DISCONNECTED);
}

bool publishPendingScaleWeightEvent() {
  if (!scale.isLinkUp()) {
    return false;
  }
  const bool weightAvailable = scale.newWeightAvailable();
  if (!scale.isLinkUp()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return false;
  }
  if (!weightAvailable) {
    return false;
  }
  ScaleEvent event;
  event.type = ScaleEventType::WEIGHT;
  event.receivedAtMs = millis();
  event.weightG = scale.getWeight();
  publishScaleEvent(event, false);
  return true;
}

void yieldBetweenScaleAttOps() {
  BLE.poll();
  // Harvest notifications that arrived during a blocking ATT write so
  // observedWeight (A→M / suspend) does not freeze while the link is up.
  publishPendingScaleWeightEvent();
  markScaleWorkerProgress();
  feedOrTripCurrentTaskWatchdog();
}

void executeScaleStartCommand(const ScaleCommand &command) {
  ScaleEvent event;
  event.type = ScaleEventType::TIMER_START_RESULT;
  event.cycleId = command.cycleId;
  event.commandFeedbackExpected = command.commandFeedbackExpected;

  if (scale.isConnected()) {
    if (command.canTareStartTimer && command.autoTare &&
        scale.features().has(ScaleFeatureCombinedTareStart)) {
      event.commandAttempted = true;
      event.usedCombinedTareStart = true;
      event.writeSucceeded = scaleCommandOk(scale.tareStartTimer());
      yieldBetweenScaleAttOps();
    }
    if (!event.writeSucceeded) {
      event.usedCombinedTareStart = false;
      bool resetSucceeded = true;
      if (scale.features().has(ScaleFeatureResetTimer)) {
        resetSucceeded = scaleCommandOk(scale.resetTimer());
        yieldBetweenScaleAttOps();
      }
      if (resetSucceeded && scale.features().has(ScaleFeatureStartTimer)) {
        event.commandAttempted = true;
        event.writeSucceeded = scaleCommandOk(scale.startTimer());
        yieldBetweenScaleAttOps();
      }
      if (event.writeSucceeded && command.autoTare &&
          scale.features().has(ScaleFeatureTare)) {
        (void)scale.tare();
        yieldBetweenScaleAttOps();
      }
    }
  }

  updateWorkerLinkState();
  publishScaleEvent(event, true);
}

void executeScaleStopCommand(const ScaleCommand &command) {
  ScaleEvent event;
  event.type = ScaleEventType::TIMER_STOP_RESULT;
  event.cycleId = command.cycleId;
  event.commandFeedbackExpected = command.commandFeedbackExpected;

  if (scale.isConnected()) {
    // A failed start write may only mean its ATT response was lost. Attempting
    // STOP on the existing connection is harmless and covers that case.
    event.commandAttempted = true;
    event.writeSucceeded = scaleCommandOk(scale.stopTimer());
    yieldBetweenScaleAttOps();
  }

  updateWorkerLinkState();
  publishScaleEvent(event, true);
}

void executeScaleTareCommand(const ScaleCommand &command) {
  ScaleEvent event;
  event.type = ScaleEventType::TARE_RESULT;
  event.cycleId = command.cycleId;
  event.commandFeedbackExpected = command.commandFeedbackExpected;

  if (scale.isConnected()) {
    event.commandAttempted = true;
    event.writeSucceeded = scaleCommandOk(scale.tare());
    yieldBetweenScaleAttOps();
  }

  updateWorkerLinkState();
  publishScaleEvent(event, true);
}

void executeScaleBeepCommand(DebugCode successCode, DebugCode failureCode,
                             DebugCode unsupportedCode) {
  if (!scale.isConnected()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    addDebugEvent(DebugCategory::SCALE, failureCode);
    return;
  }
  if (!scale.features().has(ScaleFeatureIndependentBeep)) {
    addDebugEvent(DebugCategory::SCALE, unsupportedCode);
    return;
  }
  const bool succeeded = scaleCommandOk(scale.beepWithoutStateChange());
  yieldBetweenScaleAttOps();
  addDebugEvent(DebugCategory::SCALE, succeeded ? successCode : failureCode);
  updateWorkerLinkState();
}

bool scaleHasVolumeControl() {
  return scale.isConnected() && scale.features().has(ScaleFeatureVolume);
}

bool enqueueScaleDebugCommand(BookooDebugAction action, uint8_t beepLevel) {
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (link.state != ScaleLinkState::CONNECTED ||
      !link.features.has(ScaleFeatureVolume)) {
    return false;
  }
  if (action == BookooDebugAction::VOLUME && beepLevel > BOOKOO_BEEP_LEVEL_MAX) {
    return false;
  }
  portENTER_CRITICAL(&scaleDebugMux);
  const bool busy = scaleDebugPending;
  if (!busy) {
    scaleDebugPending = true;
    scaleDebugAction = action;
    scaleDebugBeepLevel = beepLevel;
  }
  portEXIT_CRITICAL(&scaleDebugMux);
  return !busy;
}

bool takeScaleDebugCommand(BookooDebugAction &action, uint8_t &beepLevel) {
  bool pending = false;
  portENTER_CRITICAL(&scaleDebugMux);
  if (scaleDebugPending) {
    pending = true;
    action = scaleDebugAction;
    beepLevel = scaleDebugBeepLevel;
    scaleDebugPending = false;
    scaleDebugAction = BookooDebugAction::START;
    scaleDebugBeepLevel = 0;
  }
  portEXIT_CRITICAL(&scaleDebugMux);
  return pending;
}

void executeScaleDebugCommand(BookooDebugAction action, uint8_t beepLevel) {
  if (!scale.isConnected()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_FAILED);
    return;
  }
  if (!scaleHasVolumeControl()) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
    return;
  }
  bool succeeded = false;
  switch (action) {
    case BookooDebugAction::START:
      succeeded = scaleCommandOk(scale.startTimer());
      break;
    case BookooDebugAction::STOP:
      succeeded = scaleCommandOk(scale.stopTimer());
      break;
    case BookooDebugAction::TARE:
      succeeded = scaleCommandOk(scale.tare());
      break;
    case BookooDebugAction::COMBINED:
      if (!scale.features().has(ScaleFeatureCombinedTareStart)) {
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
        return;
      }
      succeeded = scaleCommandOk(scale.tareStartTimer());
      break;
    case BookooDebugAction::BEEP:
      if (!scale.features().has(ScaleFeatureIndependentBeep)) {
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
        return;
      }
      succeeded = scaleCommandOk(scale.beepWithoutStateChange());
      break;
    case BookooDebugAction::VOLUME:
      if (!scale.features().has(ScaleFeatureVolume)) {
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
        return;
      }
      succeeded = scaleCommandOk(scale.setBeepLevel(beepLevel));
      break;
  }
  yieldBetweenScaleAttOps();
  addDebugEvent(DebugCategory::SCALE,
                succeeded ? DebugCode::SCALE_DEBUG_OK
                          : DebugCode::SCALE_DEBUG_FAILED);
  updateWorkerLinkState();
}

void applyBookooConnectBeepPolicy() {
  if (!scaleHasVolumeControl() ||
      !scale.features().has(ScaleFeatureIndependentBeep)) {
    return;
  }
  if (!soundAlertsEnabled()) {
    (void)scale.setBeepLevel(0);
    yieldBetweenScaleAttOps();
    return;
  }
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (runtimeConfig.bookooMuteOnBuzzerOnly &&
      channel == AlertOutputChannel::BUZZER_ONLY) {
    (void)scale.setBeepLevel(0);
    yieldBetweenScaleAttOps();
    return;
  }
  if (runtimeConfig.bookooConnectBeepLevel >= 1 &&
      runtimeConfig.bookooConnectBeepLevel <= BOOKOO_BEEP_LEVEL_MAX &&
      (channel == AlertOutputChannel::SCALE_ONLY ||
       channel == AlertOutputChannel::SCALE_PRIORITY)) {
    (void)scale.setBeepLevel(runtimeConfig.bookooConnectBeepLevel);
    yieldBetweenScaleAttOps();
  }
}

void cancelBookooConnectBeepPolicy() {
  bookooConnectBeepPending = false;
  bookooConnectBeepSawWeight = false;
  bookooConnectBeepArmedAtMs = 0;
}

void armBookooConnectBeepPolicy() {
  if (!scaleHasVolumeControl() ||
      !scale.features().has(ScaleFeatureIndependentBeep)) {
    cancelBookooConnectBeepPolicy();
    return;
  }
  bookooConnectBeepPending = true;
  bookooConnectBeepSawWeight = false;
  bookooConnectBeepArmedAtMs = millis();
}

void serviceBookooConnectBeepPolicy(bool sawWeightThisTick) {
  if (!bookooConnectBeepPending) {
    return;
  }
  if (!scale.isLinkUp()) {
    cancelBookooConnectBeepPolicy();
    return;
  }
  if (sawWeightThisTick) {
    bookooConnectBeepSawWeight = true;
    return;
  }
  if (!bookooConnectBeepSawWeight &&
      elapsedMs(bookooConnectBeepArmedAtMs) < BOOKOO_CONNECT_BEEP_DEFER_MS) {
    return;
  }
  bookooConnectBeepPending = false;
  applyBookooConnectBeepPolicy();
}

void requestBookooSilenceIfConfigured() {
  if (!soundAlertsEnabled()) {
    (void)enqueueScaleDebugCommand(BookooDebugAction::VOLUME, 0);
    return;
  }
  if (runtimeConfig.bookooMuteOnBuzzerOnly &&
      currentAlertOutputChannel() == AlertOutputChannel::BUZZER_ONLY) {
    (void)enqueueScaleDebugCommand(BookooDebugAction::VOLUME, 0);
  }
}

void requestBookooAlertVolumeRestore() {
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (soundAlertsEnabled() && runtimeConfig.bookooConnectBeepLevel >= 1 &&
      runtimeConfig.bookooConnectBeepLevel <= BOOKOO_BEEP_LEVEL_MAX &&
      (channel == AlertOutputChannel::SCALE_ONLY ||
       channel == AlertOutputChannel::SCALE_PRIORITY)) {
    (void)enqueueScaleDebugCommand(BookooDebugAction::VOLUME,
                                   runtimeConfig.bookooConnectBeepLevel);
  }
}

void requestScaleBrewBeep(uint32_t cycleId) {
  portENTER_CRITICAL(&scaleBeepMux);
  scaleBeepPending = true;
  scaleBeepCycleId = cycleId;
  portEXIT_CRITICAL(&scaleBeepMux);
}

bool takeScaleBrewBeep(uint32_t &cycleId) {
  bool pending = false;
  portENTER_CRITICAL(&scaleBeepMux);
  if (scaleBeepPending) {
    pending = true;
    cycleId = scaleBeepCycleId;
    scaleBeepPending = false;
    scaleBeepCycleId = 0;
  }
  portEXIT_CRITICAL(&scaleBeepMux);
  return pending;
}

void cancelScaleBrewBeep(uint32_t cycleId) {
  portENTER_CRITICAL(&scaleBeepMux);
  if (scaleBeepPending && scaleBeepCycleId == cycleId) {
    scaleBeepPending = false;
    scaleBeepCycleId = 0;
  }
  portEXIT_CRITICAL(&scaleBeepMux);
}

void requestScalePaddleReturnReminderBeep() {
  portENTER_CRITICAL(&scaleBeepMux);
  scalePaddleReturnReminderBeepPending = true;
  portEXIT_CRITICAL(&scaleBeepMux);
}

bool takeScalePaddleReturnReminderBeep() {
  bool pending = false;
  portENTER_CRITICAL(&scaleBeepMux);
  if (scalePaddleReturnReminderBeepPending) {
    pending = true;
    scalePaddleReturnReminderBeepPending = false;
  }
  portEXIT_CRITICAL(&scaleBeepMux);
  return pending;
}

void cancelScalePaddleReturnReminderBeep() {
  portENTER_CRITICAL(&scaleBeepMux);
  scalePaddleReturnReminderBeepPending = false;
  portEXIT_CRITICAL(&scaleBeepMux);
}

void requestScaleCompletionBeep() {
  portENTER_CRITICAL(&scaleBeepMux);
  scaleCompletionBeepPending = true;
  portEXIT_CRITICAL(&scaleBeepMux);
}

bool takeScaleCompletionBeep() {
  bool pending = false;
  portENTER_CRITICAL(&scaleBeepMux);
  if (scaleCompletionBeepPending) {
    pending = true;
    scaleCompletionBeepPending = false;
  }
  portEXIT_CRITICAL(&scaleBeepMux);
  return pending;
}

void executeScaleCommand(const ScaleCommand &command) {
  BLE.poll();
  publishPendingScaleWeightEvent();
  markScaleWorkerProgress();
  switch (command.type) {
    case ScaleCommandType::START_TIMER_AND_TARE:
      executeScaleStartCommand(command);
      break;
    case ScaleCommandType::TARE_ONLY:
      executeScaleTareCommand(command);
      break;
    case ScaleCommandType::STOP_TIMER:
      executeScaleStopCommand(command);
      break;
  }
}

void copyPreferredScaleMac(char *out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }
  portENTER_CRITICAL(&scalePreferredMacMux);
  copyCString(out, capacity, scalePreferredMac);
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

void copyPreferredScaleName(char *out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }
  portENTER_CRITICAL(&scalePreferredMacMux);
  copyCString(out, capacity, scalePreferredName);
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

void copyScaleHistory(ScaleHistoryEntry *out) {
  if (out == nullptr) {
    return;
  }
  portENTER_CRITICAL(&scalePreferredMacMux);
  memcpy(out, scaleHistory, sizeof(scaleHistory));
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

bool hasPreferredScaleMac() {
  char mac[PREFERRED_SCALE_MAC_CAPACITY];
  copyPreferredScaleMac(mac, sizeof(mac));
  return mac[0] != '\0' && validPreferredScaleMac(mac);
}

uint32_t scaleMacCachePauseRemainingMs(uint32_t nowMs) {
  portENTER_CRITICAL(&scalePreferredMacMux);
  const uint32_t until = scaleDiscoveryPausedUntilMs;
  portEXIT_CRITICAL(&scalePreferredMacMux);
  if (until == 0) {
    return 0;
  }
  const int32_t remaining = static_cast<int32_t>(until - nowMs);
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

bool scaleDiscoveryPaused(uint32_t nowMs) {
  return scaleMacCachePauseRemainingMs(nowMs) > 0;
}

ScaleMacCacheMode currentScaleMacCacheMode() {
  return static_cast<ScaleMacCacheMode>(runtimeConfig.scaleMacCacheMode);
}

bool scaleHuntRfClearActive(uint32_t nowMs = millis()) {
  return scaleHuntRfUntilMs != 0 &&
         static_cast<int32_t>(nowMs - scaleHuntRfUntilMs) < 0;
}

void armScaleHuntRfClear() {
  scaleHuntRfUntilMs = millis() + SCALE_HUNT_RF_CLEAR_MS;
}

// Pause companion advertising while connecting to a scale (GAP connect cannot
// coexist with a peripheral advert), while the machine circuit is closed
// (brew RF preference), while a scale GATT link is up (dual-role advertising
// is the btController hog), and for SCALE_HUNT_RF_CLEAR_MS after GAP (re)start
// so the scanner owns the radio. After that window, scan coexists with
// Companion advertising again.
bool companionAdvertisingShouldPause() {
  return scale.isConnecting() || scale.isLinkUp() ||
         getRelaySafetySnapshot().closed || scaleHuntRfClearActive();
}

void syncCompanionAdvertisingForScaleLink() {
#if !defined(SHOT_STOPPER_HOST_TEST)
  if (bleCompanion != nullptr) {
    bleCompanion->setAdvertisingPaused(companionAdvertisingShouldPause());
  }
#endif
}

void noteScaleHistory(const char *mac, const char *name, bool persist) {
  if (mac == nullptr || !validPreferredScaleMac(mac) || mac[0] == '\0') {
    return;
  }
  portENTER_CRITICAL(&scalePreferredMacMux);
  (void)upsertScaleHistory(scaleHistory, scaleHistorySeq, mac, name);
  if (persist) {
    scalePreferredMacDirty = true;
  }
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

void notePreferredScale(const char *mac, const char *name) {
  noteScaleHistory(mac, name, true);
  const ScaleMacCacheMode cacheMode = currentScaleMacCacheMode();
  // FIRST: never auto-write preferred. PREFER/ONLY: refresh name only when the
  // connected MAC already matches the user-selected preferred (never auto-fill
  // or steal on fallback connect).
  if (cacheMode == ScaleMacCacheMode::FIRST) {
    return;
  }
  if (scaleDiscoveryPaused()) {
    return;
  }
  if (mac == nullptr || !validPreferredScaleMac(mac) || mac[0] == '\0') {
    return;
  }
  char canonicalMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  copyCString(canonicalMac, sizeof(canonicalMac), mac);
  canonicalizePreferredScaleMac(canonicalMac, sizeof(canonicalMac));
  char safeName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name)) {
    copyCString(safeName, sizeof(safeName), name);
  }
  bool changed = false;
  portENTER_CRITICAL(&scalePreferredMacMux);
  if (scalePreferredMac[0] == '\0' ||
      !preferredScaleMacEqual(scalePreferredMac, canonicalMac)) {
    portEXIT_CRITICAL(&scalePreferredMacMux);
    return;
  }
  const bool nameChanged =
      strncmp(scalePreferredName, safeName, PREFERRED_SCALE_NAME_CAPACITY) != 0;
  if (nameChanged) {
    copyCString(scalePreferredName, sizeof(scalePreferredName), safeName);
    scalePreferredMacDirty = true;
    changed = true;
  }
  portEXIT_CRITICAL(&scalePreferredMacMux);
  if (changed) {
    serialTracef(LogLevel::INFO, "Preferred scale name updated: %s — %s",
                 safeName[0] != '\0' ? safeName : "(unknown)", canonicalMac);
  }
}

void coerceScalePreferenceModeToFirst() {
  if (runtimeConfig.scaleMacCacheMode ==
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST)) {
    return;
  }
  RuntimeConfig candidate = runtimeConfig;
  candidate.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
  serialTrace(LogLevel::INFO,
              "Scale preference coerced to first (no preferred)");
}

// Clear preferred without the Forget 30 s pause (dropdown "None").
void clearPreferredScaleSelectionOnly() {
  portENTER_CRITICAL(&scalePreferredMacMux);
  scalePreferredMac[0] = '\0';
  scalePreferredName[0] = '\0';
  scalePreferredMacDirty = true;
  scaleDiscoveryPausedUntilMs = 0;
  ++scalePreferredDirectedResetGeneration;
  portEXIT_CRITICAL(&scalePreferredMacMux);
  serialTrace(LogLevel::INFO, "Preferred scale cleared (history kept)");
  coerceScalePreferenceModeToFirst();
  if (scale.isScanning() || scale.isConnected()) {
    // Restart discovery under the new (unlocked) policy.
    scale.disconnect();
  }
  updateWorkerLinkState();
  setScaleLinkState(ScaleLinkState::DISCONNECTED);
}

void selectPreferredScale(const char *mac, const char *name) {
  if (mac == nullptr || mac[0] == '\0') {
    if (!hasPreferredScaleMac()) {
      return;
    }
    clearPreferredScaleSelectionOnly();
    return;
  }
  if (!validPreferredScaleMac(mac)) {
    return;
  }
  char canonicalMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  copyCString(canonicalMac, sizeof(canonicalMac), mac);
  canonicalizePreferredScaleMac(canonicalMac, sizeof(canonicalMac));
  char currentMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  copyPreferredScaleMac(currentMac, sizeof(currentMac));
  if (preferredScaleMacEqual(currentMac, canonicalMac)) {
    return;
  }
  char resolvedName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name) && name[0] != '\0') {
    copyCString(resolvedName, sizeof(resolvedName), name);
  } else {
    portENTER_CRITICAL(&scalePreferredMacMux);
    findScaleHistoryName(scaleHistory, canonicalMac, resolvedName,
                         sizeof(resolvedName));
    portEXIT_CRITICAL(&scalePreferredMacMux);
  }
  noteScaleHistory(canonicalMac, resolvedName, true);
  portENTER_CRITICAL(&scalePreferredMacMux);
  memcpy(scalePreferredMac, canonicalMac, sizeof(scalePreferredMac));
  copyCString(scalePreferredName, sizeof(scalePreferredName), resolvedName);
  scalePreferredMacDirty = true;
  scaleDiscoveryPausedUntilMs = 0;
  ++scalePreferredDirectedResetGeneration;
  portEXIT_CRITICAL(&scalePreferredMacMux);
  if (scale.isConnected()) {
    const char *connected = scale.address();
    if (connected == nullptr ||
        !preferredScaleMacEqual(connected, canonicalMac)) {
      scale.disconnect();
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::DISCONNECTED);
    }
  } else if (scale.isScanning()) {
    scale.disconnect();
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
  }
  serialTracef(LogLevel::INFO, "Preferred scale selected: %s — %s",
               resolvedName[0] != '\0' ? resolvedName : "(unknown)",
               canonicalMac);
}

void clearPreferredScaleCache() {
  portENTER_CRITICAL(&scalePreferredMacMux);
  scalePreferredMac[0] = '\0';
  scalePreferredName[0] = '\0';
  scalePreferredMacDirty = true;
  scaleDiscoveryPausedUntilMs = millis() + SCALE_PAIRING_DISCOVERY_PAUSE_MS;
  ++scalePreferredDirectedResetGeneration;
  portEXIT_CRITICAL(&scalePreferredMacMux);
  serialTrace(LogLevel::INFO,
              "Paired scale forgotten; looking paused for 30 s");
  coerceScalePreferenceModeToFirst();
}

void logScaleConnectionFailed(bool directed) {
  serialTracef(LogLevel::WARNING, "%s: %s",
               directed ? "Preferred scale connection failed"
                        : "Scale connection failed",
               scale.lastDisconnectReasonName());
}

void logScaleScanStarted(bool directed) {
  scaleDiscoveryDirected = directed;
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_SCAN_STARTED,
                directed ? SCALE_SCAN_TARGET_PREFERRED : SCALE_SCAN_TARGET_ANY,
                static_cast<int32_t>(liveBleScanIntensity()));
}

void logScaleConnectFailed(int32_t step) {
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECT_FAILED,
                static_cast<int32_t>(scale.lastDisconnectReason()), step);
}

bool serviceScaleGattConnecting() {
  feedOrTripCurrentTaskWatchdog();
  const uint8_t stepBefore = scale.connectStepId();
  if (!scaleLoggedGattConnecting) {
    scaleLoggedGattConnecting = true;
    scaleLoggedGattConnectAttempts = 0;
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_GATT_CONNECTING,
                  scaleDiscoveryDirected ? SCALE_SCAN_TARGET_PREFERRED
                                         : SCALE_SCAN_TARGET_ANY);
  }
  const bool connected = scale.pollScan();
  if (connected) {
    scaleLoggedGattConnecting = false;
    scaleLoggedGattConnectAttempts = 0;
    return true;
  }
  const uint8_t attempts = scale.connectAttemptCount();
  if (attempts > scaleLoggedGattConnectAttempts) {
    scaleLoggedGattConnectAttempts = attempts;
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECT_ATTEMPT_FAILED,
                  static_cast<int32_t>(attempts),
                  static_cast<int32_t>(scale.connectStepId()));
  }
  if (!scale.isConnecting()) {
    scaleLoggedGattConnecting = false;
    logScaleConnectFailed(stepBefore);
  }
  return false;
}

// ONLY + preferred MAC → name scan with connect-filter (not GAP directed).
// PREFER uses the same filter until SCALE_PREFER_FALLBACK_MS, then any scale.
bool shouldUseDirectedScaleScan(ScaleMacCacheMode cacheMode, bool hasMac,
                                bool preferStillWaiting) {
  if (!hasMac) {
    return false;
  }
  if (cacheMode == ScaleMacCacheMode::ONLY) {
    return true;
  }
  if (cacheMode == ScaleMacCacheMode::PREFER) {
    return preferStillWaiting;
  }
  return false;
}

bool applyScaleDiscoveryPause() {
  if (!scaleDiscoveryPaused()) {
    return false;
  }
  if (scale.isConnected() || scale.isScanning()) {
    scale.disconnect();
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    cancelBookooConnectBeepPolicy();
  }
  return true;
}

void resetScaleWorkerRadioStateForHost() {
  scaleConnecting = false;
  scaleScanAppliedInterval = 0;
  scaleScanAppliedWindow = 0;
  scaleHuntRfUntilMs = 0;
  scaleLoggedGattConnecting = false;
  scaleLoggedGattConnectAttempts = 0;
  scaleDiscoveryDirected = false;
  liveBleScanIntensityRaw.store(
      static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE),
      std::memory_order_relaxed);
  bookooConnectBeepPending = false;
  bookooConnectBeepSawWeight = false;
  bookooConnectBeepArmedAtMs = 0;
  scaleLinkRssiValid = false;
  scaleLinkRssi = 0;
  lastScaleLinkRssiSampleMs = 0;
}

void serviceScaleLinkRssi(uint32_t nowMs) {
  if (!scale.isConnected()) {
    lastScaleLinkRssiSampleMs = 0;
    portENTER_CRITICAL(&scaleLinkMux);
    scaleLinkRssiValid = false;
    scaleLinkRssi = 0;
    portEXIT_CRITICAL(&scaleLinkMux);
    return;
  }
  if (lastScaleLinkRssiSampleMs != 0 &&
      static_cast<uint32_t>(nowMs - lastScaleLinkRssiSampleMs) <
          SCALE_LINK_RSSI_SAMPLE_MS) {
    return;
  }
  lastScaleLinkRssiSampleMs = nowMs;
  const int raw = scale.linkRssi();
  const bool valid = raw != SCALE_LINK_RSSI_UNAVAILABLE && raw >= -128 &&
                     raw <= 126;
  portENTER_CRITICAL(&scaleLinkMux);
  scaleLinkRssiValid = valid;
  scaleLinkRssi = valid ? static_cast<int8_t>(raw) : 0;
  portEXIT_CRITICAL(&scaleLinkMux);
}

void applyLiveBleScanIntensity(BleScanIntensity intensity) {
  liveBleScanIntensityRaw.store(static_cast<uint8_t>(clampBleScanIntensity(
                                    static_cast<uint8_t>(intensity))),
                                std::memory_order_relaxed);
}

BleScanIntensity liveBleScanIntensity() {
  return clampBleScanIntensity(
      liveBleScanIntensityRaw.load(std::memory_order_relaxed));
}

bool startScaleDiscoveryScan(const char *mac, bool forceRestart) {
  uint16_t interval = BLE_SCAN_NORMAL_INTERVAL;
  uint16_t window = BLE_SCAN_NORMAL_WINDOW;
  bleScanHciParams(liveBleScanIntensity(), interval, window);
  const bool scanningBefore = scale.isScanning();
  const uint16_t prevInterval = scaleScanAppliedInterval;
  const uint16_t prevWindow = scaleScanAppliedWindow;
  const bool addressScan =
      mac != nullptr && mac[0] != '\0' &&
      currentScaleMacCacheMode() == ScaleMacCacheMode::ONLY;
  if (!scale.startScan(mac, forceRestart, interval, window, addressScan)) {
    return false;
  }
  scaleScanAppliedInterval = interval;
  scaleScanAppliedWindow = window;
  if (!scanningBefore || forceRestart || prevInterval != interval ||
      prevWindow != window) {
    armScaleHuntRfClear();
  }
  return true;
}

void fillCurrentScaleScanFilter(char *macOut, size_t cap, bool &useDirected) {
  char preferredMac[PREFERRED_SCALE_MAC_CAPACITY];
  copyPreferredScaleMac(preferredMac, sizeof(preferredMac));
  const bool hasMac =
      preferredMac[0] != '\0' && validPreferredScaleMac(preferredMac);
  useDirected = scale.isDirectedScan() && hasMac;
  if (useDirected) {
    copyCString(macOut, cap, preferredMac);
  } else if (macOut != nullptr && cap > 0) {
    macOut[0] = '\0';
  }
}

void serviceScaleScanIntensity() {
  if (!scale.isScanning() || scale.isConnecting()) {
    return;
  }
  uint16_t interval = BLE_SCAN_NORMAL_INTERVAL;
  uint16_t window = BLE_SCAN_NORMAL_WINDOW;
  bleScanHciParams(liveBleScanIntensity(), interval, window);
  if (scaleScanAppliedInterval == interval &&
      scaleScanAppliedWindow == window) {
    return;
  }
  char mac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  bool useDirected = false;
  fillCurrentScaleScanFilter(mac, sizeof(mac), useDirected);
  (void)startScaleDiscoveryScan(useDirected ? mac : nullptr, true);
}

void syncScaleRadioCoex() {
#if !defined(SHOT_STOPPER_HOST_TEST)
  networkManager.syncScaleLinkRf(scale.isConnecting() || scale.isLinkUp());
  networkManager.syncScaleConnectingRf(scale.isConnecting());
  networkManager.syncScaleHuntRf(scaleHuntRfClearActive());
#endif
}

void serviceScaleWorkerDiscovery(uint32_t &lastScanCycleMs,
                                 uint32_t &lastConnectLogMs,
                                 bool &connectAttemptSeriesActive,
                                 uint32_t &scanSessionAtMs,
                                 uint32_t &scanLastAdvertAtMs) {
  // Library drop can happen on a beep/command path that never refreshed the
  // link snapshot. Clear CONNECTED before idle scan work so the UI cannot sit
  // on "BLE connected" for the whole (indefinite) discovery session.
  if (!scale.isConnected() && !scale.isConnecting()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
  }
  if (applyScaleDiscoveryPause()) {
    return;
  }

  const ScaleMacCacheMode cacheMode = currentScaleMacCacheMode();

  if (scale.isConnecting() || scaleLoggedGattConnecting) {
    // connect() can block up to BLE_CONNECT_TIMEOUT_MS with no inner WDT feed.
    const bool connected = serviceScaleGattConnecting();
    if (connected) {
      connectAttemptSeriesActive = false;
      const char *address = scale.address();
      const char *name = scale.localName();
      notePreferredScale(address, name);
      serialTracef(LogLevel::INFO, "Scale connected: %s @ %s (%s)",
                   name != nullptr && name[0] != '\0' ? name : "(unknown)",
                   address != nullptr && address[0] != '\0' ? address
                                                             : "(no address)",
                   scale.connectedProtocolName());
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::CONNECTED);
      armBookooConnectBeepPolicy();
    } else {
      updateWorkerLinkState();
    }
    return;
  }

  if (scale.isScanning()) {
    feedOrTripCurrentTaskWatchdog();
    const bool connected = scale.pollScan();
    char seenMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
    char seenName[PREFERRED_SCALE_NAME_CAPACITY] = {};
    bool sawCompatibleAd = false;
    if (scale.takeSeenAdvertisement(seenMac, sizeof(seenMac), seenName,
                                    sizeof(seenName))) {
      noteScaleHistory(seenMac, seenName, false);
      sawCompatibleAd = true;
      scanLastAdvertAtMs = millis();
    }
    if (connected) {
      connectAttemptSeriesActive = false;
      const char *address = scale.address();
      const char *name = scale.localName();
      notePreferredScale(address, name);
      serialTracef(LogLevel::INFO, "Scale connected: %s @ %s (%s)",
                   name != nullptr && name[0] != '\0' ? name : "(unknown)",
                   address != nullptr && address[0] != '\0' ? address
                                                             : "(no address)",
                   scale.connectedProtocolName());
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::CONNECTED);
      armBookooConnectBeepPolicy();
      return;
    }
    if (scale.isConnecting()) {
      updateWorkerLinkState();
      return;
    }
    if (scale.isScanning()) {
      serviceScaleScanIntensity();
      if (elapsedMs(lastScanCycleMs) < SCALE_DISCOVERY_TICK_MS) {
        return;
      }
      lastScanCycleMs = millis();
      const bool directed = scale.isDirectedScan();
      const bool logAttempt =
          !connectAttemptSeriesActive ||
          elapsedMs(lastConnectLogMs) >= SCALE_CONNECT_LOG_MS;
      if (logAttempt && !sawCompatibleAd) {
        lastConnectLogMs = lastScanCycleMs;
        connectAttemptSeriesActive = true;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_SCAN_WAITING,
                      SCALE_SCAN_WAIT_NO_ADVERT);
        if (directed) {
          serialTrace(LogLevel::DEBUG,
                      "Preferred scale attempt: no advertisement");
        } else {
          serialTrace(LogLevel::DEBUG,
                      "Scale name scan: no advertisement");
        }
      } else if (logAttempt && directed && sawCompatibleAd) {
        lastConnectLogMs = lastScanCycleMs;
        connectAttemptSeriesActive = true;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_SCAN_WAITING,
                      SCALE_SCAN_WAIT_OTHER_SCALE);
        serialTrace(LogLevel::DEBUG,
                    "Preferred scale attempt: other scale seen, waiting");
      }
      // PREFER: after grace, drop the preferred-only filter and accept any.
      if (cacheMode == ScaleMacCacheMode::PREFER && directed &&
          elapsedMs(scanSessionAtMs) >= SCALE_PREFER_FALLBACK_MS) {
        if (startScaleDiscoveryScan(nullptr, true)) {
          serialTrace(LogLevel::INFO,
                      "Preferred scale not found; falling back to any scale");
          scanLastAdvertAtMs = 0;
          logScaleScanStarted(false);
        }
        return;
      }
      // HCI/GAP force-restart only when the idle scan has gone quiet.
      const bool noAdsThisSession =
          scanLastAdvertAtMs == 0 ||
          elapsedMs(scanLastAdvertAtMs) >= SCALE_SCAN_HCI_RESTART_MS;
      if (elapsedMs(scanSessionAtMs) >= SCALE_SCAN_HCI_RESTART_MS &&
          noAdsThisSession) {
        char preferredMac[PREFERRED_SCALE_MAC_CAPACITY];
        copyPreferredScaleMac(preferredMac, sizeof(preferredMac));
        const bool hasMac =
            preferredMac[0] != '\0' && validPreferredScaleMac(preferredMac);
        const bool preferWaiting =
            cacheMode != ScaleMacCacheMode::PREFER ||
            elapsedMs(scanSessionAtMs) < SCALE_PREFER_FALLBACK_MS;
        const bool useDirected =
            shouldUseDirectedScaleScan(cacheMode, hasMac, preferWaiting);
        if (startScaleDiscoveryScan(useDirected ? preferredMac : nullptr, true)) {
          scanSessionAtMs = lastScanCycleMs;
          scanLastAdvertAtMs = 0;
          logScaleScanStarted(useDirected);
        }
      }
      return;
    }

    lastScanCycleMs = millis();
    const bool finishedDirectedAttempt =
        (cacheMode == ScaleMacCacheMode::ONLY ||
         cacheMode == ScaleMacCacheMode::PREFER) &&
        hasPreferredScaleMac();

    if (finishedDirectedAttempt) {
      serialTracef(LogLevel::WARNING,
                   "Preferred scale attempt: %s",
                   scale.lastDisconnectReasonName());
    }

    const bool logAttempt =
        !connectAttemptSeriesActive ||
        elapsedMs(lastConnectLogMs) >= SCALE_CONNECT_LOG_MS;
    if (finishedDirectedAttempt) {
      lastConnectLogMs = lastScanCycleMs;
      connectAttemptSeriesActive = true;
      logScaleConnectFailed(scale.connectStepId());
    } else if (logAttempt) {
      lastConnectLogMs = lastScanCycleMs;
      connectAttemptSeriesActive = true;
      logScaleConnectFailed(scale.connectStepId());
      logScaleConnectionFailed(false);
    }
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    // Fall through and startScan on this same tick. Do not wait after a
    // connect/GATT/scan-start failure.
  }

  lastScanCycleMs = millis();
  char preferredMac[PREFERRED_SCALE_MAC_CAPACITY];
  copyPreferredScaleMac(preferredMac, sizeof(preferredMac));
  const bool hasMac =
      preferredMac[0] != '\0' && validPreferredScaleMac(preferredMac);
  // PREFER always starts directed; fallback switches mid-session.
  const bool useDirected =
      shouldUseDirectedScaleScan(cacheMode, hasMac, true);
  if (startScaleDiscoveryScan(useDirected ? preferredMac : nullptr, false)) {
    scanSessionAtMs = lastScanCycleMs;
    scanLastAdvertAtMs = 0;
    lastConnectLogMs = lastScanCycleMs;
    connectAttemptSeriesActive = true;
    if (useDirected) {
      serialTracef(LogLevel::INFO, "Scanning for preferred scale %s...",
                   preferredMac);
    } else {
      serialTrace(LogLevel::INFO,
                  "Scanning for any compatible scale (name scan)");
    }
    logScaleScanStarted(useDirected);
    return;
  }

  lastConnectLogMs = lastScanCycleMs;
  connectAttemptSeriesActive = true;
  logScaleConnectFailed(0);
  if (useDirected) {
    serialTrace(LogLevel::WARNING, "Preferred scale scan failed to start");
  } else {
    logScaleConnectionFailed(false);
  }
  updateWorkerLinkState();
  setScaleLinkState(ScaleLinkState::DISCONNECTED);
}

void serviceScaleWorkerLink() {
  if (!scale.isLinkUp()) {
    cancelBookooConnectBeepPolicy();
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return;
  }

  bool snapshotDirty = false;
  if (scale.heartbeatRequired()) {
    if (!scaleCommandOk(scale.heartbeat())) {
      cancelBookooConnectBeepPolicy();
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::DISCONNECTED);
      return;
    }
    snapshotDirty = true;
  }

  const bool sawWeight = publishPendingScaleWeightEvent();
  if (sawWeight) {
    snapshotDirty = true;
  }
  serviceBookooConnectBeepPolicy(sawWeight);
  if (!scale.isLinkUp()) {
    cancelBookooConnectBeepPolicy();
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return;
  }
  if (snapshotDirty) {
    updateWorkerLinkState();
  }
}

void scaleWorkerTask(void *) {
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t scanLastAdvertAtMs = 0;
  uint32_t telemetryAtMs = 0;

  if (!subscribeCurrentTaskToWatchdog()) {
    reportTaskWatchdogFault();
  }

#if !defined(SHOT_STOPPER_HOST_TEST)
  bleStackReady = BLE.begin();
  if (!bleStackReady) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_BLE);
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_BLE, 0);
    scaleWorkerTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  // ArduinoBLE defaults ATT operations to five seconds. Bound every central
  // operation owned by this task so scale-staleness telemetry remains useful.
  BLE.setTimeout(SCALE_ATT_TIMEOUT_MS);
  ensureRfCoexBt();
  logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
          BOOT_SUBSYSTEM_BLE, 1);
  BleCompanionRuntimeSnapshot initialBleSnapshot;
  copyBleCompanionRuntimeSnapshot(initialBleSnapshot);
  if (bleCompanionProfileAllocated()) {
    bleCompanion->begin(enqueueBleCompanionRequest);
    syncCompanionAdvertisingForScaleLink();
    publishBleCompanionStatus(bleCompanion->status());
  } else {
    BleCompanionStatusSnapshot inactiveStatus;
    inactiveStatus.stackReady = true;
    inactiveStatus.apActive = initialBleSnapshot.apActive;
    if (initialBleSnapshot.configuredEnabled) {
      inactiveStatus.lastReject =
          BleCompanionRejectReason::ALLOCATION_FAILED;
    }
    publishBleCompanionStatus(inactiveStatus);
  }
#endif

  for (;;) {
    // Block up to the tick budget waiting for HCI. Same 1 ms connected
    // bound as vTaskDelay(1), but the core sleeps until a packet arrives.
    const uint32_t tickDelayMs = scaleWorkerTickDelayMs();
    BLE.poll(tickDelayMs);

    const uint32_t nowMs = millis();
    static uint32_t lastBackgroundMs = 0;
    const bool backgroundDue =
        lastBackgroundMs == 0 ||
        static_cast<uint32_t>(nowMs - lastBackgroundMs) >=
            SCALE_WORKER_BACKGROUND_MS;
    if (backgroundDue) {
      lastBackgroundMs = nowMs;
      markScaleWorkerProgress();
    }
    // Must run every tick: pollScan() defers GAP connect by one Settle step,
    // and advertising as peripheral during connect() fails on ESP32-S3.
    // setAdvertisingPaused() is a no-op when the pause state is unchanged.
    syncCompanionAdvertisingForScaleLink();
    syncScaleRadioCoex();

#if !defined(SHOT_STOPPER_HOST_TEST)
    if (bleCompanion != nullptr) {
      BleCompanionResult bleResult;
      while (bleCompanionResultQueue != nullptr &&
             xQueueReceive(bleCompanionResultQueue, &bleResult, 0) == pdTRUE) {
        bleCompanion->noteResult(bleResult);
      }
      if (backgroundDue) {
        BleCompanionRuntimeSnapshot bleSnapshot;
        copyBleCompanionRuntimeSnapshot(bleSnapshot);
        bleCompanion->service(bleSnapshot, nowMs);
        const BleCompanionStatusSnapshot status = bleCompanion->status();
        static BleCompanionStatusSnapshot lastCompanionStatus = {};
        static uint32_t lastCompanionPublishMs = 0;
        static bool haveCompanionStatus = false;
        const bool companionChanged =
            !haveCompanionStatus ||
            !bleCompanionStatusUnchanged(status, lastCompanionStatus);
        if (bleCompanionStatusShouldPublish(scale.isLinkUp(), companionChanged,
                                            lastCompanionPublishMs, nowMs)) {
          publishBleCompanionStatus(status);
          lastCompanionStatus = status;
          lastCompanionPublishMs = nowMs;
          haveCompanionStatus = true;
        }
      }
    }
#endif

    // Live GAP check once per tick. Packet timeouts and HCI events cover the
    // rest of the hot path via isLinkUp().
    const bool linked = scale.isConnected();
    static uint32_t lastLinkSnapshotMs = 0;
    const bool linkSnapshotDue =
        lastLinkSnapshotMs == 0 ||
        static_cast<uint32_t>(nowMs - lastLinkSnapshotMs) >=
            SCALE_LINK_SNAPSHOT_INTERVAL_MS;

    // Packet timeout / remote-drop detection must not wait behind beeps or
    // queued commands. Bookoo has no heartbeat; silence is the only watchdog.
    if (linked) {
      connectAttemptSeriesActive = false;
      serviceScaleWorkerLink();
      serviceScaleLinkRssi(nowMs);
      if (linkSnapshotDue) {
        lastLinkSnapshotMs = nowMs;
        updateWorkerLinkState();
      }
    } else if (getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED) {
      cancelBookooConnectBeepPolicy();
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::DISCONNECTED);
      lastLinkSnapshotMs = nowMs;
    }

    // GAP/GATT setup must not wait behind tare/beep/debug: connecting is not
    // linked, so those would otherwise skip advanceConnection() for a tick.
    const bool connecting =
        scale.isConnecting() || scaleLoggedGattConnecting;
    if (connecting) {
      serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                  connectAttemptSeriesActive, scanSessionAtMs,
                                  scanLastAdvertAtMs);
    } else {
      ScaleCommand command;
      if (xQueueReceive(scaleCommandQueue, &command, 0) == pdTRUE) {
        executeScaleCommand(command);
      } else {
        uint32_t beepCycleId = 0;
        if (takeScaleBrewBeep(beepCycleId)) {
          (void)beepCycleId;
          executeScaleBeepCommand(DebugCode::SCALE_BEEP_OK,
                                  DebugCode::SCALE_BEEP_FAILED,
                                  DebugCode::SCALE_BEEP_UNSUPPORTED);
        } else if (takeScalePaddleReturnReminderBeep()) {
          executeScaleBeepCommand(DebugCode::SCALE_PADDLE_REMINDER_BEEP_OK,
                                  DebugCode::SCALE_PADDLE_REMINDER_BEEP_FAILED,
                                  DebugCode::SCALE_PADDLE_REMINDER_BEEP_UNSUPPORTED);
        } else if (takeScaleCompletionBeep()) {
          executeScaleBeepCommand(DebugCode::SCALE_BEEP_OK,
                                  DebugCode::SCALE_BEEP_FAILED,
                                  DebugCode::SCALE_BEEP_UNSUPPORTED);
        } else {
          BookooDebugAction debugAction = BookooDebugAction::START;
          uint8_t debugLevel = 0;
          if (takeScaleDebugCommand(debugAction, debugLevel)) {
            executeScaleDebugCommand(debugAction, debugLevel);
          } else if (!linked && !applyScaleDiscoveryPause()) {
            serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                        connectAttemptSeriesActive,
                                        scanSessionAtMs, scanLastAdvertAtMs);
          }
        }
      }
    }

    // Pause advertising on the same tick beginConnection() sets _connecting,
    // so the next Settle/Connect steps never race a live peripheral advert.
    syncCompanionAdvertisingForScaleLink();
    syncScaleRadioCoex();

    if (!feedCurrentTaskWatchdog()) {
      reportTaskWatchdogFault();
    }
    if (elapsedMs(telemetryAtMs) >= HEALTH_TELEMETRY_INTERVAL_MS) {
      telemetryAtMs = millis();
      scaleWorkerStackMinWords =
          static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    }
  }
}

bool initializeScaleWorker() {
  scaleCommandQueue =
      xQueueCreate(SCALE_COMMAND_QUEUE_LENGTH, sizeof(ScaleCommand));
  scaleEventQueue = xQueueCreate(SCALE_EVENT_QUEUE_LENGTH,
                                 sizeof(ScaleEvent));
  if (bleCompanionProfileAllocated()) {
    bleCompanionRequestQueue = xQueueCreate(BLE_COMPANION_REQUEST_QUEUE_LENGTH,
                                            sizeof(BleCompanionRequest));
    bleCompanionResultQueue = xQueueCreate(BLE_COMPANION_RESULT_QUEUE_LENGTH,
                                           sizeof(BleCompanionResult));
  }
  if (scaleCommandQueue == nullptr || scaleEventQueue == nullptr ||
      (bleCompanionProfileAllocated() &&
       (bleCompanionRequestQueue == nullptr ||
        bleCompanionResultQueue == nullptr))) {
    if (scaleCommandQueue != nullptr) {
      vQueueDelete(scaleCommandQueue);
      scaleCommandQueue = nullptr;
    }
    if (scaleEventQueue != nullptr) {
      vQueueDelete(scaleEventQueue);
      scaleEventQueue = nullptr;
    }
    if (bleCompanionRequestQueue != nullptr) {
      vQueueDelete(bleCompanionRequestQueue);
      bleCompanionRequestQueue = nullptr;
    }
    if (bleCompanionResultQueue != nullptr) {
      vQueueDelete(bleCompanionResultQueue);
      bleCompanionResultQueue = nullptr;
    }
    return false;
  }

  if (xTaskCreatePinnedToCore(scaleWorkerTask, "scale_worker",
                              SCALE_WORKER_TASK_STACK_SIZE, nullptr,
                              tskIDLE_PRIORITY + 1, &scaleWorkerTaskHandle,
                              SCALE_WORKER_TASK_CORE) != pdPASS) {
    vQueueDelete(scaleCommandQueue);
    vQueueDelete(scaleEventQueue);
    scaleCommandQueue = nullptr;
    scaleEventQueue = nullptr;
    if (bleCompanionRequestQueue != nullptr) {
      vQueueDelete(bleCompanionRequestQueue);
    }
    if (bleCompanionResultQueue != nullptr) {
      vQueueDelete(bleCompanionResultQueue);
    }
    bleCompanionRequestQueue = nullptr;
    bleCompanionResultQueue = nullptr;
    scaleWorkerTaskHandle = nullptr;
    return false;
  }
#if !defined(SHOT_STOPPER_HOST_TEST)
  // BLE.begin() runs on this worker. Do not start OTA/Wi-Fi until the
  // controller has finished HCI reset: setup() continues on the same core
  // and network_manager shares core 0 with bleTask.
  const uint32_t bleWaitStartMs = millis();
  while (scaleWorkerTaskHandle != nullptr && !bleStackReady &&
         elapsedMs(bleWaitStartMs) < BLE_STACK_READY_WAIT_MS) {
    feedOrTripCurrentTaskWatchdog();
    delay(20);
  }
#endif
  return scaleWorkerTaskHandle != nullptr && bleStackReady;
}

}  // namespace shotstopper
