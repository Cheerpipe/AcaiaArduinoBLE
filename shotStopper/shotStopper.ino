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
#include "ShotStopperVersion.h"
#include "ShotStopperHardwareTimer.h"
#include "ShotStopperIndicators.h"
#include "ShotStopperResetGuard.h"
#include "ShotStopperSafety.h"
#include "ShotStopperShotLog.h"
#include "ShotStopperTime.h"
#include "ShotStopperWatchdog.h"
#include "ShotStopperHwmon.h"

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
constexpr uint32_t SCALE_COMPLETION_BEEP_DELAY_MS = 200;
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
  TARE_ONLY,
  STOP_TIMER
};

enum class ScaleEventType : uint8_t {
  WEIGHT,
  TIMER_START_RESULT,
  TARE_RESULT,
  TIMER_STOP_RESULT
};

enum class BbwProtectionPhase : uint8_t {
  NONE,
  RETARE,
  CONFIRMATION,
  NORMAL
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
  bool bbwProtectionEnabled = false;
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
  uint32_t firstDropMs = 0;
  float scaleBaselineG = 0.0f;
  bool scaleBaselineReady = false;
  float lastAcceptedWeightG = 0.0f;
  float recoveryLastWeightG = 0.0f;
  bool retareEnded = false;
  bool brewStartConfirmEnded = false;
  bool flowDuringRetare = false;
  uint32_t retareFlowFirstDetectedAtMs = 0;
  bool retarePerformed = false;
  bool retareDisabled = false;
  bool firstDropsBeepSent = false;
  float retareCandidateWeightG = 0.0f;
  uint8_t retareStabilitySamples = 0;
  uint32_t retareStabilityStartedAtMs = 0;
  uint32_t retareLastSampleAtMs = 0;
  float retareFlowLastWeightG = 0.0f;
  bool retareFlowSampleValid = false;
  uint8_t firstDropConfirmations = 0;
  uint32_t firstDropLastAtMs = 0;
  uint32_t firstDropLastPacketSequence = 0;
  ControlSource source = ControlSource::NONE;
  CycleConfigSnapshot config = {};
  EndReason endReason = EndReason::NONE;
  bool extractionExtended = false;
  bool targetReachedEarly = false;
  uint32_t targetReachedAtMs = 0;
  bool autoToManualGuardArmed = false;
  bool autoToManualGuardEnforced = false;
  uint32_t autoToManualGuardDeadlineAtMs = 0;
};

struct PendingShotFinalize {
  bool pending = false;
  bool offsetAnalysis = false;
  bool logEligible = false;
  uint32_t endedAtMs = 0;
  uint32_t endedWeightSequence = 0;
  uint32_t cycleStartedAtMs = 0;
  uint32_t bootId = 0;
  uint16_t durationDs = 0;
  uint16_t firstDropDs = SHOT_LOG_METRIC_MISSING;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  float scaleBaselineG = 0.0f;
  bool startedWithScale = false;
  bool timerOnly = false;
  bool confirmedBrew = false;
  StopperState finalState = StopperState::READY;
  EndReason endReason = EndReason::NONE;
  bool extractionGuardEnabled = false;
  bool extractionExtended = false;
  bool targetReachedEarly = false;
  uint16_t targetReachedEarlyDs = SHOT_LOG_METRIC_MISSING;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
  bool lastKnownWeightValid = false;
  float lastKnownWeightG = 0.0f;
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
PendingShotFinalize pendingFinalize;
RuntimeConfig runtimeConfig;
LastCycleSummary lastCycle;
DebugRingBuffer debugLog;
ShotLog shotLog;

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
char scaleProtocolName[20] = "none";
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
uint32_t paddleReturnReminderStartedAtMs = 0;
bool scaleCompletionBeepPending = false;
bool scaleCompletionBeepScheduled = false;
uint32_t scaleCompletionBeepDueAtMs = 0;

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
uint32_t hostNtpSyncRequestCount = 0;
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
Hwmon hwmon;
HwmonSnapshot hwmonSnapshot = {};
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
uint32_t bootStartedAtMs = 0;
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
  char protocolName[20];
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

size_t copyShotRecords(ShotLogRecord *output, size_t capacity) {
  return shotLog.copyNewestFirst(output, capacity);
}

bool deleteShotRecord(uint32_t id) {
  return shotLog.removeById(id);
}

bool clearShotLog() {
  return shotLog.clear();
}

bool enqueueWebCommand(const WebCommand &command) {
  return webCommandQueue != nullptr &&
         xQueueSend(webCommandQueue, &command, 0) == pdTRUE;
}

ScaleLinkSnapshot getScaleLinkSnapshot() {
  ScaleLinkSnapshot snapshot = {};
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
  memcpy(snapshot.protocolName, scaleProtocolName, sizeof(snapshot.protocolName));
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
    if (session.autoToManualGuardArmed &&
        !session.autoToManualGuardEnforced) {
      session.autoToManualGuardEnforced = true;
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::AUTO_TO_MANUAL_GUARD_ENFORCED,
                    static_cast<int32_t>(
                        elapsedMs(session.startedAtMs)),
                    static_cast<int32_t>(
                        session.autoToManualGuardDeadlineAtMs -
                        session.startedAtMs));
    }
  } else if (state == WeightControlState::ACTIVE &&
             (previous == WeightControlState::SUSPENDED ||
              previous == WeightControlState::VALIDATING)) {
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_CONTROL_RECOVERED);
    if (session.autoToManualGuardEnforced) {
      session.autoToManualGuardEnforced = false;
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::AUTO_TO_MANUAL_GUARD_CLEARED);
    }
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
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
      return "fast extraction max weight";
    case EndReason::FAST_EXTRACTION_MIN_TIME:
      return "fast extraction min time";
    case EndReason::AUTO_TO_MANUAL_GUARD:
      return "auto-to-manual time guard";
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

