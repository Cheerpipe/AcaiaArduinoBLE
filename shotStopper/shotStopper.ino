/*
  Shot Stopper for La Marzocco Micra

  The Micra paddle is connected only to an ESP32 GPIO and GND. The stopper is
  the sole controller of the Micra CN9 circuit through a normally-open relay.

  Paddle ON  (microswitch closed) -> GPIO LOW
  Paddle OFF (microswitch open)   -> GPIO HIGH
  Relay de-energized              -> CN9 open (safe state)

  Released under the MIT license.
  https://github.com/tatemazer/AcaiaArduinoBLE
*/

#if defined(SHOT_STOPPER_HOST_TEST)
#include "tests/shot_stopper_host_stubs.h"
#else
#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include <driver/gpio.h>
#include <esp32-hal-rgb-led.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <math.h>
#include "ShotStopperNetwork.h"
#include "ShotStopperPersistence.h"
#endif

#include "ShotStopperDomain.h"
#include "ShotStopperHardwareTimer.h"
#include "ShotStopperIndicators.h"
#include "ShotStopperResetGuard.h"
#include "ShotStopperSafety.h"
#include "ShotStopperWatchdog.h"

using namespace shotstopper;

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

constexpr uint32_t PADDLE_DEBOUNCE_MS = 30;
constexpr uint32_t DRIP_DELAY_MS = 3000;
constexpr uint32_t SCALE_CONNECT_RETRY_MS = 1000;
constexpr uint32_t SCALE_CONNECT_LOG_MS = 10000;
constexpr uint32_t SCALE_WORKER_STALE_MS = 2000;
constexpr uint32_t SCALE_ATT_TIMEOUT_MS = 1000;
constexpr uint32_t PADDLE_RETURN_REMINDER_BEEP_INTERVAL_MS = 15000;
constexpr size_t SCALE_COMMAND_QUEUE_LENGTH = 12;
constexpr size_t SCALE_EVENT_QUEUE_LENGTH = 64;
constexpr uint32_t SCALE_STOP_RETRY_INTERVAL_MS = 250;
constexpr uint32_t SCALE_STOP_RETRY_WINDOW_MS = 5000;
constexpr uint8_t SCALE_STOP_MAX_ATTEMPTS = 3;
constexpr uint32_t MAINTENANCE_LEASE_SETTLE_MS = 100;
constexpr uint32_t RUNTIME_PERSIST_RETRY_MS = 500;
constexpr uint32_t HEALTH_TELEMETRY_INTERVAL_MS = 5000;
constexpr uint32_t STATUS_INDICATOR_TASK_STACK_SIZE = 3072;

#ifndef SHOT_STOPPER_LED_BRIGHTNESS
#define SHOT_STOPPER_LED_BRIGHTNESS 32
#endif

constexpr uint8_t STATUS_INDICATOR_BRIGHTNESS =
    SHOT_STOPPER_LED_BRIGHTNESS;

constexpr bool DEBUG = false;

// ---------------------------------------------------------------------------
// Board hardware
// ---------------------------------------------------------------------------

#if defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t PADDLE_GPIO = 21;
constexpr uint8_t RELAY_GPIO = 38;
#ifndef SHOT_STOPPER_SCALE_LED_GPIO
#define SHOT_STOPPER_SCALE_LED_GPIO 48
#endif
#ifndef SHOT_STOPPER_STOPPER_LED_GPIO
#define SHOT_STOPPER_STOPPER_LED_GPIO 47
#endif
#elif defined(ARDUINO_NANO_ESP32)
constexpr uint8_t PADDLE_GPIO = 10;
constexpr uint8_t RELAY_GPIO = 11;
#ifndef SHOT_STOPPER_SCALE_LED_GPIO
#define SHOT_STOPPER_SCALE_LED_GPIO 2
#endif
#ifndef SHOT_STOPPER_STOPPER_LED_GPIO
#define SHOT_STOPPER_STOPPER_LED_GPIO 3
#endif
#elif defined(ARDUINO_ESP32_DEV)
// GPIO 27 supports INPUT_PULLUP for the paddle; GPIO 26 drives the relay.
constexpr uint8_t PADDLE_GPIO = 27;
constexpr uint8_t RELAY_GPIO = 26;
#ifndef SHOT_STOPPER_SCALE_LED_GPIO
#define SHOT_STOPPER_SCALE_LED_GPIO 25
#endif
#ifndef SHOT_STOPPER_STOPPER_LED_GPIO
#define SHOT_STOPPER_STOPPER_LED_GPIO 33
#endif
#else
#error "Unsupported board: configure explicit GPIO and WS2812B pins"
#endif

constexpr uint8_t SCALE_STATUS_LED_GPIO = SHOT_STOPPER_SCALE_LED_GPIO;
constexpr uint8_t STOPPER_STATUS_LED_GPIO =
    SHOT_STOPPER_STOPPER_LED_GPIO;

constexpr uint8_t PADDLE_ACTIVE_LEVEL = LOW;
constexpr uint8_t RELAY_CLOSED_LEVEL = LOW;
constexpr uint8_t RELAY_OPEN_LEVEL = HIGH;

#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO) != \
    defined(SHOT_STOPPER_CN9_FEEDBACK_GPIO)
#error "Define both safety heartbeat and CN9 feedback GPIOs, or neither"
#endif

#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO)
constexpr bool EXTERNAL_SAFETY_HARDWARE_PRESENT = true;
constexpr uint8_t SAFETY_HEARTBEAT_GPIO =
    SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO;
constexpr uint8_t CN9_FEEDBACK_GPIO = SHOT_STOPPER_CN9_FEEDBACK_GPIO;
#ifndef SHOT_STOPPER_CN9_FEEDBACK_CLOSED_LEVEL
#define SHOT_STOPPER_CN9_FEEDBACK_CLOSED_LEVEL LOW
#endif
constexpr uint8_t CN9_FEEDBACK_CLOSED_LEVEL =
    SHOT_STOPPER_CN9_FEEDBACK_CLOSED_LEVEL;
#else
constexpr bool EXTERNAL_SAFETY_HARDWARE_PRESENT = false;
constexpr uint8_t SAFETY_HEARTBEAT_GPIO = 0;
constexpr uint8_t CN9_FEEDBACK_GPIO = 0;
constexpr uint8_t CN9_FEEDBACK_CLOSED_LEVEL = LOW;
#endif

constexpr uint32_t SAFETY_HEARTBEAT_TOGGLE_MS = 50;
constexpr uint32_t CN9_FEEDBACK_SETTLE_MS = 100;
constexpr uint32_t RESET_RECOVERY_OFF_DWELL_MS = 1000;

static_assert(PADDLE_GPIO != RELAY_GPIO,
              "Paddle and relay must use different GPIOs");
static_assert(PADDLE_ACTIVE_LEVEL == LOW,
              "Micra paddle wiring requires INPUT_PULLUP and active LOW");
static_assert(RELAY_CLOSED_LEVEL != RELAY_OPEN_LEVEL,
              "Relay open and closed levels must differ");
static_assert((RELAY_OPEN_LEVEL == LOW || RELAY_OPEN_LEVEL == HIGH) &&
                  (RELAY_CLOSED_LEVEL == LOW || RELAY_CLOSED_LEVEL == HIGH),
              "Relay levels must be LOW or HIGH");
static_assert(SHOT_STOPPER_LED_BRIGHTNESS > 0 &&
                  SHOT_STOPPER_LED_BRIGHTNESS <= 255,
              "WS2812B brightness must be between 1 and 255");
static_assert(SCALE_STATUS_LED_GPIO != STOPPER_STATUS_LED_GPIO,
              "Scale and stopper WS2812B LEDs require distinct data GPIOs");
static_assert(SCALE_STATUS_LED_GPIO != RELAY_GPIO &&
                  SCALE_STATUS_LED_GPIO != PADDLE_GPIO &&
                  STOPPER_STATUS_LED_GPIO != RELAY_GPIO &&
                  STOPPER_STATUS_LED_GPIO != PADDLE_GPIO,
              "WS2812B data GPIOs must not share paddle or relay GPIOs");
#ifndef SHOT_STOPPER_HOST_TEST
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(SCALE_STATUS_LED_GPIO),
              "Scale WS2812B must use a valid output-capable GPIO");
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(STOPPER_STATUS_LED_GPIO),
              "Stopper WS2812B must use a valid output-capable GPIO");
#endif
#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO)
static_assert(SAFETY_HEARTBEAT_GPIO != RELAY_GPIO &&
                  SAFETY_HEARTBEAT_GPIO != PADDLE_GPIO &&
                  CN9_FEEDBACK_GPIO != RELAY_GPIO &&
                  CN9_FEEDBACK_GPIO != PADDLE_GPIO &&
                  CN9_FEEDBACK_GPIO != SAFETY_HEARTBEAT_GPIO,
              "Safety GPIOs must be unique");
static_assert(SAFETY_HEARTBEAT_GPIO != SCALE_STATUS_LED_GPIO &&
                  SAFETY_HEARTBEAT_GPIO != STOPPER_STATUS_LED_GPIO &&
                  CN9_FEEDBACK_GPIO != SCALE_STATUS_LED_GPIO &&
                  CN9_FEEDBACK_GPIO != STOPPER_STATUS_LED_GPIO,
              "Safety GPIOs must not share a WS2812B data pin");
#ifndef SHOT_STOPPER_HOST_TEST
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(SAFETY_HEARTBEAT_GPIO),
              "Heartbeat must use a valid output-capable GPIO");
static_assert(GPIO_IS_VALID_GPIO(CN9_FEEDBACK_GPIO),
              "CN9 feedback must use a valid input GPIO");
#endif
static_assert(CN9_FEEDBACK_CLOSED_LEVEL == LOW ||
                  CN9_FEEDBACK_CLOSED_LEVEL == HIGH,
              "CN9 feedback level must be LOW or HIGH");
#endif
static_assert(PADDLE_DEBOUNCE_MS > 0,
              "Paddle debounce must be greater than zero");
static_assert(PADDLE_DEBOUNCE_MS < 100,
              "Paddle debounce must fit every valid rinse gesture");
static_assert(SCALE_WORKER_STALE_MS > PADDLE_DEBOUNCE_MS &&
                  SCALE_WORKER_STALE_MS < HARD_MAX_CN9_CLOSED_MS,
              "Scale worker stale timeout must be useful and safety-bounded");

// ---------------------------------------------------------------------------
// Persistent storage and scale prediction
// ---------------------------------------------------------------------------

constexpr size_t EEPROM_SIZE = 2;
constexpr size_t WEIGHT_ADDR = 0;
constexpr size_t OFFSET_ADDR = 1;
constexpr size_t TREND_POINT_COUNT = 10;
constexpr size_t MAX_SHOT_DATAPOINTS = 1000;

enum class ScaleLinkState : uint8_t {
  DISCONNECTED,
  CONNECTED
};

enum class ScaleCommandType : uint8_t {
  START_TIMER_AND_TARE,
  STOP_TIMER
};

enum class ScaleEventType : uint8_t {
  WEIGHT,
  TIMER_START_RESULT,
  TIMER_STOP_RESULT
};

enum class TimerStopResult : uint8_t {
  NOT_REQUIRED,
  NOT_ATTEMPTED,
  PENDING,
  WRITE_SUCCEEDED,
  WRITE_FAILED
};

struct ShotTrajectory {
  uint32_t startMs = 0;
  float expectedEndS = HARD_MAX_CN9_CLOSED_MS / 1000.0f;
  float weight[MAX_SHOT_DATAPOINTS] = {};
  float timeS[MAX_SHOT_DATAPOINTS] = {};
  size_t datapoints = 0;
  bool confirmedBrew = false;
};

struct CycleSession {
  bool active = false;
  bool automaticEnabled = false;
  bool startedWithScale = false;
  bool scaleWasLost = false;
  bool timerStartCommandQueued = false;
  bool remoteTimerMayBeRunning = false;
  bool remoteTimerStarted = false;
  bool stopTimerRequested = false;
  bool stopTimerCommandQueued = false;
  bool receivedFreshWeightInCycle = false;
  bool calibrationEligible = false;
  bool hasWeightAnchor = false;
  bool directStopPending = false;
  bool awaitingPostTareBaseline = false;
  TimerStopResult timerStopResult = TimerStopResult::NOT_REQUIRED;
  WeightControlState weightControlState = WeightControlState::INACTIVE;
  EndReason directStopReason = EndReason::NONE;
  uint8_t stopTimerAttempts = 0;
  uint8_t thresholdConfirmations = 0;
  uint8_t recoveryConfirmations = 0;
  uint32_t id = 0;
  uint32_t webSessionId = 0;
  uint32_t controlLeaseId = 0;
  uint32_t scaleDisconnectSequenceAtStart = 0;
  uint32_t weightSequenceAtStart = 0;
  uint32_t connectionGenerationAtStart = 0;
  uint32_t ownedConnectionGeneration = 0;
  uint32_t startedAtMs = 0;
  uint32_t rinseStartedAtMs = 0;
  uint32_t postTareBaselineDeadlineMs = 0;
  uint32_t stopTimerRetryDeadlineMs = 0;
  uint32_t stopTimerLastAttemptMs = 0;
  uint32_t lastAcceptedWeightAtMs = 0;
  uint32_t lastAcceptedPacketSequence = 0;
  uint32_t lastThresholdAtMs = 0;
  uint32_t lastThresholdPacketSequence = 0;
  uint32_t lastThresholdConnectionGeneration = 0;
  uint32_t recoveryLastAtMs = 0;
  uint32_t recoveryLastPacketSequence = 0;
  float lastAcceptedWeightG = 0.0f;
  float recoveryLastWeightG = 0.0f;
  ControlSource source = ControlSource::NONE;
  CycleConfigSnapshot config = {};
  EndReason endReason = EndReason::NONE;
};

struct PendingShotAnalysis {
  bool pending = false;
  uint32_t endedAtMs = 0;
  uint32_t endedWeightSequence = 0;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
};

struct ScaleCommand {
  ScaleCommandType type = ScaleCommandType::STOP_TIMER;
  uint32_t cycleId = 0;
  bool autoTare = false;
  bool canTareStartTimer = false;
};

struct ScaleEvent {
  ScaleEventType type = ScaleEventType::WEIGHT;
  uint32_t cycleId = 0;
  uint32_t receivedAtMs = 0;
  uint32_t connectionGeneration = 0;
  uint32_t packetSequence = 0;
  float weightG = 0.0f;
  bool commandAttempted = false;
  bool writeSucceeded = false;
};

struct MaintenanceLease {
  bool active = false;
  bool forwarded = false;
  bool applyRuntimeOnSuccess = false;
  uint32_t id = 0;
  uint32_t startedAtMs = 0;
  WebCommand command = {};
};

// ---------------------------------------------------------------------------
// BLE, application state and input state
// ---------------------------------------------------------------------------

AcaiaArduinoBLE scale(DEBUG);

StopperState stopperState = StopperState::REQUIRES_OFF;
ShotTrajectory shot;
CycleSession session;
PendingShotAnalysis pendingAnalysis;
RuntimeConfig runtimeConfig;
LastCycleSummary lastCycle;
DebugRingBuffer debugLog;

float currentWeight = 0.0f;
uint32_t currentWeightReceivedAtMs = 0;
uint32_t currentWeightSequence = 0;
uint32_t currentWeightConnectionGeneration = 0;
float observedWeight = 0.0f;
uint32_t observedWeightReceivedAtMs = 0;
uint32_t observedWeightSequence = 0;
uint32_t observedWeightConnectionGeneration = 0;
WeightStreamState weightStreamState = WeightStreamState::NO_SAMPLE;
uint32_t nextCycleId = 1;
TaskHandle_t scaleWorkerTaskHandle = nullptr;
QueueHandle_t scaleCommandQueue = nullptr;
QueueHandle_t scaleEventQueue = nullptr;
QueueHandle_t webCommandQueue = nullptr;
portMUX_TYPE scaleLinkMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleBeepMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleCriticalEventMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleWeightEventMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE webStatusMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE debugLogMux = portMUX_INITIALIZER_UNLOCKED;
ScaleLinkState scaleLinkState = ScaleLinkState::DISCONNECTED;
uint32_t scaleDisconnectSequence = 0;
uint32_t scaleConnectionGeneration = 0;
uint32_t scalePacketSequence = 0;
uint32_t scalePacketGaps = 0;
uint32_t scaleRejectedPackets = 0;
uint32_t scaleReconnects = 0;
uint8_t scaleLastDisconnectReason = 0;
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
bool scalePaddleReturnReminderBeepPending = false;
bool paddleReturnReminderActive = false;
uint32_t paddleReturnReminderLastAtMs = 0;

bool rawPaddleOn = false;
bool paddleOn = false;
bool paddleTurnedOn = false;
bool paddleTurnedOff = false;
uint32_t rawPaddleChangedAtMs = 0;
bool virtualPaddleOn = false;
ControlStatusSnapshot publishedControlStatus;
MaintenanceLease maintenanceLease;
WebCommand maintenanceCancellationCommand;
bool maintenanceCancellationPending = false;
WebCommand controlResultCommand;
bool controlResultPending = false;
#ifdef SHOT_STOPPER_HOST_TEST
bool hostForwardAcceptedNetworkCommandSucceeds = true;
uint32_t hostForwardAcceptedNetworkCommandCalls = 0;
WebCommand hostLastForwardedNetworkCommand;
#endif
bool runtimePersistPending = false;
RuntimeConfig runtimePersistCandidate;
uint32_t runtimePersistRequestId = 0;
uint32_t runtimePersistRetryAtMs = 0;
uint32_t nextInternalRequestId = 0x80000000UL;
uint32_t lastLoopAtMs = 0;
uint32_t loopMaxGapMs = 0;
uint32_t loopStackMinWords = 0;
uint32_t healthTelemetryAtMs = 0;
uint32_t freeHeapBytes = 0;
uint32_t minimumFreeHeapBytes = 0;
uint32_t largestFreeHeapBlockBytes = 0;
bool platformClockReady = false;
bool persistenceReady = false;
bool bleStackReady = false;
bool firmwareInitializationComplete = false;

bool beginMaintenanceLease(const WebCommand &networkCommand,
                           bool applyRuntimeOnSuccess);
void completeMaintenanceLease(const WebCommand &result);

#ifndef SHOT_STOPPER_HOST_TEST
PersistedSettings persistedSettings;
ShotStopperNetwork networkManager;
#endif

// The esp_timer callback independently opens CN9 at the hard limit even if the
// normal control loop is delayed or unavailable.
esp_timer_handle_t relaySafetyTimer = nullptr;
esp_timer_handle_t operationalLimitTimer = nullptr;
IndependentSafetyTimer independentSafetyTimer;
portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;
bool cn9Closed = false;
bool relaySafetyTripped = false;
bool operationalLimitTripped = false;
uint32_t cn9ClosedAtMs = 0;
uint32_t operationalLimitAtArmMs = HARD_MAX_CN9_CLOSED_MS;
RelaySafetyState relaySafetyState = RelaySafetyState::BOOT_SAFE;
RelaySafetyFault relaySafetyFault = RelaySafetyFault::NONE;
uint32_t relaySafetyGeneration = 0;
bool relaySafetyTimersReady = false;
bool taskWatchdogReady = false;
volatile bool criticalTaskWatchdogFault = false;
bool feedbackTransitionPending = false;
bool feedbackExpectedClosed = false;
uint32_t feedbackTransitionStartedAtMs = 0;
bool safetyHeartbeatLevel = false;
uint32_t safetyHeartbeatToggledAtMs = 0;
volatile bool safeRestartRequested = false;
SafetyResetSnapshot safetyResetStatus;
bool resetRecoverySawPaddleOn = false;
uint32_t resetRecoveryOffStartedAtMs = 0;

struct ScaleLinkSnapshot {
  ScaleLinkState state;
  uint32_t disconnectSequence;
  uint32_t connectionGeneration;
  uint32_t packetSequence;
  uint32_t packetGaps;
  uint32_t rejectedPackets;
  uint32_t reconnects;
  uint8_t lastDisconnectReason;
  uint32_t workerProgressAtMs;
};

struct StatusIndicatorFrame {
  IndicatorColor scale;
  IndicatorColor stopper;
};

QueueHandle_t statusIndicatorQueue = nullptr;
TaskHandle_t statusIndicatorTaskHandle = nullptr;
bool statusIndicatorsReady = false;
StatusIndicatorFrame lastPublishedIndicatorFrame;
bool indicatorFramePublished = false;

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

uint32_t elapsedMs(uint32_t sinceMs) {
  return static_cast<uint32_t>(millis() - sinceMs);
}

void addDebugEvent(DebugCategory category, DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0) {
  portENTER_CRITICAL(&debugLogMux);
  debugLog.add(millis(), category, code, argument1, argument2);
  portEXIT_CRITICAL(&debugLogMux);
}

size_t copyDebugEvents(uint32_t afterSequence, DebugEvent *output,
                       size_t capacity) {
  size_t copied;
  portENTER_CRITICAL(&debugLogMux);
  copied = debugLog.copyAfter(afterSequence, output, capacity);
  portEXIT_CRITICAL(&debugLogMux);
  return copied;
}

void copyControlStatus(ControlStatusSnapshot &output) {
  portENTER_CRITICAL(&webStatusMux);
  output = publishedControlStatus;
  portEXIT_CRITICAL(&webStatusMux);
}

void reportTaskWatchdogFault() {
  criticalTaskWatchdogFault = true;
}

void requestSafeRestart() {
  safeRestartRequested = true;
}

bool enqueueWebCommand(const WebCommand &command) {
  return webCommandQueue != nullptr &&
         xQueueSend(webCommandQueue, &command, 0) == pdTRUE;
}

ScaleLinkSnapshot getScaleLinkSnapshot() {
  ScaleLinkSnapshot snapshot;
  portENTER_CRITICAL(&scaleLinkMux);
  snapshot.state = scaleLinkState;
  snapshot.disconnectSequence = scaleDisconnectSequence;
  snapshot.connectionGeneration = scaleConnectionGeneration;
  snapshot.packetSequence = scalePacketSequence;
  snapshot.packetGaps = scalePacketGaps;
  snapshot.rejectedPackets = scaleRejectedPackets;
  snapshot.reconnects = scaleReconnects;
  snapshot.lastDisconnectReason = scaleLastDisconnectReason;
  snapshot.workerProgressAtMs = scaleWorkerProgressAtMs;
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
  }
  if (scaleLinkState != ScaleLinkState::CONNECTED &&
      state == ScaleLinkState::CONNECTED) {
    ++scaleConnectionGeneration;
    if (scaleConnectionGeneration == 0) {
      scaleConnectionGeneration = 1;
    }
  }
  scaleLinkState = state;
  scaleWorkerProgressAtMs = progressAtMs;
  portEXIT_CRITICAL(&scaleLinkMux);
  if (previous != state) {
    addDebugEvent(DebugCategory::SCALE,
                  state == ScaleLinkState::CONNECTED
                      ? DebugCode::SCALE_CONNECTED
                      : DebugCode::SCALE_DISCONNECTED);
  }
}

void markScaleWorkerProgress() {
  const uint32_t progressAtMs = millis();
  portENTER_CRITICAL(&scaleLinkMux);
  scaleWorkerProgressAtMs = progressAtMs;
  portEXIT_CRITICAL(&scaleLinkMux);
}

bool scaleLinkAvailable(const ScaleLinkSnapshot &snapshot) {
  return snapshot.state == ScaleLinkState::CONNECTED &&
         elapsedMs(snapshot.workerProgressAtMs) <= SCALE_WORKER_STALE_MS;
}

bool scaleAvailable() {
  return scaleLinkAvailable(getScaleLinkSnapshot());
}

bool currentWeightIsFresh(uint32_t now = millis()) {
  const uint32_t linkGeneration = getScaleLinkSnapshot().connectionGeneration;
  return currentWeightSequence > 0 && isfinite(currentWeight) &&
         currentWeight >= MIN_AUTOMATION_WEIGHT_G &&
         currentWeight <= MAX_AUTOMATION_WEIGHT_G &&
         (currentWeightConnectionGeneration == 0 ||
          currentWeightConnectionGeneration == linkGeneration) &&
         static_cast<int32_t>(now - currentWeightReceivedAtMs) >= 0 &&
         static_cast<uint32_t>(now - currentWeightReceivedAtMs) <=
             MAX_AUTOMATION_WEIGHT_AGE_MS;
}

bool observedWeightIsFresh(uint32_t now = millis()) {
  const uint32_t linkGeneration = getScaleLinkSnapshot().connectionGeneration;
  return observedWeightSequence > 0 && isfinite(observedWeight) &&
         fabsf(observedWeight) <= MAX_PARSED_WEIGHT_G &&
         observedWeightConnectionGeneration != 0 &&
         observedWeightConnectionGeneration == linkGeneration &&
         static_cast<int32_t>(now - observedWeightReceivedAtMs) >= 0 &&
         static_cast<uint32_t>(now - observedWeightReceivedAtMs) <=
             MAX_AUTOMATION_WEIGHT_AGE_MS;
}

void setWeightControlState(WeightControlState state) {
  if (session.weightControlState == state) {
    session.automaticEnabled = state == WeightControlState::ACTIVE;
    return;
  }
  const WeightControlState previous = session.weightControlState;
  session.weightControlState = state;
  session.automaticEnabled = state == WeightControlState::ACTIVE;
  if (state == WeightControlState::SUSPENDED) {
    session.scaleWasLost = true;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_CONTROL_SUSPENDED,
                  static_cast<int32_t>(previous));
  } else if (state == WeightControlState::ACTIVE &&
             (previous == WeightControlState::SUSPENDED ||
              previous == WeightControlState::VALIDATING)) {
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_CONTROL_RECOVERED);
  }
}

void resetWeightTrend() {
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
}

void suspendWeightControl() {
  if (session.weightControlState == WeightControlState::ACTIVE ||
      session.weightControlState == WeightControlState::VALIDATING) {
    setWeightControlState(WeightControlState::SUSPENDED);
    session.recoveryConfirmations = 0;
    session.calibrationEligible = false;
    resetWeightTrend();
  }
}

bool scaleAutomationUnavailableForSession() {
  const ScaleLinkSnapshot snapshot = getScaleLinkSnapshot();
  return !scaleLinkAvailable(snapshot) ||
         !currentWeightIsFresh() ||
         snapshot.disconnectSequence !=
             session.scaleDisconnectSequenceAtStart;
}

bool enqueueScaleCommand(const ScaleCommand &command) {
  if (scaleCommandQueue == nullptr) {
    return false;
  }

  if (xQueueSend(scaleCommandQueue, &command, 0) == pdTRUE) {
    return true;
  }
  Serial.println("Scale command queue full");
  return false;
}

float cycleElapsedSeconds() {
  return session.active ? elapsedMs(session.startedAtMs) / 1000.0f : 0.0f;
}

const char *stateName(StopperState state) {
  return stopperStateName(state);
}

const char *endReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::NONE: return "none";
    case EndReason::PADDLE: return "paddle";
    case EndReason::SCALE_PREDICTION: return "scale prediction";
    case EndReason::SCALE_THRESHOLD: return "scale threshold";
    case EndReason::WEIGHT_ANOMALY: return "weight anomaly";
    case EndReason::GLOBAL_LIMIT: return "global CN9 limit";
    case EndReason::CONFIGURED_WALL_LIMIT:
      return "configured wall limit";
    case EndReason::SHORT_SHOT: return "short shot";
    case EndReason::RINSE_COMPLETE: return "rinse complete";
    case EndReason::WEB_STOP: return "web stop";
    case EndReason::PHYSICAL_OVERRIDE: return "physical override";
    case EndReason::WEB_HEARTBEAT_TIMEOUT: return "web heartbeat timeout";
    case EndReason::RELAY_SAFETY_FAILURE: return "relay safety failure";
  }
  return "unknown";
}

void transitionTo(StopperState nextState) {
  if (stopperState == nextState) {
    return;
  }

  const StopperState previousState = stopperState;
  Serial.print("State ");
  Serial.print(stateName(stopperState));
  Serial.print(" -> ");
  Serial.println(stateName(nextState));
  stopperState = nextState;
  addDebugEvent(DebugCategory::STATE, DebugCode::STATE_TRANSITION,
                static_cast<int32_t>(previousState),
                static_cast<int32_t>(nextState));
}

IndicatorColor applyIndicatorBrightness(const IndicatorColor &color) {
  const auto scaleChannel = [](uint8_t channel) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(channel) * STATUS_INDICATOR_BRIGHTNESS +
         127U) /
        255U);
  };
  return {scaleChannel(color.red), scaleChannel(color.green),
          scaleChannel(color.blue)};
}

void writeWs2812b(uint8_t pin, const IndicatorColor &color) {
  const IndicatorColor limited = applyIndicatorBrightness(color);
  rgbLedWrite(pin, limited.red, limited.green, limited.blue);
}

void statusIndicatorTask(void *) {
  StatusIndicatorFrame applied;
  bool appliedOnce = false;
  for (;;) {
    StatusIndicatorFrame requested;
    if (xQueueReceive(statusIndicatorQueue, &requested, portMAX_DELAY) !=
        pdTRUE) {
      continue;
    }
    if (!appliedOnce || requested.scale != applied.scale) {
      writeWs2812b(SCALE_STATUS_LED_GPIO, requested.scale);
    }
    if (!appliedOnce || requested.stopper != applied.stopper) {
      writeWs2812b(STOPPER_STATUS_LED_GPIO, requested.stopper);
    }
    applied = requested;
    appliedOnce = true;
  }
}