bool requestRemoteRetare() {
  if (!session.config.autoTare) {
    return false;
  }
  ScaleCommand command;
  command.type = ScaleCommandType::TARE_ONLY;
  command.cycleId = session.id;
  command.autoTare = true;
  return enqueueScaleCommand(command);
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
  if (session.extractionExtended) {
    shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
    return;
  }
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

float effectiveMaxStopThreshold() {
  return session.config.maxRecoveryWeightG - session.config.weightOffsetG;
}

bool fastExtractionGuardSession() {
  return session.active && session.config.fastExtractionGuardEnabled &&
         !session.config.timerOnly && session.startedWithScale;
}

bool minBrewTimeReached() {
  return elapsedMs(session.startedAtMs) >= session.config.minBrewTimeMs;
}

bool targetWeightReached(float weight) {
  return weight >= effectiveStopThreshold();
}

void requestScaleBrewBeep(uint32_t cycleId);

void recordFirstDropTimestamp(uint32_t receivedAtMs) {
  if (session.firstDropMs == 0) {
    session.firstDropMs = receivedAtMs;
  }
}

void requestFirstDropBeep() {
  if (!session.firstDropsBeepSent && session.config.brewConfirmationBeep) {
    requestScaleBrewBeep(session.id);
    session.firstDropsBeepSent = true;
  }
}

void notifyRetareFlowDetected(uint32_t receivedAtMs) {
  if (session.retareFlowFirstDetectedAtMs == 0) {
    session.retareFlowFirstDetectedAtMs = receivedAtMs;
    recordFirstDropTimestamp(receivedAtMs);
    requestFirstDropBeep();
    addDebugEvent(DebugCategory::SCALE, DebugCode::FIRST_DROP_DURING_RETARE,
                  static_cast<int32_t>(session.id),
                  static_cast<int32_t>(elapsedMs(session.startedAtMs)));
  }
  session.flowDuringRetare = true;
}

void resetDirectStopConfirmation() {
  session.thresholdConfirmations = 0;
  session.lastThresholdAtMs = 0;
  session.lastThresholdPacketSequence = 0;
  session.lastThresholdConnectionGeneration = 0;
  session.directStopPending = false;
}

bool bbwAutomaticScaleSession() {
  return session.active && session.bbwProtectionEnabled;
}

bool retareWindowOpen() {
  if (!bbwAutomaticScaleSession() || !session.config.autoRetare) {
    return false;
  }
  if (session.retareEnded || session.retarePerformed) {
    return false;
  }
  return elapsedMs(session.startedAtMs) < session.config.retareWindowMs;
}

bool retareHasEnded() {
  if (!bbwAutomaticScaleSession() || !session.config.autoRetare) {
    return true;
  }
  return session.retareEnded;
}

bool brewStartConfirmationOpen() {
  if (!bbwAutomaticScaleSession()) {
    return false;
  }
  return !session.brewStartConfirmEnded;
}

void skipBrewStartConfirmationDueToRetareFlow(uint32_t receivedAtMs) {
  session.brewStartConfirmEnded = true;
  resetDirectStopConfirmation();
  if (session.firstDropMs == 0) {
    session.firstDropMs = session.retareFlowFirstDetectedAtMs != 0
                              ? session.retareFlowFirstDetectedAtMs
                              : receivedAtMs;
  }
}

void resetRetareStabilityStreak();

void markRetareEnded(uint32_t endedAtMs) {
  if (session.retareEnded) {
    return;
  }
  session.retareEnded = true;
  session.retareDisabled = true;
  resetRetareStabilityStreak();
  if (session.flowDuringRetare && !session.retarePerformed) {
    skipBrewStartConfirmationDueToRetareFlow(endedAtMs);
  }
}

void endBrewStartConfirmation(uint32_t receivedAtMs, bool allowBeep) {
  (void)receivedAtMs;
  session.brewStartConfirmEnded = true;
  resetDirectStopConfirmation();
  if (allowBeep && !session.firstDropsBeepSent &&
      session.config.brewConfirmationBeep && retareHasEnded()) {
    requestScaleBrewBeep(session.id);
    session.firstDropsBeepSent = true;
  }
}

bool bbwWeightStopInhibited() {
  if (!bbwAutomaticScaleSession()) {
    return false;
  }
  if (retareWindowOpen()) {
    return true;
  }
  if (brewStartConfirmationOpen()) {
    return true;
  }
  return false;
}

bool readyToConfirmBrew() {
  return elapsedMs(session.startedAtMs) > session.config.rinseGestureMs;
}

void resetRetareStabilityStreak() {
  session.retareStabilitySamples = 0;
  session.retareStabilityStartedAtMs = 0;
  session.retareLastSampleAtMs = 0;
}

void onFirstDropsDetected(uint32_t receivedAtMs) {
  if (retareWindowOpen()) {
    return;
  }
  recordFirstDropTimestamp(receivedAtMs);
  endBrewStartConfirmation(receivedAtMs, true);
}

void performAutomaticRetare() {
  if (session.retarePerformed || session.retareDisabled || !retareWindowOpen()) {
    return;
  }
  session.retarePerformed = true;
  resetRetareStabilityStreak();
  (void)requestRemoteRetare();
  markRetareEnded(millis());
}

void considerRetareCupCandidate(float weight, uint32_t receivedAtMs) {
  if (!retareWindowOpen() || session.retareDisabled || session.retarePerformed) {
    return;
  }
  if (weight < session.config.minimumCupWeightG) {
    resetRetareStabilityStreak();
    return;
  }

  if (session.retareStabilitySamples == 0) {
    session.retareCandidateWeightG = weight;
    session.retareStabilitySamples = 1;
    session.retareStabilityStartedAtMs = receivedAtMs;
    session.retareLastSampleAtMs = receivedAtMs;
    return;
  }

  if (static_cast<uint32_t>(receivedAtMs - session.retareLastSampleAtMs) >
          session.config.retareStabilityMaxGapMs ||
      fabsf(weight - session.retareCandidateWeightG) >
          session.config.retareStabilityToleranceG) {
    session.retareCandidateWeightG = weight;
    session.retareStabilitySamples = 1;
    session.retareStabilityStartedAtMs = receivedAtMs;
    session.retareLastSampleAtMs = receivedAtMs;
    return;
  }

  session.retareCandidateWeightG = weight;
  session.retareLastSampleAtMs = receivedAtMs;
  if (session.retareStabilitySamples < UINT8_MAX) {
    ++session.retareStabilitySamples;
  }
  const uint32_t stableDurationMs =
      static_cast<uint32_t>(receivedAtMs - session.retareStabilityStartedAtMs);
  const bool samplesMet =
      session.retareStabilitySamples >= session.config.retareStabilitySamples;
  const bool durationMet =
      session.config.retareStabilityMinDurationMs == 0U ||
      stableDurationMs >= session.config.retareStabilityMinDurationMs;
  if (samplesMet && durationMet &&
      session.retareCandidateWeightG >= session.config.minimumCupWeightG) {
    performAutomaticRetare();
  }
}

void initializeBbwProtection() {
  session.bbwProtectionEnabled = false;
  session.retareEnded = false;
  session.brewStartConfirmEnded = false;
  session.flowDuringRetare = false;
  session.retareFlowFirstDetectedAtMs = 0;
  session.retarePerformed = false;
  session.retareDisabled = false;
  session.firstDropsBeepSent = false;
  session.retareCandidateWeightG = 0.0f;
  resetRetareStabilityStreak();
  session.retareFlowLastWeightG = 0.0f;
  session.retareFlowSampleValid = false;
  session.retareFlowFirstDetectedAtMs = 0;
  session.firstDropConfirmations = 0;
  session.firstDropLastAtMs = 0;
  session.firstDropLastPacketSequence = 0;

  if (!session.startedWithScale || session.config.timerOnly ||
      !session.automaticEnabled) {
    session.retareEnded = true;
    session.brewStartConfirmEnded = true;
    return;
  }
  session.bbwProtectionEnabled = true;
  if (!session.config.autoRetare) {
    session.retareEnded = true;
  }
}

void serviceBbwProtectionPhases() {
  if (!session.active || !session.bbwProtectionEnabled) {
    return;
  }
  const uint32_t nowMs = millis();
  if (!session.retareEnded && session.config.autoRetare &&
      !session.retarePerformed &&
      elapsedMs(session.startedAtMs) >= session.config.retareWindowMs) {
    markRetareEnded(nowMs);
  }
  if (!session.brewStartConfirmEnded &&
      elapsedMs(session.startedAtMs) >= session.config.confirmationTimeoutMs) {
    session.brewStartConfirmEnded = true;
    resetDirectStopConfirmation();
  }
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
  if (!shouldTrackWeight() || packetSequence == 0 || !isfinite(weight) ||
      bbwWeightStopInhibited()) {
    return;
  }

  const bool overload = fabsf(weight) > MAX_AUTOMATION_WEIGHT_G;
  const bool overMax =
      fastExtractionGuardSession() && session.extractionExtended &&
      weight >= effectiveMaxStopThreshold();
  const bool overThreshold = weight >= effectiveStopThreshold();
  if (!overThreshold && !overload && !overMax) {
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

  if (overload) {
    session.directStopPending = true;
    session.directStopReason = EndReason::WEIGHT_ANOMALY;
    weightStreamState = WeightStreamState::OVERLOAD;
    session.calibrationEligible = false;
    setWeightControlState(WeightControlState::FAULT_STOPPED);
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_OVERLOAD_CONFIRMED,
                  static_cast<int32_t>(weight));
    return;
  }

  if (overMax) {
    session.directStopPending = true;
    session.directStopReason = EndReason::FAST_EXTRACTION_MAX_WEIGHT;
    addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_STOP_MAX,
                  static_cast<int32_t>(weight * 100.0f));
    return;
  }

  if (overThreshold && fastExtractionGuardSession() && !minBrewTimeReached()) {
    if (!session.extractionExtended) {
      session.extractionExtended = true;
      session.targetReachedEarly = true;
      session.targetReachedAtMs = receivedAtMs;
      addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_ENTERED,
                    static_cast<int32_t>(weight * 100.0f),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
    }
    resetDirectStopConfirmation();
    return;
  }

  session.directStopPending = true;
  session.directStopReason = EndReason::SCALE_THRESHOLD;
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_THRESHOLD_CONFIRMED,
                static_cast<int32_t>(weight * 100.0f));
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