bool initializeStatusIndicators() {
  statusIndicatorQueue = xQueueCreate(1, sizeof(StatusIndicatorFrame));
  if (statusIndicatorQueue == nullptr) {
    return false;
  }
  const StatusIndicatorFrame off = {INDICATOR_OFF, INDICATOR_OFF};
  if (xQueueOverwrite(statusIndicatorQueue, &off) != pdPASS) {
    vQueueDelete(statusIndicatorQueue);
    statusIndicatorQueue = nullptr;
    return false;
  }
  if (xTaskCreate(statusIndicatorTask, "status_indicator",
                  STATUS_INDICATOR_TASK_STACK_SIZE, nullptr,
                  tskIDLE_PRIORITY + 1, &statusIndicatorTaskHandle) !=
      pdPASS) {
    vQueueDelete(statusIndicatorQueue);
    statusIndicatorQueue = nullptr;
    statusIndicatorTaskHandle = nullptr;
    return false;
  }
  lastPublishedIndicatorFrame = off;
  indicatorFramePublished = true;
  return true;
}

// ---------------------------------------------------------------------------
// Relay and independent hard limit
// ---------------------------------------------------------------------------

bool readCn9FeedbackClosed() {
  return EXTERNAL_SAFETY_HARDWARE_PRESENT &&
         digitalRead(CN9_FEEDBACK_GPIO) == CN9_FEEDBACK_CLOSED_LEVEL;
}

void stopRelayDeadlineTimers() {
  independentSafetyTimer.stop();
  if (relaySafetyTimer != nullptr) {
    (void)esp_timer_stop(relaySafetyTimer);
  }
  if (operationalLimitTimer != nullptr) {
    (void)esp_timer_stop(operationalLimitTimer);
  }
}

void tripRelaySafetyLocked(RelaySafetyFault fault, bool hardLimit,
                           bool operationalLimit, bool lockout) {
  // The electrical action is deliberately first. State publication, logging,
  // timer cleanup and recovery all happen after the relay is de-energized.
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  recordRelayCommandedClosed(false);
  cn9Closed = false;
  ++relaySafetyGeneration;
  relaySafetyState = lockout ? RelaySafetyState::LOCKOUT
                             : RelaySafetyState::TRIPPED;
  relaySafetyFault = fault;
  relaySafetyTripped = relaySafetyTripped || hardLimit;
  operationalLimitTripped =
      operationalLimitTripped || operationalLimit;
  feedbackExpectedClosed = false;
  feedbackTransitionPending = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  feedbackTransitionStartedAtMs = millis();
}

void tripRelaySafety(RelaySafetyFault fault, bool hardLimit = false,
                     bool operationalLimit = false, bool lockout = true) {
  portENTER_CRITICAL(&relayMux);
  tripRelaySafetyLocked(fault, hardLimit, operationalLimit, lockout);
  portEXIT_CRITICAL(&relayMux);
  stopRelayDeadlineTimers();
}

void relaySafetyTimerCallback(void *) {
  const uint32_t callbackAtMs = millis();
  portENTER_CRITICAL(&relayMux);
  // Ignore a callback queued by a previous generation. ARMING is included so
  // a timeout that races the close transaction cancels that transaction.
  if ((relaySafetyState == RelaySafetyState::ARMING ||
       relaySafetyState == RelaySafetyState::CLOSED) &&
      static_cast<uint32_t>(callbackAtMs - cn9ClosedAtMs) >=
          HARD_MAX_CN9_CLOSED_MS) {
    tripRelaySafetyLocked(RelaySafetyFault::HARD_LIMIT, true, false, false);
  }
  portEXIT_CRITICAL(&relayMux);
}

void operationalLimitTimerCallback(void *) {
  const uint32_t callbackAtMs = millis();
  portENTER_CRITICAL(&relayMux);
  if ((relaySafetyState == RelaySafetyState::ARMING ||
       relaySafetyState == RelaySafetyState::CLOSED) &&
      operationalLimitAtArmMs < HARD_MAX_CN9_CLOSED_MS &&
      static_cast<uint32_t>(callbackAtMs - cn9ClosedAtMs) >=
          operationalLimitAtArmMs) {
    tripRelaySafetyLocked(RelaySafetyFault::OPERATIONAL_LIMIT, false, true,
                          false);
  }
  portEXIT_CRITICAL(&relayMux);
}

#ifndef SHOT_STOPPER_HOST_TEST
void IRAM_ATTR writeGpioFromIsr(uint8_t pin, uint8_t level) {
  const uint32_t mask = (pin < 32) ? (uint32_t{1} << pin)
                                   : (uint32_t{1} << (pin - 32));
  if (level == LOW) {
    if (pin < 32) {
      REG_WRITE(GPIO_OUT_W1TC_REG, mask);
    }
#ifdef GPIO_OUT1_W1TC_REG
    else {
      REG_WRITE(GPIO_OUT1_W1TC_REG, mask);
    }
#endif
  } else {
    if (pin < 32) {
      REG_WRITE(GPIO_OUT_W1TS_REG, mask);
    }
#ifdef GPIO_OUT1_W1TS_REG
    else {
      REG_WRITE(GPIO_OUT1_W1TS_REG, mask);
    }
#endif
  }
}

void IRAM_ATTR openRelayElectricalFromIsr() {
  // Arduino-ESP32 does not place digitalWrite()/gpio_set_level() in IRAM in
  // the standard board configurations. Write the GPIO register directly so
  // the GPTimer deadline can de-energize K1 while flash cache is disabled.
  // Supports both active-HIGH and active-LOW relay modules: the correct
  // register (W1TS or W1TC) is selected at compile time via RELAY_OPEN_LEVEL.
  writeGpioFromIsr(RELAY_GPIO, RELAY_OPEN_LEVEL);
}
#else
void openRelayElectricalFromIsr() {
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
}
#endif

void IRAM_ATTR independentSafetyTimerCallback(void *) {
  portENTER_CRITICAL_ISR(&relayMux);
  if (relaySafetyState == RelaySafetyState::ARMING ||
      relaySafetyState == RelaySafetyState::CLOSED) {
    const bool operational =
        operationalLimitAtArmMs < HARD_MAX_CN9_CLOSED_MS;
    // Keep this ISR minimal and IRAM-safe. The control task performs timer
    // cleanup, RTC OPEN publication and logging after observing the latched
    // trip flags. If reset wins that race, the retained CLOSE marker causes a
    // conservative boot lockout.
    openRelayElectricalFromIsr();
    cn9Closed = false;
    ++relaySafetyGeneration;
    relaySafetyState = RelaySafetyState::TRIPPED;
    relaySafetyFault = operational ? RelaySafetyFault::OPERATIONAL_LIMIT
                                   : RelaySafetyFault::HARD_LIMIT;
    relaySafetyTripped = !operational;
    operationalLimitTripped = operational;
    feedbackExpectedClosed = false;
    feedbackTransitionPending = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  }
  portEXIT_CRITICAL_ISR(&relayMux);
}

bool initializeRelaySafetyTimer() {
  esp_timer_create_args_t hardArgs = {};
  hardArgs.callback = &relaySafetyTimerCallback;
  hardArgs.arg = nullptr;
  hardArgs.dispatch_method = ESP_TIMER_TASK;
  hardArgs.name = "cn9_hard_limit";
  if (esp_timer_create(&hardArgs, &relaySafetyTimer) != ESP_OK) {
    return false;
  }

  esp_timer_create_args_t operationalArgs = {};
  operationalArgs.callback = &operationalLimitTimerCallback;
  operationalArgs.arg = nullptr;
  operationalArgs.dispatch_method = ESP_TIMER_TASK;
  operationalArgs.name = "cn9_oper_limit";
  if (esp_timer_create(&operationalArgs, &operationalLimitTimer) != ESP_OK) {
    return false;
  }
  return independentSafetyTimer.begin(&independentSafetyTimerCallback,
                                      nullptr);
}

RelaySafetySnapshot getRelaySafetySnapshot() {
  RelaySafetySnapshot snapshot;
  portENTER_CRITICAL(&relayMux);
  snapshot.state = relaySafetyState;
  snapshot.fault = relaySafetyFault;
  snapshot.closed = cn9Closed;
  snapshot.commandedClosed = relaySafetyState == RelaySafetyState::ARMING ||
                             relaySafetyState == RelaySafetyState::CLOSED;
  snapshot.feedbackAvailable = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  snapshot.externalSafetyPresent = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  snapshot.watchdogReady = taskWatchdogReady;
  snapshot.timersReady = relaySafetyTimersReady;
  snapshot.tripped = relaySafetyTripped;
  snapshot.operationalTripped = operationalLimitTripped;
  snapshot.generation = relaySafetyGeneration;
  snapshot.closedAtMs = cn9ClosedAtMs;
  snapshot.operationalLimitMs = operationalLimitAtArmMs;
  snapshot.resetReasonCode = safetyResetStatus.reasonCode;
  snapshot.unsafeResetCount = safetyResetStatus.unsafeResetCount;
  snapshot.resetRecoveryRequired = safetyResetStatus.recoveryRequired;
  snapshot.bootLoopDetected = safetyResetStatus.bootLoopDetected;
  portEXIT_CRITICAL(&relayMux);
  snapshot.feedbackClosed = readCn9FeedbackClosed();
  return snapshot;
}

bool setCn9Closed(bool closed,
                  uint32_t operationalLimitMs = HARD_MAX_CN9_CLOSED_MS) {
  if (closed) {
    const RelaySafetySnapshot before = getRelaySafetySnapshot();
    if (before.closed) {
      return true;
    }

    if (operationalLimitMs < 1 ||
        operationalLimitMs > HARD_MAX_CN9_CLOSED_MS) {
      tripRelaySafety(RelaySafetyFault::INVALID_LIMIT);
      Serial.println("Cannot close CN9: invalid safety limit");
      return false;
    }
    if (before.state == RelaySafetyState::LOCKOUT) {
      Serial.println("Cannot close CN9: safety lockout is active");
      return false;
    }
    if (!platformClockReady || !relaySafetyTimersReady || !taskWatchdogReady ||
        relaySafetyTimer == nullptr || operationalLimitTimer == nullptr ||
        !independentSafetyTimer.ready() || criticalTaskWatchdogFault) {
      tripRelaySafety(!taskWatchdogReady || criticalTaskWatchdogFault
                          ? RelaySafetyFault::WATCHDOG_UNAVAILABLE
                          : RelaySafetyFault::INITIALIZATION_FAILED);
      Serial.println("Cannot close CN9: safety supervisor unavailable");
      return false;
    }
    if (EXTERNAL_SAFETY_HARDWARE_PRESENT && readCn9FeedbackClosed()) {
      tripRelaySafety(RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
      Serial.println("Cannot close CN9: feedback is already closed");
      return false;
    }

    stopRelayDeadlineTimers();
    const uint32_t closingAtMs = millis();
    uint32_t generation;
    portENTER_CRITICAL(&relayMux);
    generation = ++relaySafetyGeneration;
    cn9ClosedAtMs = closingAtMs;
    operationalLimitAtArmMs = operationalLimitMs;
    relaySafetyTripped = false;
    operationalLimitTripped = false;
    relaySafetyFault = RelaySafetyFault::NONE;
    relaySafetyState = RelaySafetyState::ARMING;
    cn9Closed = false;
    portEXIT_CRITICAL(&relayMux);

    bool armed = independentSafetyTimer.arm(
        operationalLimitMs < HARD_MAX_CN9_CLOSED_MS
            ? operationalLimitMs
            : HARD_MAX_CN9_CLOSED_MS);
    if (esp_timer_start_once(
            relaySafetyTimer,
            static_cast<uint64_t>(HARD_MAX_CN9_CLOSED_MS) * 1000ULL) !=
        ESP_OK) {
      armed = false;
    }
    if (operationalLimitMs < HARD_MAX_CN9_CLOSED_MS &&
        esp_timer_start_once(
            operationalLimitTimer,
            static_cast<uint64_t>(operationalLimitMs) * 1000ULL) != ESP_OK) {
      armed = false;
    }

    if (!armed) {
      tripRelaySafety(RelaySafetyFault::TIMER_ARM_FAILED);
      Serial.println("Cannot close CN9: failed to arm safety deadline");
      return false;
    }

#ifdef SHOT_STOPPER_HOST_TEST
    if (hostCn9ArmBeforeCommitHook != nullptr) {
      hostCn9ArmBeforeCommitHook();
    }
#endif

    bool committed = false;
    portENTER_CRITICAL(&relayMux);
    // A timer callback may have run while the timers were being armed. It
    // increments the generation and changes ARMING to TRIPPED, so this stale
    // continuation can no longer energize the relay.
    if (relaySafetyGeneration == generation &&
        relaySafetyState == RelaySafetyState::ARMING &&
        static_cast<uint32_t>(millis() - closingAtMs) <
            operationalLimitMs) {
      // Conservatively mark CLOSE before energizing K1. A reset between these
      // two writes produces a safe false-positive lockout, never a missed one.
      recordRelayCommandedClosed(true);
      digitalWrite(RELAY_GPIO, RELAY_CLOSED_LEVEL);
      cn9Closed = true;
      relaySafetyState = RelaySafetyState::CLOSED;
      feedbackExpectedClosed = true;
      feedbackTransitionPending = EXTERNAL_SAFETY_HARDWARE_PRESENT;
      feedbackTransitionStartedAtMs = millis();
      committed = true;
    }
    portEXIT_CRITICAL(&relayMux);
    if (!committed) {
      stopRelayDeadlineTimers();
      Serial.println("Cannot close CN9: arm transaction was canceled");
      return false;
    }
    Serial.println("CN9 closed");
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_CLOSED,
                  static_cast<int32_t>(operationalLimitMs));
    return true;
  }

  portENTER_CRITICAL(&relayMux);
  const bool wasClosed = cn9Closed ||
                         relaySafetyState == RelaySafetyState::ARMING;
  // Open before stopping either timer. A preemption after this write is safe.
  const bool alreadyOpenedBySafety =
      !cn9Closed && (relaySafetyState == RelaySafetyState::TRIPPED ||
                     relaySafetyState == RelaySafetyState::LOCKOUT);
  if (!alreadyOpenedBySafety) {
    digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  }
  recordRelayCommandedClosed(false);
  cn9Closed = false;
  ++relaySafetyGeneration;
  if (relaySafetyState != RelaySafetyState::TRIPPED &&
      relaySafetyState != RelaySafetyState::LOCKOUT) {
    relaySafetyState = RelaySafetyState::OPEN;
  }
  feedbackExpectedClosed = false;
  feedbackTransitionPending = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  feedbackTransitionStartedAtMs = millis();
  portEXIT_CRITICAL(&relayMux);
  stopRelayDeadlineTimers();
  if (wasClosed) {
    Serial.println("CN9 open");
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_OPENED);
  }
  return true;
}

bool consumeRelaySafetyTrip() {
  bool tripped;
  portENTER_CRITICAL(&relayMux);
  tripped = relaySafetyTripped;
  relaySafetyTripped = false;
  portEXIT_CRITICAL(&relayMux);
  return tripped;
}

bool consumeOperationalLimitTrip() {
  bool tripped;
  portENTER_CRITICAL(&relayMux);
  tripped = operationalLimitTripped;
  operationalLimitTripped = false;
  portEXIT_CRITICAL(&relayMux);
  return tripped;
}

void serviceRelaySafety() {
  if (safetyResetStatus.recoveryRequired) {
    const bool physicalPaddleOn =
        digitalRead(PADDLE_GPIO) == PADDLE_ACTIVE_LEVEL;
    if (physicalPaddleOn) {
      resetRecoverySawPaddleOn = true;
      resetRecoveryOffStartedAtMs = 0;
    } else if (resetRecoverySawPaddleOn) {
      if (resetRecoveryOffStartedAtMs == 0) {
        resetRecoveryOffStartedAtMs = millis();
      } else if (elapsedMs(resetRecoveryOffStartedAtMs) >=
                     RESET_RECOVERY_OFF_DWELL_MS &&
                 taskWatchdogReady && relaySafetyTimersReady &&
                 !criticalTaskWatchdogFault &&
                 (!EXTERNAL_SAFETY_HARDWARE_PRESENT ||
                  !readCn9FeedbackClosed())) {
        portENTER_CRITICAL(&relayMux);
        if (relaySafetyState == RelaySafetyState::LOCKOUT &&
            (relaySafetyFault == RelaySafetyFault::RESET_DURING_CLOSE ||
             relaySafetyFault == RelaySafetyFault::UNSAFE_RESET ||
             relaySafetyFault == RelaySafetyFault::BOOT_LOOP)) {
          relaySafetyState = RelaySafetyState::OPEN;
          relaySafetyFault = RelaySafetyFault::NONE;
          completeLocalResetRecovery(safetyResetStatus);
        }
        portEXIT_CRITICAL(&relayMux);
      }
    }
  }

  if (criticalTaskWatchdogFault) {
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
    safeRestartRequested = true;
    return;
  }

  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if ((relay.state == RelaySafetyState::ARMING || relay.closed) &&
      elapsedMs(relay.closedAtMs) >= relay.operationalLimitMs) {
    const bool operational =
        relay.operationalLimitMs < HARD_MAX_CN9_CLOSED_MS;
    tripRelaySafety(operational ? RelaySafetyFault::OPERATIONAL_LIMIT
                               : RelaySafetyFault::HARD_LIMIT,
                    !operational, operational, false);
    return;
  }

  if (!EXTERNAL_SAFETY_HARDWARE_PRESENT) {
    return;
  }

  const bool feedbackClosed = readCn9FeedbackClosed();
  if (feedbackTransitionPending) {
    if (elapsedMs(feedbackTransitionStartedAtMs) <
        CN9_FEEDBACK_SETTLE_MS) {
      return;
    }
    feedbackTransitionPending = false;
    if (feedbackClosed != feedbackExpectedClosed) {
      tripRelaySafety(feedbackExpectedClosed
                          ? RelaySafetyFault::FEEDBACK_FAILED_TO_CLOSE
                          : RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
    }
    return;
  }

  if (relay.closed != feedbackClosed) {
    tripRelaySafety(relay.closed
                        ? RelaySafetyFault::FEEDBACK_CHANGED_UNEXPECTEDLY
                        : RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
  }
}

void serviceSafetyHeartbeat(bool healthyLoopCompleted) {
  if (!EXTERNAL_SAFETY_HARDWARE_PRESENT) {
    return;
  }
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const bool healthy = healthyLoopCompleted && relay.watchdogReady &&
                       relay.timersReady && !criticalTaskWatchdogFault &&
                       relay.state != RelaySafetyState::LOCKOUT &&
                       relay.state != RelaySafetyState::TRIPPED;
  if (!healthy) {
    safetyHeartbeatLevel = false;
    digitalWrite(SAFETY_HEARTBEAT_GPIO, LOW);
    return;
  }
  if (elapsedMs(safetyHeartbeatToggledAtMs) >=
      SAFETY_HEARTBEAT_TOGGLE_MS) {
    safetyHeartbeatLevel = !safetyHeartbeatLevel;
    digitalWrite(SAFETY_HEARTBEAT_GPIO,
                 safetyHeartbeatLevel ? HIGH : LOW);
    safetyHeartbeatToggledAtMs = millis();
  }
}

// ---------------------------------------------------------------------------
// Paddle input and debounce
// ---------------------------------------------------------------------------

bool readRawPaddleOn() {
  return digitalRead(PADDLE_GPIO) == PADDLE_ACTIVE_LEVEL;
}

void initializePaddleInput() {
  pinMode(PADDLE_GPIO, INPUT_PULLUP);
  rawPaddleOn = readRawPaddleOn();
  paddleOn = rawPaddleOn;
  rawPaddleChangedAtMs = millis();
}

void updatePaddleInput() {
  paddleTurnedOn = false;
  paddleTurnedOff = false;

  const bool sampledOn = readRawPaddleOn();
  if (sampledOn != rawPaddleOn) {
    rawPaddleOn = sampledOn;
    rawPaddleChangedAtMs = millis();
  }

  if (paddleOn != rawPaddleOn &&
      elapsedMs(rawPaddleChangedAtMs) >= PADDLE_DEBOUNCE_MS) {
    const bool previous = paddleOn;
    paddleOn = rawPaddleOn;
    paddleTurnedOn = !previous && paddleOn;
    paddleTurnedOff = previous && !paddleOn;

    Serial.print("Paddle ");
    Serial.println(paddleOn ? "ON" : "OFF");
    addDebugEvent(DebugCategory::PADDLE,
                  paddleOn ? DebugCode::PADDLE_ON : DebugCode::PADDLE_OFF);
  }
}

bool paddleIsStablyOff() {
  return !paddleOn && !rawPaddleOn &&
         elapsedMs(rawPaddleChangedAtMs) >= PADDLE_DEBOUNCE_MS;
}

// ---------------------------------------------------------------------------
// Scale timer session
// ---------------------------------------------------------------------------

bool requestRemoteTimerStart() {
  ScaleCommand command;
  command.type = ScaleCommandType::START_TIMER_AND_TARE;
  command.cycleId = session.id;
  command.autoTare = session.config.autoTare;
  command.canTareStartTimer = session.config.canTareStartTimer;
  session.timerStartCommandQueued = enqueueScaleCommand(command);
  session.remoteTimerMayBeRunning = session.timerStartCommandQueued;
  return session.timerStartCommandQueued;
}

bool tryQueueRemoteTimerStop() {
  if (!session.stopTimerRequested || session.stopTimerCommandQueued ||
      session.timerStopResult == TimerStopResult::WRITE_SUCCEEDED ||
      session.stopTimerAttempts >= SCALE_STOP_MAX_ATTEMPTS ||
      static_cast<int32_t>(millis() - session.stopTimerRetryDeadlineMs) >= 0 ||
      !scaleAvailable()) {
    return false;
  }
  ScaleCommand command;
  command.type = ScaleCommandType::STOP_TIMER;
  command.cycleId = session.id;
  ++session.stopTimerAttempts;
  session.stopTimerLastAttemptMs = millis();
  if (!enqueueScaleCommand(command)) {
    session.timerStopResult = TimerStopResult::NOT_ATTEMPTED;
    return false;
  } else {
    session.timerStopResult = TimerStopResult::PENDING;
    session.stopTimerCommandQueued = true;
    Serial.print("Remote timer stop queued for cycle ");
    Serial.println(session.id);
    return true;
  }
}

void requestRemoteTimerStop() {
  if (!session.timerStartCommandQueued || session.stopTimerRequested) {
    return;
  }

  session.stopTimerRequested = true;
  session.stopTimerRetryDeadlineMs = millis() + SCALE_STOP_RETRY_WINDOW_MS;
  if (!tryQueueRemoteTimerStop()) {
    session.timerStopResult = TimerStopResult::NOT_ATTEMPTED;
    Serial.print("Remote timer stop deferred for cycle ");
    Serial.println(session.id);
  }
}

void serviceRemoteTimerStopRetry() {
  if (!session.stopTimerRequested || session.stopTimerCommandQueued ||
      session.timerStopResult == TimerStopResult::WRITE_SUCCEEDED ||
      session.stopTimerAttempts >= SCALE_STOP_MAX_ATTEMPTS ||
      static_cast<int32_t>(millis() - session.stopTimerRetryDeadlineMs) >= 0) {
    return;
  }
  if (session.stopTimerAttempts > 0 &&
      elapsedMs(session.stopTimerLastAttemptMs) <
          SCALE_STOP_RETRY_INTERVAL_MS) {
    return;
  }
  (void)tryQueueRemoteTimerStop();
}

// ---------------------------------------------------------------------------
// Shot prediction and offset analysis
// ---------------------------------------------------------------------------

void resetShotTrajectory(uint32_t startedAtMs) {
  shot.startMs = startedAtMs;
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
  shot.confirmedBrew = false;
}

void calculateExpectedEndTime() {
  if (shot.datapoints < TREND_POINT_COUNT ||
      shot.weight[shot.datapoints - 1] < 10.0f) {
    shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
    return;
  }

  float sumXY = 0.0f;
  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumSquaredX = 0.0f;
  const size_t first = shot.datapoints - TREND_POINT_COUNT;

  for (size_t i = first; i < shot.datapoints; ++i) {
    sumXY += shot.timeS[i] * shot.weight[i];
    sumX += shot.timeS[i];
    sumY += shot.weight[i];
    sumSquaredX += shot.timeS[i] * shot.timeS[i];
  }

  const float n = static_cast<float>(TREND_POINT_COUNT);
  const float denominator = n * sumSquaredX - sumX * sumX;
  if (fabsf(denominator) < 0.000001f) {
    shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
    return;
  }

  const float slope = (n * sumXY - sumX * sumY) / denominator;
  if (slope <= 0.0f || !isfinite(slope)) {
    shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
    return;
  }

  const float meanX = sumX / n;
  const float meanY = sumY / n;
  const float intercept = meanY - slope * meanX;
  const float predicted =
      (session.config.goalWeightG - session.config.weightOffsetG - intercept) /
      slope;

  if (!isfinite(predicted)) {
    shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  } else {
    shot.expectedEndS = predicted;
  }
}

bool shouldTrackWeight() {
  return session.active && !session.config.timerOnly &&
         session.weightControlState != WeightControlState::INACTIVE &&
         session.weightControlState != WeightControlState::FAULT_STOPPED &&
         (stopperState == StopperState::QUALIFYING_ON ||
          stopperState == StopperState::BREW);
}

float effectiveStopThreshold() {
  return static_cast<float>(session.config.goalWeightG) -
         session.config.weightOffsetG;
}

void resetDirectStopConfirmation() {
  session.thresholdConfirmations = 0;
  session.lastThresholdAtMs = 0;
  session.lastThresholdPacketSequence = 0;
  session.lastThresholdConnectionGeneration = 0;
}

void rejectScaleSample(DebugCode code, float weightG, float referenceG = 0.0f) {
  addDebugEvent(DebugCategory::SCALE, code, weightToCentigrams(weightG),
                weightToCentigrams(referenceG));
}

void armPostTareBaselineWindow() {
  session.awaitingPostTareBaseline = true;
  session.postTareBaselineDeadlineMs =
      session.startedAtMs + POST_TARE_BASELINE_GRACE_MS;
  session.hasWeightAnchor = false;
  session.recoveryConfirmations = 0;
  session.recoveryLastAtMs = 0;
  session.recoveryLastPacketSequence = 0;
}

void expirePostTareBaselineIfNeeded() {
  if (!session.awaitingPostTareBaseline) {
    return;
  }
  if (static_cast<int32_t>(millis() - session.postTareBaselineDeadlineMs) <
      0) {
    return;
  }
  session.awaitingPostTareBaseline = false;
  addDebugEvent(DebugCategory::SCALE,
                DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT,
                static_cast<int32_t>(session.id),
                static_cast<int32_t>(POST_TARE_BASELINE_GRACE_MS));
}

void considerDirectStopSample(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence,
                              uint32_t connectionGeneration) {
  if (!shouldTrackWeight() || packetSequence == 0 || !isfinite(weight)) {
    return;
  }

  const bool overload = fabsf(weight) > MAX_AUTOMATION_WEIGHT_G;
  const bool overThreshold = weight >= effectiveStopThreshold();
  if (!overThreshold && !overload) {
    resetDirectStopConfirmation();
    return;
  }

  const bool consecutive = session.thresholdConfirmations > 0 &&
      connectionGeneration == session.lastThresholdConnectionGeneration &&
      packetSequence == session.lastThresholdPacketSequence + 1U &&
      static_cast<int32_t>(receivedAtMs - session.lastThresholdAtMs) >= 0 &&
      static_cast<uint32_t>(receivedAtMs - session.lastThresholdAtMs) <=
          DIRECT_STOP_CONFIRMATION_WINDOW_MS;
  session.thresholdConfirmations = consecutive
      ? static_cast<uint8_t>(session.thresholdConfirmations + 1U)
      : 1U;
  session.lastThresholdAtMs = receivedAtMs;
  session.lastThresholdPacketSequence = packetSequence;
  session.lastThresholdConnectionGeneration = connectionGeneration;

  if (session.thresholdConfirmations < DIRECT_STOP_CONFIRMATION_SAMPLES) {
    return;
  }

  session.directStopPending = true;
  session.directStopReason = overload ? EndReason::WEIGHT_ANOMALY
                                      : EndReason::SCALE_THRESHOLD;
  if (overload) {
    weightStreamState = WeightStreamState::OVERLOAD;
    session.calibrationEligible = false;
    setWeightControlState(WeightControlState::FAULT_STOPPED);
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_OVERLOAD_CONFIRMED,
                  static_cast<int32_t>(weight));
  } else {
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_THRESHOLD_CONFIRMED,
                  static_cast<int32_t>(weight * 100.0f));
  }
}

bool acceptWeightIntoTrajectory(float weight, uint32_t receivedAtMs,
                                uint32_t packetSequence) {
  if (shot.datapoints >= MAX_SHOT_DATAPOINTS) {
    Serial.println("Shot trajectory full; ignoring additional samples");
    return true;
  }
  const size_t index = shot.datapoints++;
  shot.timeS[index] =
      static_cast<uint32_t>(receivedAtMs - shot.startMs) / 1000.0f;
  shot.weight[index] = weight;
  session.receivedFreshWeightInCycle = true;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightAtMs = receivedAtMs;
  session.lastAcceptedWeightG = weight;
  session.lastAcceptedPacketSequence = packetSequence;
  calculateExpectedEndTime();

  if (DEBUG) {
    Serial.print(weight);
    Serial.print("g, t=");
    Serial.print(shot.timeS[index]);
    Serial.print("s, expected end=");
    Serial.print(shot.expectedEndS);
    Serial.println("s");
  }
  return true;
}