void considerScaleFlowMarkers(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence);

bool recordWeightSampleWithProvenance(float weight, uint32_t receivedAtMs,
                                      uint32_t packetSequence,
                                      uint32_t connectionGeneration) {
  if (!isfinite(weight) || fabsf(weight) > MAX_PARSED_WEIGHT_G) {
    Serial.println("Invalid or out-of-range weight ignored");
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_INVALID, weight);
    return false;
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

  if (session.active && session.startedWithScale) {
    if (retareWindowOpen() &&
        (!session.awaitingPostTareBaseline ||
         weight >= session.config.minimumCupWeightG) &&
        weight >= MIN_AUTOMATION_WEIGHT_G &&
        weight <= MAX_AUTOMATION_WEIGHT_G) {
      considerRetareCupCandidate(weight, receivedAtMs);
    }
    considerScaleFlowMarkers(weight, receivedAtMs, packetSequence);
  }

  if (!shouldTrackWeight()) {
    return weight >= MIN_AUTOMATION_WEIGHT_G &&
           weight <= MAX_AUTOMATION_WEIGHT_G;
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

void considerScaleFlowMarkers(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence) {
  if (!session.active || !session.startedWithScale || session.firstDropMs != 0) {
    return;
  }
  if (session.awaitingPostTareBaseline) {
    if (fabsf(weight) <= POST_TARE_BASELINE_MAX_ABS_G) {
      session.scaleBaselineG = weight;
      session.scaleBaselineReady = true;
    }
    return;
  }
  if (!session.scaleBaselineReady) {
    session.scaleBaselineG = weight;
    session.scaleBaselineReady = true;
    return;
  }
  const float deltaFromBaseline = weight - session.scaleBaselineG;
  if (deltaFromBaseline < FIRST_DROP_THRESHOLD_G) {
    session.firstDropConfirmations = 0;
    return;
  }
  const float firstDropMaxWeightG =
      static_cast<float>(session.config.goalWeightG) * 0.5f;
  if (weight >= firstDropMaxWeightG) {
    session.firstDropConfirmations = 0;
    return;
  }
  if (retareWindowOpen() &&
      deltaFromBaseline >= session.config.minimumCupWeightG) {
    session.firstDropConfirmations = 0;
    return;
  }
  if (retareWindowOpen() &&
      deltaFromBaseline >= session.config.minimumCupWeightG * 0.5f) {
    session.firstDropConfirmations = 0;
    return;
  }
  if (retareWindowOpen()) {
    if (!session.retareFlowSampleValid) {
      session.retareFlowLastWeightG = weight;
      session.retareFlowSampleValid = true;
      session.firstDropConfirmations = 0;
      return;
    }
    const float step = weight - session.retareFlowLastWeightG;
    session.retareFlowLastWeightG = weight;
    if (step > session.config.retareStabilityToleranceG * 2.0f || step < 0.0f) {
      session.firstDropConfirmations = 0;
      return;
    }
    notifyRetareFlowDetected(receivedAtMs);
    session.firstDropConfirmations = 0;
    return;
  }

  const bool consecutive =
      session.firstDropConfirmations > 0 &&
      packetSequence == session.firstDropLastPacketSequence + 1U &&
      static_cast<int32_t>(receivedAtMs - session.firstDropLastAtMs) >= 0 &&
      static_cast<uint32_t>(receivedAtMs - session.firstDropLastAtMs) <=
          DIRECT_STOP_CONFIRMATION_WINDOW_MS;
  session.firstDropConfirmations =
      consecutive ? static_cast<uint8_t>(session.firstDropConfirmations + 1U)
                  : 1U;
  session.firstDropLastAtMs = receivedAtMs;
  session.firstDropLastPacketSequence = packetSequence;

  if (session.firstDropConfirmations >= FIRST_DROP_CONFIRMATION_SAMPLES) {
    onFirstDropsDetected(receivedAtMs);
  }
}

void schedulePendingShotFinalize(EndReason reason, uint32_t durationMs) {
  const bool logEligible = shotLogEligible(reason, durationMs);
  const bool offsetAnalysis = shot.confirmedBrew && !session.config.timerOnly &&
                              session.calibrationEligible;
  if (!logEligible && !offsetAnalysis) {
    return;
  }

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = offsetAnalysis;
  pendingFinalize.logEligible = logEligible;
  pendingFinalize.endedAtMs = millis();
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.cycleStartedAtMs = session.startedAtMs;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs =
      static_cast<uint16_t>(durationMs / 100U);
  if (session.firstDropMs != 0 &&
      static_cast<int32_t>(session.firstDropMs - session.startedAtMs) >= 0) {
    pendingFinalize.firstDropDs = static_cast<uint16_t>(
        (session.firstDropMs - session.startedAtMs) / 100U);
  }
  pendingFinalize.goalWeightG = session.config.goalWeightG;
  pendingFinalize.weightOffsetG = session.config.weightOffsetG;
  pendingFinalize.scaleBaselineG =
      session.scaleBaselineReady ? session.scaleBaselineG : 0.0f;
  pendingFinalize.startedWithScale = session.startedWithScale;
  pendingFinalize.timerOnly = session.config.timerOnly;
  pendingFinalize.confirmedBrew = shot.confirmedBrew;
  pendingFinalize.finalState = stopperState;
  pendingFinalize.endReason = reason;
  pendingFinalize.extractionGuardEnabled =
      session.config.fastExtractionGuardEnabled;
  pendingFinalize.extractionExtended = session.extractionExtended;
  pendingFinalize.targetReachedEarly = session.targetReachedEarly;
  pendingFinalize.maxRecoveryWeightG = session.config.maxRecoveryWeightG;
  pendingFinalize.minBrewTimeMs = session.config.minBrewTimeMs;
  if (session.targetReachedAtMs != 0 &&
      static_cast<int32_t>(session.targetReachedAtMs - session.startedAtMs) >=
          0) {
    pendingFinalize.targetReachedEarlyDs = static_cast<uint16_t>(
        (session.targetReachedAtMs - session.startedAtMs) / 100U);
  }
  pendingFinalize.lastKnownWeightValid =
      session.hasWeightAnchor && isfinite(session.lastAcceptedWeightG);
  pendingFinalize.lastKnownWeightG = session.lastAcceptedWeightG;
}

void commitPendingShotLog(const PendingShotFinalize &snapshot, float finalWeightG,
                          bool finalWeightValid,
                          ActualWeightSource weightSource) {
  ShotLogRecord record;
  memset(&record, 0, sizeof(record));
  record.actualWeightCg = SHOT_LOG_WEIGHT_MISSING;
  record.errorCg = SHOT_LOG_WEIGHT_MISSING;
  record.firstDropDs = snapshot.firstDropDs;
  record.avgFlowCgS = SHOT_LOG_METRIC_MISSING;
  record.bootId = snapshot.bootId;
  record.endedAtMs = snapshot.endedAtMs;
  if (g_wallClock.synced()) {
    const uint32_t utcSec = g_wallClock.nowUtcSec(millis());
    const int16_t offset = runtimeConfig.timezoneOffsetMinutes;
    record.endedAtUnixSec = utcSec;
    record.endedAtLocalSec = shotLogLocalSecFromUtc(utcSec, offset);
    record.timezoneOffsetMinutesAtCommit = offset;
    record.hasWallTime = 1;
  } else {
    record.endedAtUnixSec = 0;
    record.endedAtLocalSec = 0;
    record.timezoneOffsetMinutesAtCommit = 0;
    record.hasWallTime = 0;
  }
  record.durationDs = snapshot.durationDs;
  record.goalWeightG = snapshot.goalWeightG;
  record.offsetUsedCg = shotLogWeightToCentigrams(snapshot.weightOffsetG);
  record.shotType = static_cast<uint8_t>(shotLogTypeFromCycle(
      snapshot.finalState, snapshot.startedWithScale, snapshot.timerOnly,
      snapshot.confirmedBrew));
  record.cutType = static_cast<uint8_t>(
      shotLogCutFromEndReason(snapshot.endReason));
  record.extractionGuardEnabled = snapshot.extractionGuardEnabled ? 1U : 0U;
  record.extractionExtended = snapshot.extractionExtended ? 1U : 0U;
  record.stopDetail = static_cast<uint8_t>(shotLogStopDetailFromEndReason(
      snapshot.endReason, snapshot.extractionGuardEnabled,
      snapshot.extractionExtended));
  if (snapshot.extractionGuardEnabled) {
    record.maxRecoveryWeightCg =
        shotLogWeightToCentigrams(snapshot.maxRecoveryWeightG);
    record.minBrewTimeDs =
        static_cast<uint16_t>(snapshot.minBrewTimeMs / 100U);
  } else {
    record.maxRecoveryWeightCg = SHOT_LOG_WEIGHT_MISSING;
    record.minBrewTimeDs = SHOT_LOG_METRIC_MISSING;
  }
  record.targetReachedEarlyDs = snapshot.targetReachedEarlyDs;
  record.actualWeightSource = static_cast<uint8_t>(weightSource);

  if (finalWeightValid) {
    record.actualWeightCg = shotLogWeightToCentigrams(finalWeightG);
    record.errorCg =
        shotLogWeightToCentigrams(finalWeightG -
                           static_cast<float>(snapshot.goalWeightG));
    if (snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING &&
        weightSource == ActualWeightSource::POST_DRIP) {
      const float durationS = snapshot.durationDs / 10.0f;
      const float firstDropS = snapshot.firstDropDs / 10.0f;
      const float brewS = durationS - firstDropS;
      const float deltaG = finalWeightG - snapshot.scaleBaselineG;
      if (brewS > 0.5f && deltaG > 0.0f) {
        const float flowGs = deltaG / brewS;
        if (isfinite(flowGs) && flowGs >= 0.0f && flowGs <= 655.35f) {
          record.avgFlowCgS =
              static_cast<uint16_t>(flowGs * 100.0f + 0.5f);
        }
      }
    }
  }

  if (!shotLog.append(record)) {
    Serial.println("Shot log persist failed");
  }
}

void maybeQueueAutoToManualGuardSample(const PendingShotFinalize &snapshot,
                                       float finalWeightG,
                                       bool postDripWeightValid) {
  if (!postDripWeightValid || snapshot.timerOnly ||
      snapshot.extractionExtended ||
      snapshot.endReason == EndReason::AUTO_TO_MANUAL_GUARD ||
      snapshot.endReason == EndReason::RINSE_COMPLETE ||
      snapshot.endReason == EndReason::SHORT_SHOT ||
      snapshot.durationDs < (MIN_SHOT_LOG_DURATION_MS / 100U) ||
      !autoToManualGuardSampleErrorOk(finalWeightG, snapshot.goalWeightG)) {
    return;
  }
  RuntimeConfig candidate =
      runtimePersistPending ? runtimePersistCandidate : runtimeConfig;
  pushAutoToManualGuardSample(candidate.autoToManualGuardSamplesDs,
                              snapshot.durationDs);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  runtimePersistCandidate = candidate;
  runtimePersistPending = true;
  runtimePersistRetryAtMs = millis();
  Serial.print("A->M guard sample queued; trend ms=");
  Serial.println(autoToManualGuardTrendMs(
      candidate.autoToManualGuardSamplesDs, candidate.operationalWallMs));
}

void pendingShotFinalizeTask() {
  if (!pendingFinalize.pending ||
      elapsedMs(pendingFinalize.endedAtMs) < DRIP_DELAY_MS) {
    return;
  }

  const PendingShotFinalize snapshot = pendingFinalize;
  pendingFinalize.pending = false;

  float finalWeightG = 0.0f;
  bool postDripWeightValid = false;
  if (scaleAvailable() && isfinite(currentWeight) &&
      currentWeightSequence != snapshot.endedWeightSequence &&
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) >
          0) {
    finalWeightG = currentWeight;
    postDripWeightValid = snapshot.startedWithScale;
  }

  ActualWeightSource weightSource = ActualWeightSource::NONE;
  bool logWeightValid = false;
  float logWeightG = 0.0f;
  if (postDripWeightValid) {
    weightSource = ActualWeightSource::POST_DRIP;
    logWeightValid = true;
    logWeightG = finalWeightG;
  } else if (snapshot.lastKnownWeightValid) {
    weightSource = ActualWeightSource::LAST_KNOWN;
    logWeightValid = true;
    logWeightG = snapshot.lastKnownWeightG;
  }

  if (snapshot.logEligible) {
    commitPendingShotLog(snapshot, logWeightG, logWeightValid, weightSource);
  }

  maybeQueueAutoToManualGuardSample(snapshot, finalWeightG, postDripWeightValid);

  if (!snapshot.offsetAnalysis) {
    return;
  }

  if (!scaleAvailable() || !isfinite(currentWeight) ||
      !isfinite(snapshot.weightOffsetG) ||
      currentWeightSequence == snapshot.endedWeightSequence ||
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) <=
          0 ||
      currentWeight <
          (snapshot.goalWeightG - snapshot.weightOffsetG)) {
    Serial.println("Final weight unavailable or too low; offset unchanged");
    return;
  }

  finalWeightG = currentWeight;

  Serial.print("Final weight: ");
  Serial.print(finalWeightG);
  Serial.print("g; cycle goal: ");
  Serial.print(snapshot.goalWeightG);
  Serial.print("g; cycle offset: ");
  Serial.print(snapshot.weightOffsetG);
  Serial.println("g");

  const float observedError =
      finalWeightG - snapshot.goalWeightG + snapshot.weightOffsetG;
  if (fabsf(observedError) > MAX_OFFSET_G) {
    Serial.println("Shot error too large; offset unchanged");
    return;
  }

  const float updatedOffset =
      snapshot.weightOffsetG + finalWeightG - snapshot.goalWeightG;
  if (!isfinite(updatedOffset) || updatedOffset < 0.0f ||
      updatedOffset > MAX_OFFSET_G) {
    Serial.println("Calculated offset outside safe range; offset unchanged");
    return;
  }

  RuntimeConfig candidate =
      runtimePersistPending ? runtimePersistCandidate : runtimeConfig;
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
  strncpy(scaleProtocolName, scale.connectedProtocolName(),
          sizeof(scaleProtocolName) - 1);
  scaleProtocolName[sizeof(scaleProtocolName) - 1] = '\0';
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

void executeScaleTareCommand(const ScaleCommand &command) {
  ScaleEvent event;
  event.type = ScaleEventType::TARE_RESULT;
  event.cycleId = command.cycleId;

  if (scale.isConnected()) {
    event.commandAttempted = true;
    event.writeSucceeded = scale.tare();
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

bool shotCompletionGetsDoubleBeep(EndReason reason) {
  switch (reason) {
    case EndReason::PADDLE:
    case EndReason::SCALE_PREDICTION:
    case EndReason::SCALE_THRESHOLD:
    case EndReason::WEIGHT_ANOMALY:
    case EndReason::GLOBAL_LIMIT:
    case EndReason::CONFIGURED_WALL_LIMIT:
    case EndReason::WEB_STOP:
    case EndReason::PHYSICAL_OVERRIDE:
    case EndReason::WEB_HEARTBEAT_TIMEOUT:
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
    case EndReason::FAST_EXTRACTION_MIN_TIME:
    case EndReason::AUTO_TO_MANUAL_GUARD:
      return true;
    default:
      return false;
  }
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

void scheduleScaleCompletionBeep() {
  scaleCompletionBeepScheduled = true;
  scaleCompletionBeepDueAtMs = millis() + SCALE_COMPLETION_BEEP_DELAY_MS;
}

void cancelScaleCompletionBeep() {
  scaleCompletionBeepScheduled = false;
  portENTER_CRITICAL(&scaleBeepMux);
  scaleCompletionBeepPending = false;
  portEXIT_CRITICAL(&scaleBeepMux);
}

void serviceScaleCompletionBeep() {
  if (!scaleCompletionBeepScheduled) {
    return;
  }
  if (static_cast<int32_t>(millis() - scaleCompletionBeepDueAtMs) < 0) {
    return;
  }
  scaleCompletionBeepScheduled = false;
  requestScaleCompletionBeep();
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
    paddleReturnReminderStartedAtMs = 0;
    cancelScalePaddleReturnReminderBeep();
    return;
  }

  const uint32_t now = millis();
  if (!paddleReturnReminderActive) {
    paddleReturnReminderActive = true;
    paddleReturnReminderLastAtMs = now;
    paddleReturnReminderStartedAtMs = now;
    return;
  }
  if (elapsedMs(paddleReturnReminderStartedAtMs) >=
      runtimeConfig.paddleReturnReminderMaxDurationMs) {
    paddleReturnReminderActive = false;
    paddleReturnReminderLastAtMs = 0;
    paddleReturnReminderStartedAtMs = 0;
    cancelScalePaddleReturnReminderBeep();
    return;
  }
  if (elapsedMs(paddleReturnReminderLastAtMs) >=
      runtimeConfig.paddleReturnReminderIntervalMs) {
    paddleReturnReminderLastAtMs = now;
    requestScalePaddleReturnReminderBeep();
  }
}

void executeScaleCommand(const ScaleCommand &command) {
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
      } else if (takeScaleCompletionBeep()) {
        executeScaleBeepCommand(DebugCode::SCALE_BEEP_OK,
                                DebugCode::SCALE_BEEP_FAILED,
                                DebugCode::SCALE_BEEP_UNSUPPORTED);
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

      case ScaleEventType::TARE_RESULT:
        if (event.cycleId == session.id && event.writeSucceeded &&
            session.startedWithScale) {
          armPostTareBaselineWindow();
          session.scaleBaselineReady = false;
          session.scaleBaselineG = 0.0f;
          session.firstDropConfirmations = 0;
          session.firstDropLastAtMs = 0;
          session.firstDropLastPacketSequence = 0;
          resetDirectStopConfirmation();
        }
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

void maybeRequestNtpSyncOnActivity();

void beginCycle(ControlSource source = ControlSource::PHYSICAL,
                uint32_t webSessionId = 0,
                uint32_t controlLeaseId = 0) {
  if (pendingFinalize.pending) {
    pendingFinalize.pending = false;
    Serial.println("Previous drip analysis cancelled by a new cycle");
  }

  resetSessionForNewCycle(source, webSessionId, controlLeaseId);
  session.startedAtMs = millis();
  session.firstDropMs = 0;
  session.retareFlowFirstDetectedAtMs = 0;
  session.scaleBaselineReady = false;
  session.scaleBaselineG = 0.0f;
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
      session.scaleBaselineG = currentWeight;
      session.scaleBaselineReady = true;
    }
  }
  resetShotTrajectory(session.startedAtMs);
  initializeBbwProtection();
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
  maybeRequestNtpSyncOnActivity();
}

void finalizeCycle(EndReason reason, StopperState nextState) {
  const RelaySafetySnapshot relayBeforeOpen = getRelaySafetySnapshot();
  const uint32_t durationMs = elapsedMs(relayBeforeOpen.closedAtMs);

  // Physical flow always stops before the non-blocking BLE command is queued.
  setCn9Closed(false);
  cancelScaleBrewBeep(session.id);
  cancelScaleCompletionBeep();
  session.endReason = reason;
  requestRemoteTimerStop();
  if (shotCompletionGetsDoubleBeep(reason)) {
    scheduleScaleCompletionBeep();
  }

  schedulePendingShotFinalize(reason, durationMs);

  lastCycle.valid = true;
  lastCycle.cycleId = session.id;
  lastCycle.durationMs = durationMs;
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
  session.autoToManualGuardArmed = false;
  session.autoToManualGuardEnforced = false;
  Serial.println("Rinse classified; paddle changes ignored until completion");
  transitionTo(StopperState::RINSE);
  maybeRequestNtpSyncOnActivity();
}

void armAutoToManualGuardForConfirmedBrew() {
  if (!session.config.autoToManualGuardEnabled ||
      session.config.timerOnly || !session.startedWithScale ||
      session.weightControlState == WeightControlState::INACTIVE) {
    return;
  }
  const uint32_t limitMs = autoToManualGuardLimitMs(
      true,
      static_cast<AutoToManualGuardLimitMode>(
          session.config.autoToManualGuardLimitMode),
      session.config.autoToManualGuardManualLimitMs,
      runtimeConfig.autoToManualGuardSamplesDs,
      session.config.operationalWallMs);
  session.autoToManualGuardArmed = true;
  session.autoToManualGuardDeadlineAtMs = session.startedAtMs + limitMs;
  addDebugEvent(DebugCategory::SCALE, DebugCode::AUTO_TO_MANUAL_GUARD_ARMED,
                static_cast<int32_t>(limitMs));
  if (session.weightControlState == WeightControlState::SUSPENDED &&
      !session.autoToManualGuardEnforced) {
    session.autoToManualGuardEnforced = true;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::AUTO_TO_MANUAL_GUARD_ENFORCED,
                  static_cast<int32_t>(elapsedMs(session.startedAtMs)),
                  static_cast<int32_t>(limitMs));
  }
}

bool autoToManualGuardDeadlineDue() {
  if (!session.active || !session.autoToManualGuardEnforced) {
    return false;
  }
  return static_cast<int32_t>(millis() -
                              session.autoToManualGuardDeadlineAtMs) >= 0;
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
    armAutoToManualGuardForConfirmedBrew();
    Serial.println("Brew confirmed");
    transitionTo(StopperState::BREW);
  } else {
    session.automaticEnabled = false;
    session.retareEnded = true;
    session.brewStartConfirmEnded = true;
    Serial.println("Manual cycle confirmed (no scale automation)");
    transitionTo(StopperState::MANUAL_NO_SCALE);
  }
}