bool recordWeightSampleWithProvenance(float weight, uint32_t receivedAtMs,
                                      uint32_t packetSequence,
                                      uint32_t connectionGeneration) {
  if (!isfinite(weight) || fabsf(weight) > MAX_PARSED_WEIGHT_G) {
    Serial.println("Invalid or out-of-range weight ignored");
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_INVALID, weight);
    return false;
  }

  if (!shouldTrackWeight()) {
    return weight >= MIN_AUTOMATION_WEIGHT_G &&
           weight <= MAX_AUTOMATION_WEIGHT_G;
  }

  if (connectionGeneration == 0) {
    connectionGeneration = session.ownedConnectionGeneration;
  }
  if (packetSequence == 0) {
    packetSequence = session.lastAcceptedPacketSequence + 1U;
    if (packetSequence == 0) {
      packetSequence = 1;
    }
  }

  if ((session.weightControlState == WeightControlState::ACTIVE ||
       session.weightControlState == WeightControlState::VALIDATING) &&
      session.ownedConnectionGeneration != 0 &&
      connectionGeneration != session.ownedConnectionGeneration) {
    suspendWeightControl();
  }

  considerDirectStopSample(weight, receivedAtMs, packetSequence,
                           connectionGeneration);

  if (weight < MIN_AUTOMATION_WEIGHT_G ||
      weight > MAX_AUTOMATION_WEIGHT_G) {
    weightStreamState = WeightStreamState::OVERLOAD;
    session.calibrationEligible = false;
    if (session.weightControlState == WeightControlState::ACTIVE) {
      setWeightControlState(WeightControlState::VALIDATING);
    }
    session.recoveryConfirmations = 0;
    const float violatedLimit = weight < MIN_AUTOMATION_WEIGHT_G
                                    ? MIN_AUTOMATION_WEIGHT_G
                                    : MAX_AUTOMATION_WEIGHT_G;
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_RANGE, weight,
                      violatedLimit);
    return false;
  }

  if (static_cast<int32_t>(receivedAtMs - shot.startMs) < 0) {
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_PRE_CYCLE, weight);
    return false;
  }

  if (session.awaitingPostTareBaseline) {
    if (fabsf(weight) <= POST_TARE_BASELINE_MAX_ABS_G) {
      session.awaitingPostTareBaseline = false;
      weightStreamState = WeightStreamState::FRESH;
      return acceptWeightIntoTrajectory(weight, receivedAtMs, packetSequence);
    }
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_SLEW, weight,
                      POST_TARE_BASELINE_MAX_ABS_G);
    return false;
  }

  bool plausible = true;
  if (session.hasWeightAnchor) {
    if (static_cast<int32_t>(receivedAtMs -
                             session.lastAcceptedWeightAtMs) < 0) {
      plausible = false;
    } else {
      uint32_t deltaMs = receivedAtMs - session.lastAcceptedWeightAtMs;
      if (deltaMs == 0) {
        deltaMs = 1;
      }
      const float allowedDelta = AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G +
          MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S * deltaMs / 1000.0f;
      plausible = fabsf(weight - session.lastAcceptedWeightG) <= allowedDelta;
    }
  }

  if (session.weightControlState == WeightControlState::ACTIVE && !plausible) {
    Serial.println("Implausible scale slew ignored; validating stream");
    weightStreamState = WeightStreamState::ANOMALOUS;
    session.calibrationEligible = false;
    setWeightControlState(WeightControlState::VALIDATING);
    session.recoveryConfirmations = 0;
    session.recoveryLastAtMs = 0;
    session.recoveryLastPacketSequence = 0;
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_SLEW, weight,
                      session.lastAcceptedWeightG);
    return false;
  }

  if (session.weightControlState == WeightControlState::VALIDATING ||
      session.weightControlState == WeightControlState::SUSPENDED) {
    bool recoveryPlausible = plausible;
    if (session.recoveryConfirmations > 0) {
      const bool consecutive =
          packetSequence == session.recoveryLastPacketSequence + 1U;
      const int32_t signedDelta =
          static_cast<int32_t>(receivedAtMs - session.recoveryLastAtMs);
      if (!consecutive || signedDelta <= 0 ||
          static_cast<uint32_t>(signedDelta) > MAX_AUTOMATION_WEIGHT_AGE_MS) {
        recoveryPlausible = false;
      } else {
        const float allowedIncrease = AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G +
            MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S *
                static_cast<uint32_t>(signedDelta) / 1000.0f;
        const float change = weight - session.recoveryLastWeightG;
        recoveryPlausible = change >= -MAX_RECOVERY_WEIGHT_DROP_G &&
                            change <= allowedIncrease;
      }
    } else if (session.hasWeightAnchor) {
      recoveryPlausible = recoveryPlausible &&
          weight >= session.lastAcceptedWeightG - MAX_RECOVERY_WEIGHT_DROP_G;
    }

    if (!recoveryPlausible) {
      const float referenceG = session.recoveryConfirmations > 0
                                   ? session.recoveryLastWeightG
                                   : session.lastAcceptedWeightG;
      session.recoveryConfirmations = 0;
      session.recoveryLastAtMs = 0;
      session.recoveryLastPacketSequence = 0;
      rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_RECOVERY, weight,
                        referenceG);
      return false;
    }

    ++session.recoveryConfirmations;
    session.recoveryLastAtMs = receivedAtMs;
    session.recoveryLastPacketSequence = packetSequence;
    session.recoveryLastWeightG = weight;
    if (session.recoveryConfirmations <
        WEIGHT_RECOVERY_CONFIRMATION_SAMPLES) {
      return false;
    }

    session.ownedConnectionGeneration = connectionGeneration;
    session.scaleDisconnectSequenceAtStart =
        getScaleLinkSnapshot().disconnectSequence;
    session.recoveryConfirmations = 0;
    resetWeightTrend();
    setWeightControlState(WeightControlState::ACTIVE);
  }

  weightStreamState = WeightStreamState::FRESH;
  return acceptWeightIntoTrajectory(weight, receivedAtMs, packetSequence);
}

bool recordWeightSample(float weight, uint32_t receivedAtMs) {
  uint32_t packetSequence = 0;
  portENTER_CRITICAL(&scaleLinkMux);
  ++scalePacketSequence;
  if (scalePacketSequence == 0) {
    scalePacketSequence = 1;
  }
  packetSequence = scalePacketSequence;
  portEXIT_CRITICAL(&scaleLinkMux);
  return recordWeightSampleWithProvenance(
      weight, receivedAtMs, packetSequence,
      session.ownedConnectionGeneration);
}

void scheduleShotAnalysis() {
  pendingAnalysis.pending = true;
  pendingAnalysis.endedAtMs = millis();
  pendingAnalysis.endedWeightSequence = currentWeightSequence;
  pendingAnalysis.goalWeightG = session.config.goalWeightG;
  pendingAnalysis.weightOffsetG = session.config.weightOffsetG;
}

void shotAnalysisTask() {
  if (!pendingAnalysis.pending ||
      elapsedMs(pendingAnalysis.endedAtMs) < DRIP_DELAY_MS) {
    return;
  }

  pendingAnalysis.pending = false;
  if (!scaleAvailable() || !isfinite(currentWeight) ||
      !isfinite(pendingAnalysis.weightOffsetG) ||
      currentWeightSequence == pendingAnalysis.endedWeightSequence ||
      static_cast<int32_t>(currentWeightReceivedAtMs -
                           pendingAnalysis.endedAtMs) <= 0 ||
      currentWeight <
          (pendingAnalysis.goalWeightG - pendingAnalysis.weightOffsetG)) {
    Serial.println("Final weight unavailable or too low; offset unchanged");
    return;
  }

  Serial.print("Final weight: ");
  Serial.print(currentWeight);
  Serial.print("g; cycle goal: ");
  Serial.print(pendingAnalysis.goalWeightG);
  Serial.print("g; cycle offset: ");
  Serial.print(pendingAnalysis.weightOffsetG);
  Serial.println("g");

  const float observedError =
      currentWeight - pendingAnalysis.goalWeightG +
      pendingAnalysis.weightOffsetG;
  if (fabsf(observedError) > MAX_OFFSET_G) {
    Serial.println("Shot error too large; offset unchanged");
    return;
  }

  const float updatedOffset =
      pendingAnalysis.weightOffsetG + currentWeight -
      pendingAnalysis.goalWeightG;
  if (!isfinite(updatedOffset) || updatedOffset < 0.0f ||
      updatedOffset > MAX_OFFSET_G) {
    Serial.println("Calculated offset outside safe range; offset unchanged");
    return;
  }

  RuntimeConfig candidate = runtimeConfig;
  candidate.weightOffsetG = updatedOffset;
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  runtimePersistCandidate = candidate;
  runtimePersistPending = true;
  runtimePersistRetryAtMs = millis();
  Serial.print("New offset pending durable commit: ");
  Serial.println(candidate.weightOffsetG);
}

// ---------------------------------------------------------------------------
// Scale connection and weight packets
// ---------------------------------------------------------------------------

bool publishScaleEvent(const ScaleEvent &event, bool critical) {
  if (event.type == ScaleEventType::WEIGHT) {
    ScaleEvent stamped = event;
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
    portEXIT_CRITICAL(&scaleLinkMux);

    bool overwrotePendingWeight = false;
    portENTER_CRITICAL(&scaleWeightEventMux);
    overwrotePendingWeight = scaleWeightEventPending;
    scaleWeightEvent = stamped;
    scaleWeightEventPending = true;
    portEXIT_CRITICAL(&scaleWeightEventMux);
    if (overwrotePendingWeight) {
      portENTER_CRITICAL(&scaleLinkMux);
      ++scalePacketGaps;
      portEXIT_CRITICAL(&scaleLinkMux);
      addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_PACKET_GAP,
                    static_cast<int32_t>(stamped.packetSequence));
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
  portENTER_CRITICAL(&scaleLinkMux);
  scaleRejectedPackets = scale.rejectedPacketCount();
  scaleReconnects = scale.reconnectCount();
  scaleLastDisconnectReason =
      static_cast<uint8_t>(scale.lastDisconnectReason());
  portEXIT_CRITICAL(&scaleLinkMux);
  setScaleLinkState(scale.isConnected() ? ScaleLinkState::CONNECTED
                                        : ScaleLinkState::DISCONNECTED);
}

void executeScaleStartCommand(const ScaleCommand &command) {
  ScaleEvent event;
  event.type = ScaleEventType::TIMER_START_RESULT;
  event.cycleId = command.cycleId;

  if (scale.isConnected()) {
    if (command.canTareStartTimer && command.autoTare &&
        scale.supportsTareStartTimer()) {
      event.commandAttempted = true;
      event.writeSucceeded = scale.tareStartTimer();
    } else {
      const bool resetSucceeded = scale.resetTimer();
      if (resetSucceeded) {
        event.commandAttempted = true;
        event.writeSucceeded = scale.startTimer();
      }
      if (event.writeSucceeded && command.autoTare) {
        scale.tare();
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

  if (scale.isConnected()) {
    // A failed start write may only mean its ATT response was lost. Attempting
    // STOP on the existing connection is harmless and covers that case.
    event.commandAttempted = true;
    event.writeSucceeded = scale.stopTimer();
  }

  updateWorkerLinkState();
  publishScaleEvent(event, true);
}

void executeScaleBeepCommand(DebugCode successCode, DebugCode failureCode,
                             DebugCode unsupportedCode) {
  if (!scale.isConnected()) {
    addDebugEvent(DebugCategory::SCALE, failureCode);
    return;
  }
  if (!scale.supportsIndependentBeep()) {
    addDebugEvent(DebugCategory::SCALE, unsupportedCode);
    return;
  }
  const bool succeeded = scale.beepWithoutStateChange();
  addDebugEvent(DebugCategory::SCALE, succeeded ? successCode : failureCode);
  updateWorkerLinkState();
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

void servicePaddleReturnReminder() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  // Read the GPIO here rather than a debounced state: this reminder describes
  // the physical paddle circuit as it is wired at this instant.
  const bool shouldRemind = runtimeConfig.paddleReturnReminderBeep &&
                            readRawPaddleOn() && !relay.closed &&
                            scaleAvailable();
  if (!shouldRemind) {
    paddleReturnReminderActive = false;
    paddleReturnReminderLastAtMs = 0;
    cancelScalePaddleReturnReminderBeep();
    return;
  }

  const uint32_t now = millis();
  if (!paddleReturnReminderActive) {
    paddleReturnReminderActive = true;
    paddleReturnReminderLastAtMs = now;
    return;
  }
  if (elapsedMs(paddleReturnReminderLastAtMs) >=
      PADDLE_RETURN_REMINDER_BEEP_INTERVAL_MS) {
    paddleReturnReminderLastAtMs = now;
    requestScalePaddleReturnReminderBeep();
  }
}

void executeScaleCommand(const ScaleCommand &command) {
  switch (command.type) {
    case ScaleCommandType::START_TIMER_AND_TARE:
      executeScaleStartCommand(command);
      break;
    case ScaleCommandType::STOP_TIMER:
      executeScaleStopCommand(command);
      break;
  }
}

void serviceScaleWorkerLink() {
  if (!scale.isConnected()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return;
  }

  if (scale.heartbeatRequired()) {
    if (!scale.heartbeat()) {
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::DISCONNECTED);
      return;
    }
  }

  const bool weightAvailable = scale.newWeightAvailable();
  if (!scale.isConnected()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return;
  }

  if (weightAvailable) {
    ScaleEvent event;
    event.type = ScaleEventType::WEIGHT;
    event.receivedAtMs = millis();
    event.weightG = scale.getWeight();
    publishScaleEvent(event, false);
  }
  updateWorkerLinkState();
}

void scaleWorkerTask(void *) {
  uint32_t lastConnectAttemptMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;
  uint32_t telemetryAtMs = 0;

  if (!subscribeCurrentTaskToWatchdog()) {
    reportTaskWatchdogFault();
  }

  for (;;) {
    // The control task treats a connected link with no worker progress as
    // unavailable, preventing stale prediction data from ending a shot.
    markScaleWorkerProgress();
    BLE.poll();

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
      } else if (scale.isConnected()) {
        connectAttemptSeriesActive = false;
        serviceScaleWorkerLink();
      } else if (elapsedMs(lastConnectAttemptMs) >= SCALE_CONNECT_RETRY_MS) {
        lastConnectAttemptMs = millis();
        const bool logAttempt =
            !connectAttemptSeriesActive ||
            elapsedMs(lastConnectLogMs) >= SCALE_CONNECT_LOG_MS;
        if (logAttempt) {
          lastConnectLogMs = lastConnectAttemptMs;
          connectAttemptSeriesActive = true;
          Serial.println("Attempting scale connection...");
          addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECTING);
        }
        const bool connected = scale.init();
        if (connected || logAttempt) {
          Serial.println(connected ? "Scale connected"
                                   : "Scale connection failed");
        }
        updateWorkerLinkState();
        setScaleLinkState(connected ? ScaleLinkState::CONNECTED
                                    : ScaleLinkState::DISCONNECTED);
      }
    }

    if (!feedCurrentTaskWatchdog()) {
      reportTaskWatchdogFault();
    }
    if (elapsedMs(telemetryAtMs) >= HEALTH_TELEMETRY_INTERVAL_MS) {
      telemetryAtMs = millis();
      scaleWorkerStackMinWords =
          static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool initializeScaleWorker() {
  scaleCommandQueue =
      xQueueCreate(SCALE_COMMAND_QUEUE_LENGTH, sizeof(ScaleCommand));
  scaleEventQueue = xQueueCreate(SCALE_EVENT_QUEUE_LENGTH,
                                 sizeof(ScaleEvent));
  if (scaleCommandQueue == nullptr || scaleEventQueue == nullptr) {
    if (scaleCommandQueue != nullptr) {
      vQueueDelete(scaleCommandQueue);
      scaleCommandQueue = nullptr;
    }
    if (scaleEventQueue != nullptr) {
      vQueueDelete(scaleEventQueue);
      scaleEventQueue = nullptr;
    }
    return false;
  }

  if (xTaskCreate(scaleWorkerTask, "scale_worker", 8192, nullptr,
                  tskIDLE_PRIORITY + 1, &scaleWorkerTaskHandle) != pdPASS) {
    vQueueDelete(scaleCommandQueue);
    vQueueDelete(scaleEventQueue);
    scaleCommandQueue = nullptr;
    scaleEventQueue = nullptr;
    scaleWorkerTaskHandle = nullptr;
    return false;
  }
  return true;
}

void processScaleWorkerEvents() {
  if (scaleEventQueue == nullptr && !scaleCriticalEventPending &&
      !scaleTimerStartEventPending && !scaleWeightEventPending) {
    return;
  }

  ScaleEvent event;
  size_t processed = 0;
  while (processed < SCALE_EVENT_QUEUE_LENGTH + 1) {
    bool receivedCritical = false;
    bool receivedWeight = false;
    portENTER_CRITICAL(&scaleCriticalEventMux);
    if (scaleCriticalEventPending) {
      event = scaleCriticalEvent;
      scaleCriticalEventPending = false;
      receivedCritical = true;
    }
    portEXIT_CRITICAL(&scaleCriticalEventMux);
    if (!receivedCritical) {
      portENTER_CRITICAL(&scaleCriticalEventMux);
      if (scaleTimerStartEventPending) {
        event = scaleTimerStartEvent;
        scaleTimerStartEventPending = false;
        receivedCritical = true;
      }
      portEXIT_CRITICAL(&scaleCriticalEventMux);
    }
    if (!receivedCritical) {
      portENTER_CRITICAL(&scaleWeightEventMux);
      if (scaleWeightEventPending) {
        event = scaleWeightEvent;
        scaleWeightEventPending = false;
        receivedWeight = true;
      }
      portEXIT_CRITICAL(&scaleWeightEventMux);
    }
    if (!receivedCritical && !receivedWeight &&
        (scaleEventQueue == nullptr ||
         xQueueReceive(scaleEventQueue, &event, 0) != pdTRUE)) {
      break;
    }
    ++processed;
    switch (event.type) {
      case ScaleEventType::WEIGHT:
        if (!isfinite(event.weightG) ||
            fabsf(event.weightG) > MAX_PARSED_WEIGHT_G) {
          Serial.println("Invalid scale weight event ignored");
          rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_INVALID,
                            event.weightG);
          break;
        }
        {
          const ScaleLinkSnapshot link = getScaleLinkSnapshot();
          if (event.connectionGeneration == 0) {
            event.connectionGeneration = link.connectionGeneration;
          }
          if (event.connectionGeneration != link.connectionGeneration ||
              (session.active &&
               static_cast<int32_t>(event.receivedAtMs -
                                    session.startedAtMs) < 0)) {
            addDebugEvent(DebugCategory::SCALE,
                          DebugCode::SCALE_STALE_EVENT_REJECTED,
                          static_cast<int32_t>(event.connectionGeneration),
                          static_cast<int32_t>(link.connectionGeneration));
            break;
          }
        }
        observedWeight = event.weightG;
        observedWeightReceivedAtMs = event.receivedAtMs;
        observedWeightSequence = event.packetSequence;
        observedWeightConnectionGeneration = event.connectionGeneration;
        weightStreamState = fabsf(event.weightG) > MAX_AUTOMATION_WEIGHT_G
                                ? WeightStreamState::OVERLOAD
                                : WeightStreamState::FRESH;
        if (recordWeightSampleWithProvenance(
                event.weightG, event.receivedAtMs, event.packetSequence,
                event.connectionGeneration)) {
          currentWeight = event.weightG;
          currentWeightReceivedAtMs = event.receivedAtMs;
          currentWeightSequence = event.packetSequence;
          currentWeightConnectionGeneration = event.connectionGeneration;
        }
        break;

      case ScaleEventType::TIMER_START_RESULT:
        if (event.cycleId == session.id) {
          session.remoteTimerMayBeRunning = event.commandAttempted;
          session.remoteTimerStarted = event.writeSucceeded;
          if (event.writeSucceeded && session.config.autoTare &&
              session.startedWithScale) {
            armPostTareBaselineWindow();
          }
        }
        Serial.print("Remote timer start write: ");
        Serial.println(event.writeSucceeded ? "successful" : "failed/skipped");
        addDebugEvent(DebugCategory::SCALE,
                      event.writeSucceeded ? DebugCode::SCALE_TIMER_START_OK
                                           : DebugCode::SCALE_TIMER_START_FAILED,
                      static_cast<int32_t>(event.cycleId));
        break;

      case ScaleEventType::TIMER_STOP_RESULT:
        if (event.cycleId == session.id) {
          session.stopTimerCommandQueued = false;
          session.timerStopResult =
              !event.commandAttempted
                  ? TimerStopResult::NOT_ATTEMPTED
                  : (event.writeSucceeded ? TimerStopResult::WRITE_SUCCEEDED
                                          : TimerStopResult::WRITE_FAILED);
        }
        Serial.print("Remote timer stop write for cycle ");
        Serial.print(event.cycleId);
        Serial.print(": ");
        Serial.println(!event.commandAttempted
                           ? "not attempted (scale disconnected)"
                           : (event.writeSucceeded ? "successful" : "failed"));
        addDebugEvent(DebugCategory::SCALE,
                      event.writeSucceeded ? DebugCode::SCALE_TIMER_STOP_OK
                                           : DebugCode::SCALE_TIMER_STOP_FAILED,
                      static_cast<int32_t>(event.cycleId));
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

void resetSessionForNewCycle(ControlSource source, uint32_t webSessionId = 0,
                             uint32_t controlLeaseId = 0) {
  session = CycleSession{};
  session.id = nextCycleId++;
  if (nextCycleId == 0) {
    nextCycleId = 1;
  }
  session.active = true;
  session.source = source;
  session.webSessionId = webSessionId;
  session.controlLeaseId = controlLeaseId;
  session.config = snapshotConfig(runtimeConfig);
  session.endReason = EndReason::NONE;
}

void beginCycle(ControlSource source = ControlSource::PHYSICAL,
                uint32_t webSessionId = 0,
                uint32_t controlLeaseId = 0) {
  if (pendingAnalysis.pending) {
    pendingAnalysis.pending = false;
    Serial.println("Previous drip analysis cancelled by a new cycle");
  }

  resetSessionForNewCycle(source, webSessionId, controlLeaseId);
  session.startedAtMs = millis();
  session.weightSequenceAtStart = currentWeightSequence;
  const ScaleLinkSnapshot scaleLinkAtStart = getScaleLinkSnapshot();
  session.startedWithScale =
      scaleLinkAvailable(scaleLinkAtStart) && currentWeightIsFresh();
  session.scaleDisconnectSequenceAtStart =
      scaleLinkAtStart.disconnectSequence;
  session.connectionGenerationAtStart =
      scaleLinkAtStart.connectionGeneration;
  session.ownedConnectionGeneration =
      scaleLinkAtStart.connectionGeneration;
  session.weightControlState =
      session.startedWithScale && !session.config.timerOnly
          ? WeightControlState::ACTIVE
          : WeightControlState::INACTIVE;
  session.automaticEnabled =
      session.weightControlState == WeightControlState::ACTIVE;
  session.calibrationEligible = session.automaticEnabled;
  if (session.startedWithScale) {
    if (session.config.autoTare) {
      armPostTareBaselineWindow();
    } else {
      session.hasWeightAnchor = true;
      session.lastAcceptedWeightG = currentWeight;
      session.lastAcceptedWeightAtMs = currentWeightReceivedAtMs;
      session.lastAcceptedPacketSequence = currentWeightSequence;
    }
  }
  resetShotTrajectory(session.startedAtMs);
  transitionTo(StopperState::QUALIFYING_ON);

  if (!setCn9Closed(true, session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }

  if (session.startedWithScale) {
    if (!requestRemoteTimerStart()) {
      session.automaticEnabled = false;
      session.scaleWasLost = true;
      Serial.println("Scale start command unavailable; cycle marked manual");
    }
  }

  Serial.print("Cycle started; goal snapshot=");
  Serial.print(session.config.goalWeightG);
  Serial.print("g, offset snapshot=");
  Serial.print(session.config.weightOffsetG);
  Serial.println("g");
}

void finalizeCycle(EndReason reason, StopperState nextState) {
  const RelaySafetySnapshot relayBeforeOpen = getRelaySafetySnapshot();
  const bool analyze = shot.confirmedBrew && !session.config.timerOnly &&
                       session.calibrationEligible;

  // Physical flow always stops before the non-blocking BLE command is queued.
  setCn9Closed(false);
  cancelScaleBrewBeep(session.id);
  session.endReason = reason;
  requestRemoteTimerStop();

  if (analyze) {
    scheduleShotAnalysis();
  }

  lastCycle.valid = true;
  lastCycle.cycleId = session.id;
  lastCycle.durationMs = elapsedMs(relayBeforeOpen.closedAtMs);
  lastCycle.endedAtMs = millis();
  lastCycle.endReason = reason;
  lastCycle.source = session.source;
  lastCycle.weightValid =
      currentWeightSequence != session.weightSequenceAtStart &&
      isfinite(currentWeight) &&
      static_cast<int32_t>(currentWeightReceivedAtMs - session.startedAtMs) >=
          0;
  lastCycle.lastWeightG = lastCycle.weightValid ? currentWeight : 0.0f;
  lastCycle.weightAgeAtEndMs =
      lastCycle.weightValid ? elapsedMs(currentWeightReceivedAtMs) : 0;
  lastCycle.weightControlState = session.weightControlState;
  lastCycle.calibrationEligible = session.calibrationEligible;
  session.active = false;
  virtualPaddleOn = false;
  Serial.print("Cycle ended by ");
  Serial.println(endReasonName(reason));
  transitionTo(nextState);
}

void enterRinse() {
  // The duration is measured from the beginning of the stable OFF level, not
  // from 30 ms later when debounce accepts the transition.
  session.rinseStartedAtMs = rawPaddleChangedAtMs;
  session.automaticEnabled = false;
  requestRemoteTimerStop();
  Serial.println("Rinse classified; paddle changes ignored until completion");
  transitionTo(StopperState::RINSE);
}

void confirmBrewOrManual() {
  if (session.config.timerOnly && session.startedWithScale) {
    Serial.println("Timer-only brew confirmed");
    transitionTo(StopperState::BREW);
    return;
  }
  if (session.startedWithScale && !session.config.timerOnly &&
      (session.receivedFreshWeightInCycle ||
       session.thresholdConfirmations > 0) &&
      session.weightControlState != WeightControlState::INACTIVE) {
    if (scaleAutomationUnavailableForSession()) {
      suspendWeightControl();
    }
    shot.confirmedBrew = true;
    Serial.println("Brew confirmed");
    transitionTo(StopperState::BREW);
    if (!session.config.timerOnly && session.config.brewConfirmationBeep) {
      requestScaleBrewBeep(session.id);
    }
  } else {
    session.automaticEnabled = false;
    Serial.println("Manual cycle confirmed (no scale automation)");
    transitionTo(StopperState::MANUAL_NO_SCALE);
  }
}

void handleQualifyingPaddleOff(uint32_t onDurationMs) {
  if (onDurationMs <= session.config.rinseGestureMs) {
    enterRinse();
    return;
  }

  // Exactly at or after the confirmation boundary, the cycle is considered a
  // confirmed brew/manual shot and is then immediately stopped by paddle OFF.
  if (onDurationMs >= session.config.brewConfirmMs) {
    if (session.automaticEnabled) {
      shot.confirmedBrew = true;
    }
    finalizeCycle(EndReason::PADDLE, StopperState::READY);
    return;
  }

  finalizeCycle(EndReason::SHORT_SHOT, StopperState::READY);
}

bool automaticScaleStopDue() {
  if (session.config.timerOnly || stopperState != StopperState::BREW) {
    return false;
  }

  const bool directStopFresh = session.directStopPending &&
      session.thresholdConfirmations >= DIRECT_STOP_CONFIRMATION_SAMPLES &&
      static_cast<int32_t>(millis() - session.lastThresholdAtMs) >= 0 &&
      elapsedMs(session.lastThresholdAtMs) <= MAX_AUTOMATION_WEIGHT_AGE_MS;
  if (elapsedMs(session.startedAtMs) >= session.config.minAutoStopMs &&
      directStopFresh) {
    return true;
  }

  if (!session.receivedFreshWeightInCycle ||
      session.weightControlState != WeightControlState::ACTIVE ||
      currentWeightSequence == session.weightSequenceAtStart ||
      scaleAutomationUnavailableForSession()) {
    return false;
  }
  const float elapsedS = cycleElapsedSeconds();
  return elapsedMs(session.startedAtMs) >= session.config.minAutoStopMs &&
         elapsedS >= shot.expectedEndS;
}

void handleGlobalLimitTrip() {
  const bool wasAlreadyOpenedByTimer = consumeRelaySafetyTrip();
  if (wasAlreadyOpenedByTimer) {
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_OPENED);
  }
  if (!wasAlreadyOpenedByTimer && getRelaySafetySnapshot().closed) {
    setCn9Closed(false);
  }

  if (!session.active) {
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }

  finalizeCycle(EndReason::GLOBAL_LIMIT, StopperState::REQUIRES_OFF);
}

void handleOperationalLimitTrip() {
  const bool wasAlreadyOpenedByTimer = consumeOperationalLimitTrip();
  if (wasAlreadyOpenedByTimer) {
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_OPENED);
  }
  addDebugEvent(DebugCategory::SECURITY, DebugCode::OPERATIONAL_LIMIT);
  if (!session.active) {
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  finalizeCycle(EndReason::CONFIGURED_WALL_LIMIT,
                StopperState::REQUIRES_OFF);
}

void stateMachineTask() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (relay.tripped ||
      (relay.closed &&
       elapsedMs(relay.closedAtMs) >= HARD_MAX_CN9_CLOSED_MS)) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::HARD_LIMIT);
    handleGlobalLimitTrip();
    return;
  }
  if (relay.operationalTripped ||
      (relay.closed && relay.operationalLimitMs < HARD_MAX_CN9_CLOSED_MS &&
       elapsedMs(relay.closedAtMs) >= relay.operationalLimitMs)) {
    handleOperationalLimitTrip();
    return;
  }

  if (maintenanceLease.active) {
    if (relay.closed) {
      setCn9Closed(false);
    }
    if (paddleOn || rawPaddleOn) {
      if (!maintenanceLease.forwarded) {
        addDebugEvent(DebugCategory::SECURITY,
                      DebugCode::MAINTENANCE_CANCELED,
                      static_cast<int32_t>(maintenanceLease.id));
        maintenanceCancellationCommand = WebCommand{};
        maintenanceCancellationCommand.type =
            WebCommandType::MAINTENANCE_COMPLETE;
        maintenanceCancellationCommand.requestId =
            maintenanceLease.command.requestId;
        maintenanceCancellationCommand.succeeded = false;
        maintenanceCancellationCommand.resultState =
            CommandResultState::CANCELED;
        maintenanceCancellationPending = true;
        maintenanceLease = MaintenanceLease{};
      }
      transitionTo(StopperState::REQUIRES_OFF);
    }
    return;
  }

  if (session.active && session.source == ControlSource::WEB &&
      (paddleTurnedOn || paddleTurnedOff)) {
    const bool mustRelease = paddleOn || rawPaddleOn;
    finalizeCycle(EndReason::PHYSICAL_OVERRIDE,
                  mustRelease ? StopperState::REQUIRES_OFF
                              : StopperState::READY);
    return;
  }

  switch (stopperState) {
    case StopperState::REQUIRES_OFF:
      if (paddleIsStablyOff()) {
        transitionTo(StopperState::READY);
      }
      return;

    case StopperState::READY:
      if (paddleTurnedOn) {
        beginCycle(ControlSource::PHYSICAL);
      }
      return;

    case StopperState::QUALIFYING_ON:
      // Classify a simultaneous paddle release before degrading the session
      // for a BLE loss, as required by the event-priority contract.
      if (paddleTurnedOff) {
        handleQualifyingPaddleOff(elapsedMs(session.startedAtMs));
        return;
      }

      // A BLE loss suspends by-weight authority but does not skip gesture
      // classification; a quick release must still become a rinse.
      if (session.weightControlState == WeightControlState::ACTIVE &&
          scaleAutomationUnavailableForSession()) {
        suspendWeightControl();
        Serial.println("Scale stream suspended while qualifying");
      }

      expirePostTareBaselineIfNeeded();

      if (elapsedMs(session.startedAtMs) >= session.config.brewConfirmMs) {
        confirmBrewOrManual();
      }
      return;

    case StopperState::RINSE:
      // All paddle transitions are intentionally consumed while rinsing.
      if (elapsedMs(session.rinseStartedAtMs) >=
          session.config.rinseDurationMs) {
        requestRemoteTimerStop();
        const bool mustReleasePaddle = paddleOn || rawPaddleOn;
        finalizeCycle(EndReason::RINSE_COMPLETE,
                      mustReleasePaddle ? StopperState::REQUIRES_OFF
                                        : StopperState::READY);
      }
      return;

    case StopperState::BREW:
      if (paddleTurnedOff) {
        finalizeCycle(EndReason::PADDLE, StopperState::READY);
        return;
      }

      if ((session.weightControlState == WeightControlState::ACTIVE ||
           session.weightControlState == WeightControlState::VALIDATING) &&
          scaleAutomationUnavailableForSession()) {
        suspendWeightControl();
        weightStreamState = WeightStreamState::STALE;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_STREAM_STALE);
        Serial.println("Scale stream suspended during brew");
      }

      if (automaticScaleStopDue()) {
        const EndReason reason = session.directStopPending
                                     ? session.directStopReason
                                     : EndReason::SCALE_PREDICTION;
        finalizeCycle(reason,
                      StopperState::REQUIRES_OFF);
      }
      return;

    case StopperState::MANUAL_NO_SCALE:
      if (paddleTurnedOff) {
        finalizeCycle(EndReason::PADDLE, StopperState::READY);
      }
      return;
  }
}

// ---------------------------------------------------------------------------
// Web/control bridge
// ---------------------------------------------------------------------------

bool controlAllowsConfigurationNow() {
  return stopperState == StopperState::READY && !session.active &&
         !getRelaySafetySnapshot().closed && !paddleOn && !rawPaddleOn &&
         !maintenanceLease.active;
}

void beginWebRinse(uint32_t webSessionId, uint32_t controlLeaseId) {
  if (pendingAnalysis.pending) {
    pendingAnalysis.pending = false;
    Serial.println("Previous drip analysis cancelled by a web rinse");
  }
  resetSessionForNewCycle(ControlSource::WEB, webSessionId, controlLeaseId);
  session.startedAtMs = millis();
  session.weightSequenceAtStart = currentWeightSequence;
  session.rinseStartedAtMs = session.startedAtMs;
  session.startedWithScale = false;
  session.automaticEnabled = false;
  virtualPaddleOn = false;
  resetShotTrajectory(session.startedAtMs);
  if (!setCn9Closed(true, session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  transitionTo(StopperState::RINSE);
}

bool forwardAcceptedNetworkCommand(const WebCommand &command) {
#ifndef SHOT_STOPPER_HOST_TEST
  if (!networkManager.enqueueAcceptedCommand(command)) {
    addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                  static_cast<int32_t>(command.type));
    return false;
  }
  return true;
#else
  ++hostForwardAcceptedNetworkCommandCalls;
  hostLastForwardedNetworkCommand = command;
  return hostForwardAcceptedNetworkCommandSucceeds;
#endif
}

void reportControlCommandResult(const WebCommand &command,
                                CommandResultState state) {
  if (command.requestId == 0 || controlResultPending) {
    return;
  }
  WebCommand result;
  result.type = WebCommandType::MAINTENANCE_COMPLETE;
  result.requestId = command.requestId;
  result.succeeded = state == CommandResultState::APPLIED ||
                     state == CommandResultState::PERSISTED;
  result.resultState = state;
  if (!forwardAcceptedNetworkCommand(result)) {
    controlResultCommand = result;
    controlResultPending = true;
  }
}

void serviceControlCommandResult() {
  if (controlResultPending &&
      forwardAcceptedNetworkCommand(controlResultCommand)) {
    controlResultPending = false;
    controlResultCommand = WebCommand{};
  }
}

void serviceMaintenanceCancellation() {
  if (maintenanceCancellationPending &&
      forwardAcceptedNetworkCommand(maintenanceCancellationCommand)) {
    maintenanceCancellationPending = false;
    maintenanceCancellationCommand = WebCommand{};
  }
}

bool beginMaintenanceLease(const WebCommand &networkCommand,
                           bool applyRuntimeOnSuccess) {
  if (!controlAllowsConfigurationNow()) {
    return false;
  }
  setCn9Closed(false);
  maintenanceLease = MaintenanceLease{};
  maintenanceLease.active = true;
  maintenanceLease.applyRuntimeOnSuccess = applyRuntimeOnSuccess;
  maintenanceLease.id = networkCommand.requestId;
  if (maintenanceLease.id == 0) {
    maintenanceLease.id = nextInternalRequestId++;
  }
  maintenanceLease.startedAtMs = millis();
  maintenanceLease.command = networkCommand;
  maintenanceLease.command.requestId = maintenanceLease.id;
  maintenanceLease.command.maintenanceLeaseId = maintenanceLease.id;
  addDebugEvent(DebugCategory::SECURITY, DebugCode::MAINTENANCE_RESERVED,
                static_cast<int32_t>(maintenanceLease.id),
                static_cast<int32_t>(networkCommand.type));
  return true;
}

void serviceMaintenanceLease() {
  if (!maintenanceLease.active || maintenanceLease.forwarded ||
      !paddleIsStablyOff() ||
      elapsedMs(maintenanceLease.startedAtMs) <
          MAINTENANCE_LEASE_SETTLE_MS) {
    return;
  }
  if (forwardAcceptedNetworkCommand(maintenanceLease.command)) {
    maintenanceLease.forwarded = true;
#ifdef SHOT_STOPPER_HOST_TEST
    WebCommand result;
    result.type = WebCommandType::MAINTENANCE_COMPLETE;
    result.requestId = maintenanceLease.command.requestId;
    result.maintenanceLeaseId = maintenanceLease.id;
    result.config = maintenanceLease.command.config;
    result.succeeded = true;
    completeMaintenanceLease(result);
#endif
  }
}

void serviceRuntimePersistence() {
  if (!runtimePersistPending || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow()) {
    return;
  }
  WebCommand persist;
  persist.type = WebCommandType::PERSIST_RUNTIME;
  persist.requestId = nextInternalRequestId++;
  if (nextInternalRequestId < 0x80000000UL) {
    nextInternalRequestId = 0x80000000UL;
  }
  persist.config = runtimePersistCandidate;
  if (beginMaintenanceLease(persist, true)) {
    runtimePersistPending = false;
    runtimePersistRequestId = persist.requestId;
  } else {
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
}

void completeMaintenanceLease(const WebCommand &result) {
  if (!maintenanceLease.active ||
      result.maintenanceLeaseId != maintenanceLease.id) {
    addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                  static_cast<int32_t>(result.type));
    return;
  }
  if (result.succeeded && maintenanceLease.applyRuntimeOnSuccess) {
    runtimeConfig = result.config;
    addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                  static_cast<int32_t>(runtimeConfig.revision));
  }
  if (!result.succeeded &&
      maintenanceLease.command.type == WebCommandType::PERSIST_RUNTIME &&
      maintenanceLease.id == runtimePersistRequestId) {
    runtimePersistCandidate = maintenanceLease.command.config;
    runtimePersistPending = true;
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
  addDebugEvent(DebugCategory::SECURITY,
                result.succeeded ? DebugCode::MAINTENANCE_COMPLETED
                                 : DebugCode::COMMAND_FAILED,
                static_cast<int32_t>(maintenanceLease.id));
  maintenanceLease = MaintenanceLease{};
  if (paddleOn || rawPaddleOn) {
    transitionTo(StopperState::REQUIRES_OFF);
  }
}

void rejectWebCommand(const WebCommand &command) {
  addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                static_cast<int32_t>(command.type));
  reportControlCommandResult(command, CommandResultState::FAILED);
}

void processWebCommand(const WebCommand &command) {
  switch (command.type) {
    case WebCommandType::STOP:
    case WebCommandType::STOP_HEARTBEAT:
    case WebCommandType::PADDLE_OFF:
      if (command.type == WebCommandType::PADDLE_OFF &&
          (command.webSessionId == 0 || command.controlLeaseId == 0 ||
           command.webSessionId != session.webSessionId ||
           command.controlLeaseId != session.controlLeaseId)) {
        rejectWebCommand(command);
        return;
      }
      if (!session.active || !getRelaySafetySnapshot().closed ||
          (command.type == WebCommandType::PADDLE_OFF &&
           session.source != ControlSource::WEB)) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::WEB,
                    command.type == WebCommandType::PADDLE_OFF
                        ? DebugCode::WEB_PADDLE_OFF
                        : DebugCode::WEB_STOP,
                    static_cast<int32_t>(command.type),
                    static_cast<int32_t>(session.id));
      finalizeCycle(
          command.type == WebCommandType::STOP_HEARTBEAT
              ? EndReason::WEB_HEARTBEAT_TIMEOUT
              : EndReason::WEB_STOP,
          (paddleOn || rawPaddleOn) ? StopperState::REQUIRES_OFF
                                    : StopperState::READY);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::PADDLE_ON:
      if (!REMOTE_CN9_CONTROL_ENABLED || command.webSessionId == 0 ||
          command.controlLeaseId == 0 ||
          !controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      virtualPaddleOn = true;
      beginCycle(ControlSource::WEB, command.webSessionId,
                 command.controlLeaseId);
      if (!session.active) {
        virtualPaddleOn = false;
        reportControlCommandResult(command, CommandResultState::FAILED);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_PADDLE_ON,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::RINSE:
      if (!REMOTE_CN9_CONTROL_ENABLED || command.webSessionId == 0 ||
          command.controlLeaseId == 0 ||
          !controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      beginWebRinse(command.webSessionId, command.controlLeaseId);
      if (!session.active) {
        reportControlCommandResult(command, CommandResultState::FAILED);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_RINSE,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::APPLY_CONFIG: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      RuntimeConfig candidate = command.config;
      // Offset is learned by the control loop and is intentionally not a Web
      // field. Preserve the newest value if a form built from an older status
      // snapshot races with the post-shot analysis.
      candidate.weightOffsetG = runtimeConfig.weightOffsetG;
      if (validateRuntimeConfig(candidate) != ConfigValidationError::NONE) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return;
      }
      candidate.revision = runtimeConfig.revision + 1;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      WebCommand persist;
      persist.type = WebCommandType::PERSIST_RUNTIME;
      persist.requestId = command.requestId;
      persist.config = candidate;
      if (!beginMaintenanceLease(persist, true)) {
        rejectWebCommand(command);
      }
      return;
    }

    case WebCommandType::RESET_WEIGHT_OFFSET: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      // Prevent a completed shot's delayed drip analysis from immediately
      // replacing the user-requested default calibration.
      pendingAnalysis.pending = false;
      RuntimeConfig candidate = runtimeConfig;
      candidate.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      WebCommand persist;
      persist.type = WebCommandType::PERSIST_RUNTIME;
      persist.requestId = command.requestId;
      persist.config = candidate;
      if (!beginMaintenanceLease(persist, true)) {
        rejectWebCommand(command);
      }
      return;
    }

    case WebCommandType::SAVE_NETWORK:
    case WebCommandType::FORGET_NETWORK:
    case WebCommandType::CHANGE_AP_PASSWORD:
    case WebCommandType::RESTART:
    case WebCommandType::RESET_NETWORK_UI:
    case WebCommandType::FACTORY_RESET:
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      if (!beginMaintenanceLease(command, false)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::PERSIST_RUNTIME:
      rejectWebCommand(command);
      return;

    case WebCommandType::START_WIFI_SCAN:
      if (!beginMaintenanceLease(command, false)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::MAINTENANCE_COMPLETE:
      completeMaintenanceLease(command);
      return;
  }
}

void processWebCommands() {
  if (webCommandQueue == nullptr || controlResultPending ||
      maintenanceCancellationPending) {
    return;
  }
  WebCommand command;
  if (xQueueReceive(webCommandQueue, &command, 0) == pdTRUE) {
    processWebCommand(command);
  }
}

void publishControlStatus() {
  const uint32_t now = millis();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
  ControlStatusSnapshot next;
  next.state = stopperState;
  next.activeCycle = session.active;
  next.relayClosed = relay.closed;
  // Status intentionally reports the actual GPIO level, not the debounced
  // state used by the control state machine.
  next.physicalPaddleOn = readRawPaddleOn();
  next.virtualPaddleOn = virtualPaddleOn;
  next.remoteControlEnabled = REMOTE_CN9_CONTROL_ENABLED;
  next.source = session.active ? session.source : ControlSource::NONE;
  next.cycleId = session.active ? session.id : 0;
  next.webSessionId = session.active ? session.webSessionId : 0;
  next.controlLeaseId = session.active ? session.controlLeaseId : 0;
  next.maintenanceLeaseActive = maintenanceLease.active;
  next.maintenanceLeaseId = maintenanceLease.active ? maintenanceLease.id : 0;
  next.maintenanceStartedAtMs =
      maintenanceLease.active ? maintenanceLease.startedAtMs : 0;
  next.cn9ElapsedMs = relay.closed ? elapsedMs(relay.closedAtMs) : 0;
  next.safetyState = relay.state;
  next.safetyFault = relay.fault;
  next.safetyGeneration = relay.generation;
  next.safetyTimersReady = relay.timersReady;
  next.taskWatchdogReady = relay.watchdogReady;
  next.externalSafetyPresent = relay.externalSafetyPresent;
  next.cn9FeedbackClosed = relay.feedbackClosed;
  next.resetReasonCode = relay.resetReasonCode;
  next.unsafeResetCount = relay.unsafeResetCount;
  next.resetRecoveryRequired = relay.resetRecoveryRequired;
  next.bootLoopDetected = relay.bootLoopDetected;
  next.scaleAvailable = scaleLinkAvailable(scaleLink);
  next.weightControlState = session.active
                                ? session.weightControlState
                                : WeightControlState::INACTIVE;
  if (observedWeightSequence == 0) {
    next.weightStreamState = WeightStreamState::NO_SAMPLE;
  } else if (!observedWeightIsFresh(now)) {
    next.weightStreamState = WeightStreamState::STALE;
  } else {
    next.weightStreamState = weightStreamState;
  }
  next.currentWeightValid = next.scaleAvailable && currentWeightIsFresh(now);
  next.currentWeightG = next.currentWeightValid ? currentWeight : 0.0f;
  next.currentWeightAgeMs =
      next.currentWeightValid
          ? static_cast<uint32_t>(now - currentWeightReceivedAtMs)
          : 0;
  next.observedWeightValid = observedWeightSequence > 0 &&
                             isfinite(observedWeight) &&
                             fabsf(observedWeight) <= MAX_PARSED_WEIGHT_G;
  next.observedWeightG = next.observedWeightValid ? observedWeight : 0.0f;
  next.observedWeightAgeMs = next.observedWeightValid
                                 ? elapsedMs(observedWeightReceivedAtMs)
                                 : 0;
  next.scaleConnectionGeneration = scaleLink.connectionGeneration;
  next.scalePacketSequence = scaleLink.packetSequence;
  next.scalePacketGaps = scaleLink.packetGaps;
  next.scaleRejectedPackets = scaleLink.rejectedPackets;
  next.scaleReconnects = scaleLink.reconnects;
  next.scaleLastDisconnectReason = scaleLink.lastDisconnectReason;
  next.loopMaxGapMs = loopMaxGapMs;
  next.loopStackMinWords = loopStackMinWords;
  next.scaleStackMinWords = scaleWorkerStackMinWords;
  next.freeHeapBytes = freeHeapBytes;
  next.minimumFreeHeapBytes = minimumFreeHeapBytes;
  next.largestFreeHeapBlockBytes = largestFreeHeapBlockBytes;
  next.scaleEventsDropped = scaleEventsDropped;
  next.config = runtimeConfig;
  next.lastCycle = lastCycle;
  portENTER_CRITICAL(&debugLogMux);
  next.debugEventsDropped = debugLog.overwritten();
  portEXIT_CRITICAL(&debugLogMux);
  portENTER_CRITICAL(&webStatusMux);
  publishedControlStatus = next;
  portEXIT_CRITICAL(&webStatusMux);
}

#ifndef SHOT_STOPPER_HOST_TEST
void serviceSerialRecovery() {
  static constexpr char commandText[] = "RESET_NETWORK_UI";
  static size_t matched = 0;
  size_t consumed = 0;
  while (Serial.available() > 0 && consumed < 4) {
    const char received = static_cast<char>(Serial.read());
    ++consumed;
    if (received == commandText[matched]) {
      ++matched;
      if (matched == sizeof(commandText) - 1) {
        WebCommand command;
        command.type = WebCommandType::RESET_NETWORK_UI;
        command.requestId = millis();
        if (!enqueueWebCommand(command)) {
          Serial.println("Network reset rejected: control queue full");
        }
        matched = 0;
      }
    } else {
      matched = received == commandText[0] ? 1 : 0;
    }
  }
}
#endif

// ---------------------------------------------------------------------------
// Status indication
// ---------------------------------------------------------------------------

ScaleIndicatorCondition currentScaleIndicatorCondition() {
  if (!firmwareInitializationComplete) {
    return ScaleIndicatorCondition::STARTING;
  }
  if (!bleStackReady || scaleWorkerTaskHandle == nullptr) {
    return ScaleIndicatorCondition::FAULT;
  }
  const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
  if (scaleLink.state == ScaleLinkState::DISCONNECTED) {
    return ScaleIndicatorCondition::DISCONNECTED;
  }
  return scaleLinkAvailable(scaleLink) && observedWeightIsFresh()
             ? ScaleIndicatorCondition::AVAILABLE
             : ScaleIndicatorCondition::STALE;
}

bool stopperUsesManualIndicatorPalette() {
  if (stopperState == StopperState::MANUAL_NO_SCALE) {
    return true;
  }
  if (session.active) {
    return session.config.timerOnly || !session.startedWithScale ||
           session.weightControlState != WeightControlState::ACTIVE;
  }
  return runtimeConfig.timerOnly || !scaleAvailable() ||
         !currentWeightIsFresh();
}

void updateStatusIndicators() {
  if (!statusIndicatorsReady || statusIndicatorQueue == nullptr) {
    return;
  }

  const uint32_t now = millis();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const IndicatorSignal scaleSignal =
      scaleIndicatorSignal(currentScaleIndicatorCondition());
  const IndicatorSignal stopperSignal = stopperIndicatorSignal(
      stopperState, relay.state,
      relay.watchdogReady && relay.timersReady &&
          !criticalTaskWatchdogFault,
      maintenanceLease.active, stopperUsesManualIndicatorPalette());
  const StatusIndicatorFrame requested = {
      renderIndicatorSignal(scaleSignal, now),
      renderIndicatorSignal(stopperSignal, now)};
  if (indicatorFramePublished &&
      requested.scale == lastPublishedIndicatorFrame.scale &&
      requested.stopper == lastPublishedIndicatorFrame.stopper) {
    return;
  }
  if (xQueueOverwrite(statusIndicatorQueue, &requested) == pdPASS) {
    lastPublishedIndicatorFrame = requested;
    indicatorFramePublished = true;
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  // Establish the relay's safe electrical state before Serial, EEPROM or BLE.
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);

  if (EXTERNAL_SAFETY_HARDWARE_PRESENT) {
    digitalWrite(SAFETY_HEARTBEAT_GPIO, LOW);
    pinMode(SAFETY_HEARTBEAT_GPIO, OUTPUT);
    digitalWrite(SAFETY_HEARTBEAT_GPIO, LOW);
    pinMode(CN9_FEEDBACK_GPIO, INPUT_PULLUP);
  }
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  safetyResetStatus = beginSafetyResetGuard();

  initializePaddleInput();
  platformClockReady = setCpuFrequencyMhz(80);
  relaySafetyTimersReady = initializeRelaySafetyTimer();
  taskWatchdogReady =
      configureTaskWatchdog() && subscribeCurrentTaskToWatchdog();
  portENTER_CRITICAL(&relayMux);
  relaySafetyState = platformClockReady && relaySafetyTimersReady &&
                             taskWatchdogReady &&
                             !safetyResetStatus.recoveryRequired
                         ? RelaySafetyState::OPEN
                         : RelaySafetyState::LOCKOUT;
  relaySafetyFault =
      !platformClockReady || !relaySafetyTimersReady
          ? RelaySafetyFault::INITIALIZATION_FAILED
          : (!taskWatchdogReady
                 ? RelaySafetyFault::WATCHDOG_UNAVAILABLE
                 : (safetyResetStatus.resetDuringClose
                        ? RelaySafetyFault::RESET_DURING_CLOSE
                        : (safetyResetStatus.bootLoopDetected
                               ? RelaySafetyFault::BOOT_LOOP
                               : (safetyResetStatus.unsafeReset
                                      ? RelaySafetyFault::UNSAFE_RESET
                                      : RelaySafetyFault::NONE))));
  portEXIT_CRITICAL(&relayMux);

  if (EXTERNAL_SAFETY_HARDWARE_PRESENT && readCn9FeedbackClosed()) {
    tripRelaySafety(RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
  }

  Serial.begin(9600);
  Serial.println("Shot Stopper Micra initializing");
  statusIndicatorsReady = initializeStatusIndicators();
  if (!statusIndicatorsReady) {
    Serial.println("Status indicators unavailable; control remains active");
  }
  updateStatusIndicators();
  if (!relaySafetyTimersReady) {
    Serial.println("FATAL: relay safety timers unavailable; CN9 will not close");
  }
  if (!platformClockReady) {
    Serial.println("FATAL: CPU frequency setup failed; CN9 will not close");
    addDebugEvent(DebugCategory::SECURITY,
                  DebugCode::INITIALIZATION_FAILED, 1);
  }
  if (!taskWatchdogReady) {
    Serial.println("FATAL: task watchdog unavailable; CN9 will not close");
  }
  if (safetyResetStatus.recoveryRequired) {
    Serial.println(
        "SAFETY LOCKOUT: cycle physical paddle ON then OFF to recover");
  }

  persistenceReady = EEPROM.begin(EEPROM_SIZE);
#ifndef SHOT_STOPPER_HOST_TEST
  bool settingsReady = persistenceReady;
  bool generatedDevicePassword = false;
  if (settingsReady && !loadPersistedSettings(persistedSettings)) {
    bool legacyMigrated = false;
    settingsReady = initializeDefaultSettings(
        persistedSettings, EEPROM.read(WEIGHT_ADDR), EEPROM.read(OFFSET_ADDR),
        &legacyMigrated);
    generatedDevicePassword = settingsReady;
    if (settingsReady) {
      settingsReady = savePersistedSettings(persistedSettings);
    }
    if (legacyMigrated) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_MIGRATED);
    }
  }
  if (settingsReady) {
    runtimeConfig = persistedSettings.runtime;
  }
  if (generatedDevicePassword) {
    Serial.print("Generated device-unique AP/UI password: ");
    Serial.println(persistedSettings.apPassword);
    Serial.println("Store it securely; it is not exposed by the Web API");
  }
#else
  runtimeConfig = RuntimeConfig{};
#endif

  if (!persistenceReady) {
    Serial.println("Persistence unavailable; using volatile defaults");
    addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED, 2);
  }

  Serial.print("Goal weight: ");
  Serial.println(runtimeConfig.goalWeightG);
  Serial.print("Weight offset: ");
  Serial.println(runtimeConfig.weightOffsetG);

  bleStackReady = BLE.begin();
  if (bleStackReady) {
    // ArduinoBLE defaults ATT operations to five seconds. Keep the BLE owner
    // isolated, but also bound connect/discovery/subscribe/write waits so its
    // watchdog and stream-staleness telemetry react promptly.
    BLE.setTimeout(SCALE_ATT_TIMEOUT_MS);
  }
  Serial.println(bleStackReady
                     ? "BLE scale central active; local configuration removed"
                     : "BLE unavailable; stopper restricted to manual mode");
  if (!bleStackReady) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::INITIALIZATION_FAILED, 3);
  }

  if (bleStackReady && !initializeScaleWorker()) {
    Serial.println("Scale worker unavailable; manual mode only");
    bleStackReady = false;
  }

  webCommandQueue =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  if (webCommandQueue == nullptr) {
    Serial.println("Web command queue unavailable; web control disabled");
  }

  publishControlStatus();