void handleQualifyingPaddleOff(uint32_t onDurationMs) {
  if (onDurationMs <= session.config.rinseGestureMs) {
    enterRinse();
    return;
  }

  if (session.automaticEnabled) {
    shot.confirmedBrew = true;
  }
  finalizeCycle(EndReason::PADDLE, StopperState::READY);
}

bool automaticScaleStopDue() {
  if (session.config.timerOnly || stopperState != StopperState::BREW ||
      bbwWeightStopInhibited()) {
    return false;
  }

  const bool directStopFresh = session.directStopPending &&
      session.thresholdConfirmations >= DIRECT_STOP_CONFIRMATION_SAMPLES &&
      static_cast<int32_t>(millis() - session.lastThresholdAtMs) >= 0 &&
      elapsedMs(session.lastThresholdAtMs) <= MAX_AUTOMATION_WEIGHT_AGE_MS;
  if (directStopFresh) {
    return true;
  }

  if (session.extractionExtended && fastExtractionGuardSession()) {
    if (minBrewTimeReached() &&
        targetWeightReached(session.lastAcceptedWeightG)) {
      session.directStopPending = true;
      session.directStopReason = EndReason::FAST_EXTRACTION_MIN_TIME;
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::FAST_EXTRACTION_STOP_MIN_TIME,
                    static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
      return true;
    }
    return false;
  }

  if (!session.receivedFreshWeightInCycle ||
      session.weightControlState != WeightControlState::ACTIVE ||
      currentWeightSequence == session.weightSequenceAtStart ||
      scaleAutomationUnavailableForSession()) {
    return false;
  }
  const float elapsedS = cycleElapsedSeconds();
  return elapsedS >= shot.expectedEndS;
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
      serviceBbwProtectionPhases();

      if (readyToConfirmBrew()) {
        confirmBrewOrManual();
      }
      return;

    case StopperState::RINSE:
      // All paddle transitions are intentionally consumed while rinsing.
      if (elapsedMs(session.rinseStartedAtMs) >=
          session.config.rinseDurationMs) {
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

      serviceBbwProtectionPhases();

      if ((session.weightControlState == WeightControlState::ACTIVE ||
           session.weightControlState == WeightControlState::VALIDATING) &&
          scaleAutomationUnavailableForSession()) {
        suspendWeightControl();
        weightStreamState = WeightStreamState::STALE;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_STREAM_STALE);
        Serial.println("Scale stream suspended during brew");
      }

      if (autoToManualGuardDeadlineDue()) {
        addDebugEvent(DebugCategory::SCALE,
                      DebugCode::AUTO_TO_MANUAL_GUARD_FIRED,
                      static_cast<int32_t>(elapsedMs(session.startedAtMs)));
        finalizeCycle(EndReason::AUTO_TO_MANUAL_GUARD,
                      StopperState::REQUIRES_OFF);
        return;
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

void maybeRequestNtpSyncOnActivity() {
#ifndef SHOT_STOPPER_HOST_TEST
  networkManager.requestNtpSyncIfNeeded();
#else
  if (wallClockNeedsActivityNtpSync(g_wallClock, hostMillis)) {
    ++hostNtpSyncRequestCount;
  }
#endif
}

bool controlAllowsConfigurationNow() {
  return stopperState == StopperState::READY && !session.active &&
         !getRelaySafetySnapshot().closed && !paddleOn && !rawPaddleOn &&
         !maintenanceLease.active;
}

void beginWebRinse(uint32_t webSessionId, uint32_t controlLeaseId) {
  if (pendingFinalize.pending) {
    pendingFinalize.pending = false;
    Serial.println("Previous drip analysis cancelled by a web rinse");
  }
  resetSessionForNewCycle(ControlSource::WEB, webSessionId, controlLeaseId);
  session.startedAtMs = millis();
  session.firstDropMs = 0;
  session.retareFlowFirstDetectedAtMs = 0;
  session.scaleBaselineReady = false;
  session.scaleBaselineG = 0.0f;
  session.weightSequenceAtStart = currentWeightSequence;
  session.startedWithScale = scaleAvailable();
  session.rinseStartedAtMs = session.startedAtMs;
  session.automaticEnabled = false;
  virtualPaddleOn = false;
  resetShotTrajectory(session.startedAtMs);
  if (!setCn9Closed(true, session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  if (session.startedWithScale &&
      !requestRemoteTimerStart()) {
    session.startedWithScale = false;
    session.automaticEnabled = false;
    session.scaleWasLost = true;
    Serial.println("Scale start command unavailable; web rinse marked manual");
  }
  transitionTo(StopperState::RINSE);
  maybeRequestNtpSyncOnActivity();
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
      // Offset and A→M duration samples are learned by the control loop and
      // are intentionally not Web fields. Preserve the newest values if a
      // form built from an older status snapshot races with post-shot work.
      candidate.weightOffsetG = runtimeConfig.weightOffsetG;
      memcpy(candidate.autoToManualGuardSamplesDs,
             runtimeConfig.autoToManualGuardSamplesDs,
             sizeof(candidate.autoToManualGuardSamplesDs));
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
      pendingFinalize.pending = false;
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

    case WebCommandType::RESET_AUTO_TO_MANUAL_GUARD_SAMPLES: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      pendingFinalize.pending = false;
      RuntimeConfig candidate = runtimeConfig;
      resetAutoToManualGuardSamples(candidate.autoToManualGuardSamplesDs);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      addDebugEvent(DebugCategory::CONFIG,
                    DebugCode::AUTO_TO_MANUAL_GUARD_SAMPLES_RESET);
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

    case WebCommandType::CLEAR_SHOT_LOG:
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
  next.bootId = shotLog.bootId();
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
  next.uptimeMs = elapsedMs(bootStartedAtMs);
  next.loopMaxGapMs = loopMaxGapMs;
  next.loopStackMinWords = loopStackMinWords;
  next.scaleStackMinWords = scaleWorkerStackMinWords;
  next.freeHeapBytes = freeHeapBytes;
  next.minimumFreeHeapBytes = minimumFreeHeapBytes;
  next.largestFreeHeapBlockBytes = largestFreeHeapBlockBytes;
  next.hwmon = hwmonSnapshot;
  next.scaleEventsDropped = scaleEventsDropped;
  next.config = runtimeConfig;
  next.lastCycle = lastCycle;
  strncpy(next.scaleProtocol, scaleLink.protocolName,
          sizeof(next.scaleProtocol) - 1);
  next.scaleProtocol[sizeof(next.scaleProtocol) - 1] = '\0';
  if (session.active) {
    next.cycleFlowDuringRetare = session.flowDuringRetare;
    next.cycleRetarePerformed = session.retarePerformed;
    next.cycleStartedWithScale = session.startedWithScale;
    next.cycleConfirmedBrew = shot.confirmedBrew;
    next.cycleTimerOnly = session.config.timerOnly;
    next.cycleFirstDropMs = session.firstDropMs;
    next.cycleRetareFlowFirstDetectedAtMs =
        session.retareFlowFirstDetectedAtMs;
    next.cycleStartedAtMs = session.startedAtMs;
    next.cycleElapsedMs = elapsedMs(session.startedAtMs);
    next.cycleExtractionExtended =
        session.extractionExtended && fastExtractionGuardSession();
    next.cycleTargetReachedEarly = session.targetReachedEarly;
    if (next.cycleExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.maxRecoveryWeightG;
      const uint32_t elapsed = next.cycleElapsedMs;
      next.cycleMinBrewTimeRemainingMs =
          elapsed >= session.config.minBrewTimeMs
              ? 0U
              : session.config.minBrewTimeMs - elapsed;
    } else {
      next.cycleActiveStopWeightG =
          static_cast<float>(session.config.goalWeightG);
      next.cycleMinBrewTimeRemainingMs = 0;
    }
    next.cycleAutoToManualGuardArmed = session.autoToManualGuardArmed;
    next.cycleAutoToManualGuardEnforced = session.autoToManualGuardEnforced;
    if (session.autoToManualGuardEnforced) {
      const uint32_t nowMs = millis();
      next.cycleAutoToManualGuardRemainingMs =
          static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs -
                               nowMs) <= 0
              ? 0U
              : session.autoToManualGuardDeadlineAtMs - nowMs;
    }
  }
  next.autoToManualGuardTrendMs = autoToManualGuardTrendMs(
      runtimeConfig.autoToManualGuardSamplesDs,
      runtimeConfig.operationalWallMs);
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
  bootStartedAtMs = millis();
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
  Serial.print("Shot Stopper Micra ");
  Serial.println(FW_VERSION);
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
  bool settingsLoaded = false;
  bool configMigrated = false;
  if (persistenceReady &&
      loadPersistedSettings(persistedSettings, &configMigrated)) {
    settingsLoaded = true;
    if (configMigrated) {
      if (savePersistedSettings(persistedSettings)) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_MIGRATED);
      } else {
        Serial.println("WARN: settings save failed");
        addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                      4);
      }
    }
  } else if (persistenceReady) {
    if (initializeDefaultSettings(persistedSettings)) {
      settingsLoaded = true;
      if (!savePersistedSettings(persistedSettings)) {
        Serial.println("WARN: settings save failed");
      }
    }
  }
  if (settingsLoaded) {
    runtimeConfig = persistedSettings.runtime;
  }
#else
  runtimeConfig = RuntimeConfig{};
#endif

#ifndef SHOT_STOPPER_HOST_TEST
  shotLog.load();
  shotLog.onBoot();
  shotLog.save();
#else
  shotLog.load();
  shotLog.onBoot();
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

  hwmonSnapshot = hwmon.sample(1);
  publishControlStatus();
#ifndef SHOT_STOPPER_HOST_TEST
  if (settingsLoaded && webCommandQueue != nullptr) {
    NetworkBridgeCallbacks callbacks;
    callbacks.copyControlStatus = copyControlStatus;
    callbacks.enqueueWebCommand = enqueueWebCommand;
    callbacks.copyDebugEvents = copyDebugEvents;
    callbacks.addDebugEvent = addDebugEvent;
    callbacks.reportTaskWatchdogFault = reportTaskWatchdogFault;
    callbacks.requestSafeRestart = requestSafeRestart;
    callbacks.copyShotRecords = copyShotRecords;
    callbacks.deleteShotRecord = deleteShotRecord;
    callbacks.clearShotLog = clearShotLog;
    if (!networkManager.begin(persistedSettings, callbacks)) {
      Serial.println("Network manager unavailable; stopper remains local");
    }
  } else if (!settingsLoaded) {
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
    const uint32_t intervalMs = elapsedMs(healthTelemetryAtMs);
    healthTelemetryAtMs = loopStartedAtMs;
    loopStackMinWords =
        static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
#ifndef SHOT_STOPPER_HOST_TEST
    freeHeapBytes = ESP.getFreeHeap();
    minimumFreeHeapBytes = ESP.getMinFreeHeap();
    largestFreeHeapBlockBytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
    hwmonSnapshot = hwmon.sample(intervalMs > 0U ? intervalMs
                                                 : HEALTH_TELEMETRY_INTERVAL_MS);
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
  serviceScaleCompletionBeep();
  serviceRemoteTimerStopRetry();
  pendingShotFinalizeTask();
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
  hwmon.noteLoopBusyMs(elapsedMs(loopStartedAtMs));
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