#ifndef SHOT_STOPPER_HOST_TEST
  if (settingsReady && webCommandQueue != nullptr) {
    NetworkBridgeCallbacks callbacks;
    callbacks.copyControlStatus = copyControlStatus;
    callbacks.enqueueWebCommand = enqueueWebCommand;
    callbacks.copyDebugEvents = copyDebugEvents;
    callbacks.addDebugEvent = addDebugEvent;
    callbacks.reportTaskWatchdogFault = reportTaskWatchdogFault;
    callbacks.requestSafeRestart = requestSafeRestart;
    if (!networkManager.begin(persistedSettings, callbacks)) {
      Serial.println("Network manager unavailable; stopper remains local");
    }
  } else if (!settingsReady) {
    Serial.println("Network settings unavailable; stopper remains local");
  }
#endif

  firmwareInitializationComplete = true;
  updateStatusIndicators();
}

void loop() {
  const uint32_t loopStartedAtMs = millis();
  if (lastLoopAtMs != 0) {
    const uint32_t gap = loopStartedAtMs - lastLoopAtMs;
    if (gap > loopMaxGapMs) {
      loopMaxGapMs = gap;
    }
  }
  lastLoopAtMs = loopStartedAtMs;
  if (elapsedMs(healthTelemetryAtMs) >= HEALTH_TELEMETRY_INTERVAL_MS) {
    healthTelemetryAtMs = loopStartedAtMs;
    loopStackMinWords =
        static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
#ifndef SHOT_STOPPER_HOST_TEST
    freeHeapBytes = ESP.getFreeHeap();
    minimumFreeHeapBytes = ESP.getMinFreeHeap();
    largestFreeHeapBlockBytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
  }
  // Relay and paddle control never wait for BLE. The worker owns every scale,
  // heartbeat, packet, timer and connection operation.
  serviceRelaySafety();
  if (safeRestartRequested) {
    setCn9Closed(false);
    serviceSafetyHeartbeat(false);
#ifndef SHOT_STOPPER_HOST_TEST
    ESP.restart();
#else
    safeRestartRequested = false;
#endif
    return;
  }
  updatePaddleInput();
  // Consume only the latest attributed weight before making automatic
  // decisions. Paddle and relay safety were already sampled first, so their
  // priority is preserved without adding a full event backlog to this loop.
  processScaleWorkerEvents();
  stateMachineTask();
  servicePaddleReturnReminder();
  serviceRemoteTimerStopRetry();
  shotAnalysisTask();
#ifndef SHOT_STOPPER_HOST_TEST
  serviceSerialRecovery();
#endif
  serviceMaintenanceCancellation();
  serviceControlCommandResult();
  processWebCommands();
  serviceControlCommandResult();
  serviceRuntimePersistence();
  serviceMaintenanceLease();
  publishControlStatus();
  updateStatusIndicators();
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
    safeRestartRequested = true;
    serviceSafetyHeartbeat(false);
    return;
  }
  serviceSafetyHeartbeat(true);
  vTaskDelay(pdMS_TO_TICKS(1));
}
