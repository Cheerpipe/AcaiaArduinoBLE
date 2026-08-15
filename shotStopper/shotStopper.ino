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
#if SHOT_STOPPER_ENABLE_ALED == 1
#include <esp32-hal-rgb-led.h>
#endif
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <math.h>
#include "ShotStopperNetwork.h"
#include "ShotStopperPersistence.h"
#if __has_include(<esp_coexist.h>)
#include <esp_coexist.h>
#define SHOT_STOPPER_HAS_COEX 1
#endif
#endif

#include "ShotStopperDomain.h"
#include "ShotStopperBuzzer.h"
#include "ShotStopperPresets.h"
#include "ShotStopperSerialCli.h"
#include "ShotStopperVersion.h"
#include "ShotStopperHardwareTimer.h"
#if SHOT_STOPPER_ENABLE_ALED == 1
#include "ShotStopperIndicators.h"
#endif
#include "ShotStopperResetGuard.h"
#include "ShotStopperSafety.h"
#include "ShotStopperShotLog.h"
#include "ShotStopperLastShot.h"
#include "ShotStopperTime.h"
#include "ShotStopperWatchdog.h"
#include "ShotStopperHwmon.h"

#include <stdarg.h>
#include <stdio.h>

using namespace shotstopper;

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

constexpr uint32_t PADDLE_DEBOUNCE_MS = 30;
constexpr uint32_t DRIP_DELAY_MS = 3000;
constexpr uint32_t SCALE_CONNECT_RETRY_MS = 1000;
constexpr uint32_t SCALE_CONNECT_RETRY_MAX_MS = 10000;
constexpr uint32_t SCALE_CONNECT_LOG_MS = 10000;
constexpr uint32_t SCALE_DISCOVERY_TICK_MS = 3000;
constexpr uint32_t SCALE_SCAN_HCI_RESTART_MS = 60000;
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
constexpr uint32_t RUNTIME_PERSIST_DEBOUNCE_MS = 300;
constexpr uint32_t SETTINGS_PERSIST_IDLE_WAIT_MS = 1000;
constexpr uint32_t HEALTH_TELEMETRY_INTERVAL_MS = 5000;
#if SHOT_STOPPER_ENABLE_ALED == 1
constexpr uint32_t STATUS_INDICATOR_TASK_STACK_SIZE = 3072;
#endif
// Pin control/BLE/LED work with Arduino loopTask on APP_CPU (core 1).
// network_manager is pinned to PRO_CPU (core 0) in ShotStopperNetwork.cpp.
constexpr BaseType_t CONTROL_TASK_CORE = 1;

#if SHOT_STOPPER_ENABLE_ALED == 1
#ifndef SHOT_STOPPER_LED_BRIGHTNESS
#define SHOT_STOPPER_LED_BRIGHTNESS 32
#endif

constexpr uint8_t STATUS_INDICATOR_BRIGHTNESS =
    SHOT_STOPPER_LED_BRIGHTNESS;
#endif

constexpr bool DEBUG = false;

// ---------------------------------------------------------------------------
// Board hardware
// ---------------------------------------------------------------------------

#if defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t PADDLE_GPIO = 21;
constexpr uint8_t RELAY_GPIO = 38;
#if SHOT_STOPPER_ENABLE_ALED == 1
#ifndef SHOT_STOPPER_SCALE_LED_GPIO
#define SHOT_STOPPER_SCALE_LED_GPIO 48
#endif
#ifndef SHOT_STOPPER_STOPPER_LED_GPIO
#define SHOT_STOPPER_STOPPER_LED_GPIO 47
#endif
#endif
#ifndef SHOT_STOPPER_BUZZER_GPIO
#define SHOT_STOPPER_BUZZER_GPIO 14
#endif
#elif defined(ARDUINO_NANO_ESP32)
constexpr uint8_t PADDLE_GPIO = 10;
constexpr uint8_t RELAY_GPIO = 11;
#if SHOT_STOPPER_ENABLE_ALED == 1
#ifndef SHOT_STOPPER_SCALE_LED_GPIO
#define SHOT_STOPPER_SCALE_LED_GPIO 2
#endif
#ifndef SHOT_STOPPER_STOPPER_LED_GPIO
#define SHOT_STOPPER_STOPPER_LED_GPIO 3
#endif
#endif
#ifndef SHOT_STOPPER_BUZZER_GPIO
#define SHOT_STOPPER_BUZZER_GPIO 5
#endif
#elif defined(ARDUINO_ESP32_DEV)
// GPIO 27 supports INPUT_PULLUP for the paddle; GPIO 26 drives the relay.
constexpr uint8_t PADDLE_GPIO = 27;
constexpr uint8_t RELAY_GPIO = 26;
#if SHOT_STOPPER_ENABLE_ALED == 1
#ifndef SHOT_STOPPER_SCALE_LED_GPIO
#define SHOT_STOPPER_SCALE_LED_GPIO 25
#endif
#ifndef SHOT_STOPPER_STOPPER_LED_GPIO
#define SHOT_STOPPER_STOPPER_LED_GPIO 33
#endif
#endif
#ifndef SHOT_STOPPER_BUZZER_GPIO
#define SHOT_STOPPER_BUZZER_GPIO 32
#endif
#else
#error "Unsupported board: configure explicit GPIO pins"
#endif

#if SHOT_STOPPER_ENABLE_ALED == 1
constexpr uint8_t SCALE_STATUS_LED_GPIO = SHOT_STOPPER_SCALE_LED_GPIO;
constexpr uint8_t STOPPER_STATUS_LED_GPIO =
    SHOT_STOPPER_STOPPER_LED_GPIO;
#endif
constexpr uint8_t BUZZER_GPIO = SHOT_STOPPER_BUZZER_GPIO;

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
#if SHOT_STOPPER_ENABLE_ALED == 1
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
static_assert(BUZZER_GPIO != PADDLE_GPIO && BUZZER_GPIO != RELAY_GPIO &&
                  BUZZER_GPIO != SCALE_STATUS_LED_GPIO &&
                  BUZZER_GPIO != STOPPER_STATUS_LED_GPIO,
              "Buzzer GPIO must be distinct from paddle, relay, and LED GPIOs");
#else
static_assert(BUZZER_GPIO != PADDLE_GPIO && BUZZER_GPIO != RELAY_GPIO,
              "Buzzer GPIO must be distinct from paddle and relay GPIOs");
#endif
#ifndef SHOT_STOPPER_HOST_TEST
#if SHOT_STOPPER_ENABLE_ALED == 1
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(SCALE_STATUS_LED_GPIO),
              "Scale WS2812B must use a valid output-capable GPIO");
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(STOPPER_STATUS_LED_GPIO),
              "Stopper WS2812B must use a valid output-capable GPIO");
#endif
static_assert(!BUZZER_SUPPORT_ENABLED ||
                  GPIO_IS_VALID_OUTPUT_GPIO(BUZZER_GPIO),
              "Buzzer must use a valid output-capable GPIO");
#endif
#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO)
static_assert(SAFETY_HEARTBEAT_GPIO != RELAY_GPIO &&
                  SAFETY_HEARTBEAT_GPIO != PADDLE_GPIO &&
                  CN9_FEEDBACK_GPIO != RELAY_GPIO &&
                  CN9_FEEDBACK_GPIO != PADDLE_GPIO &&
                  CN9_FEEDBACK_GPIO != SAFETY_HEARTBEAT_GPIO,
              "Safety GPIOs must be unique");
#if SHOT_STOPPER_ENABLE_ALED == 1
static_assert(SAFETY_HEARTBEAT_GPIO != SCALE_STATUS_LED_GPIO &&
                  SAFETY_HEARTBEAT_GPIO != STOPPER_STATUS_LED_GPIO &&
                  CN9_FEEDBACK_GPIO != SCALE_STATUS_LED_GPIO &&
                  CN9_FEEDBACK_GPIO != STOPPER_STATUS_LED_GPIO,
              "Safety GPIOs must not share a WS2812B data pin");
#endif
static_assert(BUZZER_GPIO != SAFETY_HEARTBEAT_GPIO &&
                  BUZZER_GPIO != CN9_FEEDBACK_GPIO,
              "Buzzer GPIO must be distinct from safety GPIOs");
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
constexpr size_t TREND_POINT_COUNT = WEIGHT_TREND_POINT_COUNT;
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

enum class AlertEvent : uint8_t {
  TARE = 0,
  START_TIMER,
  STOP_TIMER,
  TARE_START,
  FIRST_DROP,
  PADDLE_REMINDER,
  COMPLETION_EXTRA,
  SCALE_LOST,
  ATM_END,
  MANUAL_NO_SCALE,
  EXTENDED_PULSE,
  SCALE_CONNECTED
};

bool startExtendedPulseTrain(uint32_t durationMs);
bool startPulseTrain(BuzzerPattern pattern, uint32_t durationMs);
bool emitAlert(AlertEvent event, uint32_t cycleId = 0);
bool commandAlertUsesBuzzer();
void emitImmediateCommandAlertIfBuzzer();
void emitCommandAlert(AlertEvent event, bool commandAttempted,
                      bool writeSucceeded);

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
  bool automaticBrew = false;
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
  uint32_t cn9ClosedAtMs = 0;
  bool scaleStartLagCaptured = false;
  uint32_t scaleStartLagMs = 0;
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
  bool bbwProtectionEnded = false;
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
  bool slowExtractionExtended = false;
  bool targetReachedEarly = false;
  uint32_t targetReachedAtMs = 0;
  bool autoToManualGuardArmed = false;
  bool autoToManualGuardEnforced = false;
  uint32_t autoToManualGuardDeadlineAtMs = 0;
  uint8_t activePresetId = 0;
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
  bool automaticBrew = false;
  StopperState finalState = StopperState::READY;
  EndReason endReason = EndReason::NONE;
  bool extractionGuardEnabled = false;
  bool extractionExtended = false;
  bool slowExtractionGuardEnabled = false;
  bool slowExtractionExtended = false;
  bool targetReachedEarly = false;
  uint16_t targetReachedEarlyDs = SHOT_LOG_METRIC_MISSING;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
  bool lastKnownWeightValid = false;
  float lastKnownWeightG = 0.0f;
  uint8_t activePresetId = 0;
  uint32_t cycleId = 0;
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
  bool usedCombinedTareStart = false;
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
LocalBuzzer localBuzzer;
ShotPresetBank presetBank;
LastCycleSummary lastCycle;
DebugRingBuffer debugLog;
LogLevel serialLogLevel = LogLevel::NONE;
LogLevel ringRetainLogLevel = LogLevel::NONE;
uint32_t lastReportedLogOverwritten = 0;
ShotLog shotLog;
LastShotStore lastShotStore;
PersistedLastShot persistedLastShot;

bool noScaleShotGuardArmed = true;
uint32_t noScaleShotGuardActivityAtMs = 0;
bool noScaleShotGuardScaleWasAvailable = false;
bool noScaleShotGuardHold = false;
uint32_t noScaleShotGuardHoldAtMs = 0;

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
portMUX_TYPE scalePreferredMacMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleBeepMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleDebugMux = portMUX_INITIALIZER_UNLOCKED;
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
bool scaleTimerValid = false;
uint32_t scaleTimerMs = 0;
uint32_t scaleTimerAgeMs = 0;
char scaleProtocolName[20] = "none";
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
bool hostRuntimePersistSucceeds = true;
uint32_t hostRuntimePersistAttempts = 0;
RuntimeConfig hostLastFlushedRuntime;
ShotPresetBank hostLastFlushedPresets;
bool hostLastFlushIncludedLive = false;
#endif
bool runtimePersistPending = false;
bool runtimePersistFailed = false;
RuntimeConfig runtimePersistCandidate;
uint32_t runtimePersistRequestId = 0;
uint32_t runtimePersistRetryAtMs = 0;
int32_t runtimePersistReasonBits = 0;
uint32_t nextInternalRequestId = 0x80000000UL;
#ifndef SHOT_STOPPER_HOST_TEST
struct SettingsPersistRequest {
  PersistedSettings blob;
  uint32_t runtimeRevision = 0;
};
QueueHandle_t settingsPersistQueue = nullptr;
TaskHandle_t settingsPersistTaskHandle = nullptr;
portMUX_TYPE settingsPersistMux = portMUX_INITIALIZER_UNLOCKED;
bool settingsPersistInFlight = false;
bool settingsPersistResultReady = false;
bool settingsPersistResultOk = false;
uint32_t settingsPersistResultRuntimeRevision = 0;
uint32_t settingsPersistResultStorageRevision = 0;
#endif
uint32_t lastLoopAtMs = 0;
uint32_t loopMaxGapMs = 0;
uint32_t healthIntervalMaxGapMs = 0;
uint32_t loopStackMinWords = 0;
uint32_t healthTelemetryAtMs = 0;
uint32_t freeHeapBytes = 0;
uint32_t minimumFreeHeapBytes = 0;
uint32_t largestFreeHeapBlockBytes = 0;
bool healthHeapAlertLatched = false;
bool healthStackAlertLatched = false;
bool healthLoopGapAlertLatched = false;
Hwmon hwmon;
HwmonSnapshot hwmonSnapshot = {};
bool platformClockReady = false;
bool persistenceReady = false;
bool bleStackReady = false;
bool firmwareInitializationComplete = false;

bool beginMaintenanceLease(const WebCommand &networkCommand,
                           bool applyRuntimeOnSuccess);
void completeMaintenanceLease(const WebCommand &result);
void queueRuntimePersist(int32_t reasonBits);
void commitLiveRuntimeConfig(const RuntimeConfig &composed, int32_t reasonBits);

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
struct PendingScaleTimerStop {
  bool pending = false;
  uint32_t dueAtMs = 0;
};
PendingScaleTimerStop pendingScaleTimerStop;
uint32_t operationalLimitAtArmMs = HARD_MAX_CN9_CLOSED_MS;
RelaySafetyState relaySafetyState = RelaySafetyState::BOOT_SAFE;
RelaySafetyFault relaySafetyFault = RelaySafetyFault::NONE;
uint32_t relaySafetyGeneration = 0;
bool relaySafetyTimersReady = false;
bool taskWatchdogReady = false;
volatile bool criticalTaskWatchdogFault = false;
volatile bool feedbackTransitionPending = false;
volatile bool feedbackExpectedClosed = false;
volatile uint32_t feedbackTransitionStartedAtMs = 0;
volatile bool feedbackTransitionStampPending = false;
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
  bool timerValid;
  uint32_t timerMs;
  uint32_t timerAgeMs;
  char protocolName[20];
};

#if SHOT_STOPPER_ENABLE_ALED == 1
struct StatusIndicatorFrame {
  IndicatorColor scale;
  IndicatorColor stopper;
};

QueueHandle_t statusIndicatorQueue = nullptr;
TaskHandle_t statusIndicatorTaskHandle = nullptr;
bool statusIndicatorsReady = false;
StatusIndicatorFrame lastPublishedIndicatorFrame;
bool indicatorFramePublished = false;
#endif

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

uint32_t elapsedMs(uint32_t sinceMs) {
  return static_cast<uint32_t>(millis() - sinceMs);
}

void formatDebugEventMessage(const DebugEvent &event, char *message,
                             size_t capacity) {
  if (message == nullptr || capacity == 0) {
    return;
  }
  if (event.code == DebugCode::BOOT_BANNER) {
    snprintf(message, capacity, "Shot Stopper Micra %s (bootId=%ld)",
             FW_VERSION, static_cast<long>(event.argument1));
    return;
  }
  if (event.code == DebugCode::STATE_TRANSITION &&
      event.argument1 >= static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
      event.argument1 <= static_cast<int32_t>(StopperState::MANUAL_NO_SCALE) &&
      event.argument2 >= static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
      event.argument2 <= static_cast<int32_t>(StopperState::MANUAL_NO_SCALE)) {
    snprintf(message, capacity, "%s -> %s",
             stopperStateName(static_cast<StopperState>(event.argument1)),
             stopperStateName(static_cast<StopperState>(event.argument2)));
    return;
  }
  if (formatScaleSampleDebugMessage(event, message, capacity)) {
    return;
  }
  if (formatPersistDebugMessage(event, message, capacity)) {
    return;
  }
  if (formatLifecycleDebugMessage(event, message, capacity)) {
    return;
  }
  if ((event.code == DebugCode::WEB_COMMAND_ACCEPTED ||
       event.code == DebugCode::WEB_COMMAND_REJECTED) &&
      event.argument1 >= static_cast<int32_t>(WebCommandType::PADDLE_ON) &&
      event.argument1 <=
          static_cast<int32_t>(WebCommandType::MAINTENANCE_COMPLETE)) {
    snprintf(message, capacity, "%s: %s", debugCodeName(event.code),
             webCommandTypeName(
                 static_cast<WebCommandType>(event.argument1)));
    return;
  }
  strncpy(message, debugCodeName(event.code), capacity - 1);
  message[capacity - 1] = '\0';
}

void writeSerialLogLine(const DebugEvent &event) {
  char message[128] = {};
  formatDebugEventMessage(event, message, sizeof(message));
  char line[192] = {};
  const uint32_t wholeSec = event.atMs / 1000U;
  const uint32_t fracMs = event.atMs % 1000U;
  snprintf(line, sizeof(line), "%c (%lu.%03lu)[%s] %s",
           logLevelLetter(event.level),
           static_cast<unsigned long>(wholeSec),
           static_cast<unsigned long>(fracMs),
           debugCategoryName(event.category), message);
  Serial.println(line);
}

// Ad-hoc Serial diagnostics honor serialLogLevel. Structured events already go
// through writeSerialLogLine; prefer addDebugEvent for facts that have a code.
void serialTrace(LogLevel level, const char *message) {
  if (message == nullptr || !logLevelAtMost(level, serialLogLevel)) {
    return;
  }
  Serial.println(message);
}

void serialTracef(LogLevel level, const char *fmt, ...) {
  if (fmt == nullptr || !logLevelAtMost(level, serialLogLevel)) {
    return;
  }
  char line[192] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  Serial.println(line);
}

void maybeReportLogOverrunLocked() {
  const uint32_t overwritten = debugLog.overwritten();
  if (overwritten == 0 || overwritten == lastReportedLogOverwritten) {
    return;
  }
  if (lastReportedLogOverwritten != 0 &&
      overwritten < lastReportedLogOverwritten + 16U) {
    return;
  }
  lastReportedLogOverwritten = overwritten;
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  if (logLevelAtMost(LogLevel::WARNING, ringRetainLogLevel)) {
    debugLog.add(atMs, wallSec, LogLevel::WARNING, DebugCategory::SYSTEM,
                 DebugCode::SYSTEM_LOG_OVERRUN,
                 static_cast<int32_t>(overwritten), 0);
  }
}

void logEmit(LogLevel level, DebugCategory category, DebugCode code,
             int32_t argument1 = 0, int32_t argument2 = 0) {
  if (level == LogLevel::NONE) {
    return;
  }
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  DebugEvent event;
  event.atMs = atMs;
  event.wallSec = wallSec;
  event.level = level;
  event.category = category;
  event.code = code;
  event.argument1 = argument1;
  event.argument2 = argument2;

  const bool toSerial = logLevelAtMost(level, serialLogLevel);
  const bool toRing = logLevelAtMost(level, ringRetainLogLevel);
  if (!toSerial && !toRing) {
    return;
  }

  portENTER_CRITICAL(&debugLogMux);
  if (toRing) {
    debugLog.add(atMs, wallSec, level, category, code, argument1, argument2);
    maybeReportLogOverrunLocked();
  }
  portEXIT_CRITICAL(&debugLogMux);

  if (toSerial) {
    writeSerialLogLine(event);
  }
}

void addDebugEvent(DebugCategory category, DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0) {
  logEmit(debugCodeDefaultLevel(code), category, code, argument1, argument2);
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

bool clearLastShot() {
  persistedLastShot = PersistedLastShot{};
  return lastShotStore.clear();
}

void persistLastShotSnapshot(const PersistedLastShot &snapshot) {
  persistedLastShot = snapshot;
  (void)lastShotStore.persist(snapshot);
}

void persistLastShotFromEndedCycle(EndReason reason, uint32_t durationMs) {
  PersistedLastShot last = {};
  last.valid = true;
  last.cycleId = session.id;
  last.durationMs = durationMs;
  last.endReason = reason;
  last.weightValid =
      currentWeightSequence != session.weightSequenceAtStart &&
      isfinite(currentWeight) &&
      static_cast<int32_t>(currentWeightReceivedAtMs - session.startedAtMs) >=
          0;
  last.currentWeightG = last.weightValid ? currentWeight : 0.0f;
  last.goalWeightG = session.config.goalWeightG;
  last.extractionExtended =
      session.extractionExtended && session.config.fastExtractionGuardEnabled;
  last.slowExtractionExtended =
      session.slowExtractionExtended &&
      session.config.slowExtractionGuardEnabled;
  last.activeStopWeightG =
      last.extractionExtended
          ? session.config.maxRecoveryWeightG
          : (last.slowExtractionExtended
                 ? session.config.minRecoveryWeightG
                 : static_cast<float>(session.config.goalWeightG));
  const uint32_t startMs =
      session.cn9ClosedAtMs != 0U ? session.cn9ClosedAtMs : session.startedAtMs;
  if (session.firstDropMs != 0 &&
      static_cast<int32_t>(session.firstDropMs - startMs) >= 0) {
    last.firstDropElapsedMs = session.firstDropMs - startMs;
  }
  last.retarePerformed = session.retarePerformed;
  last.shotType = static_cast<uint8_t>(lastShotTypeFromCycle(
      stopperState, session.startedWithScale, session.config.timerOnly,
      shot.automaticBrew));
  last.scaleAvailable = session.startedWithScale;
  last.fastExtractionGuardEnabled = session.config.fastExtractionGuardEnabled;
  last.slowExtractionGuardEnabled = session.config.slowExtractionGuardEnabled;
  last.autoToManualGuardEnabled = session.config.autoToManualGuardEnabled;
  last.autoToManualGuardEnforced = session.autoToManualGuardEnforced;
  last.autoToManualGuardArmed = session.autoToManualGuardArmed;
  last.noScaleShotGuardEnabled = runtimeConfig.avoidBbwShotWithoutScale;
  last.noScaleShotGuardArmed = noScaleShotGuardArmed;
  if (session.autoToManualGuardEnforced) {
    const uint32_t nowMs = millis();
    last.autoToManualGuardRemainingMs =
        static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs - nowMs) <=
                0
            ? 0U
            : session.autoToManualGuardDeadlineAtMs - nowMs;
  }
  if (last.extractionExtended) {
    last.minBrewTimeRemainingMs =
        durationMs >= session.config.minBrewTimeMs
            ? 0U
            : session.config.minBrewTimeMs - durationMs;
  }
  strncpy(last.scaleProtocol, scaleProtocolName, sizeof(last.scaleProtocol) - 1);
  last.scaleProtocol[sizeof(last.scaleProtocol) - 1] = '\0';
  persistLastShotSnapshot(last);
}

bool enqueueWebCommand(const WebCommand &command) {
  return webCommandQueue != nullptr &&
         xQueueSend(webCommandQueue, &command, 0) == pdTRUE;
}

RuntimeConfig effectiveRuntimeConfig() {
  return composeEffectiveConfig(runtimeConfig, presetBank);
}

void copyPresetBank(ShotPresetBank *out) {
  if (out == nullptr) {
    return;
  }
  *out = presetBank;
}

void copyRuntimeConfig(RuntimeConfig *out) {
  if (out == nullptr) {
    return;
  }
  *out = runtimeConfig;
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
  snapshot.timerValid = scaleTimerValid;
  snapshot.timerMs = scaleTimerMs;
  snapshot.timerAgeMs = scaleTimerAgeMs;
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
  if (state != ScaleLinkState::CONNECTED) {
    scaleTimerValid = false;
    scaleTimerMs = 0;
    scaleTimerAgeMs = 0;
  }
  portEXIT_CRITICAL(&scaleLinkMux);
  if (previous != state) {
    addDebugEvent(DebugCategory::SCALE,
                  state == ScaleLinkState::CONNECTED
                      ? DebugCode::SCALE_CONNECTED
                      : DebugCode::SCALE_DISCONNECTED);
    if (state == ScaleLinkState::CONNECTED &&
        runtimeConfig.buzzerScaleConnectedBeep) {
      emitAlert(AlertEvent::SCALE_CONNECTED);
    }
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
    if (runtimeConfig.buzzerScaleLostBeep && session.active &&
        (previous == WeightControlState::ACTIVE ||
         previous == WeightControlState::VALIDATING)) {
      emitAlert(AlertEvent::SCALE_LOST);
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
  serialTrace(LogLevel::WARNING, "Scale command queue full");
  return false;
}

RelaySafetySnapshot getRelaySafetySnapshot();
void requestRemoteTimerStop();
uint32_t cycleShotElapsedMs();

float cycleElapsedSeconds() {
  return cycleShotElapsedMs() / 1000.0f;
}

uint32_t cycleShotElapsedMs() {
  if (!session.active) {
    return 0U;
  }
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  return relay.closed ? elapsedMs(relay.closedAtMs) : 0U;
}

void flushPendingScaleTimerStopNow() {
  if (!pendingScaleTimerStop.pending) {
    return;
  }
  pendingScaleTimerStop.pending = false;
  requestRemoteTimerStop();
}

uint32_t scaleTimerStopDelayMsForCycle() {
  uint32_t delayMs = session.config.scaleTimerStopExtraDelayMs;
  if (session.scaleStartLagCaptured) {
    delayMs += session.scaleStartLagMs;
  }
  if (delayMs > MAX_SCALE_TIMER_STOP_CATCHUP_MS) {
    delayMs = MAX_SCALE_TIMER_STOP_CATCHUP_MS;
  }
  return delayMs;
}

void scheduleScaleTimerStopAfterCycle() {
  if (!session.timerStartCommandQueued || session.stopTimerRequested) {
    return;
  }
  const uint32_t delayMs = scaleTimerStopDelayMsForCycle();
  if (delayMs == 0U) {
    requestRemoteTimerStop();
    return;
  }
  pendingScaleTimerStop.pending = true;
  pendingScaleTimerStop.dueAtMs = millis() + delayMs;
}

void servicePendingScaleTimerStop() {
  if (!pendingScaleTimerStop.pending) {
    return;
  }
  if (static_cast<int32_t>(millis() - pendingScaleTimerStop.dueAtMs) < 0) {
    return;
  }
  pendingScaleTimerStop.pending = false;
  requestRemoteTimerStop();
}

void maybeCaptureScaleStartLag() {
  if (!session.active || session.scaleStartLagCaptured ||
      !session.remoteTimerStarted) {
    return;
  }
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!link.timerValid || link.timerMs == 0U) {
    return;
  }
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (!relay.closed) {
    return;
  }
  session.scaleStartLagMs = elapsedMs(relay.closedAtMs);
  session.scaleStartLagCaptured = true;
}

void transitionTo(StopperState nextState) {
  if (stopperState == nextState) {
    return;
  }

  const StopperState previousState = stopperState;
  stopperState = nextState;
  addDebugEvent(DebugCategory::STATE, DebugCode::STATE_TRANSITION,
                static_cast<int32_t>(previousState),
                static_cast<int32_t>(nextState));
}

#if SHOT_STOPPER_ENABLE_ALED == 1
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
  if (xTaskCreatePinnedToCore(statusIndicatorTask, "status_indicator",
                              STATUS_INDICATOR_TASK_STACK_SIZE, nullptr,
                              tskIDLE_PRIORITY + 1, &statusIndicatorTaskHandle,
                              CONTROL_TASK_CORE) != pdPASS) {
    vQueueDelete(statusIndicatorQueue);
    statusIndicatorQueue = nullptr;
    statusIndicatorTaskHandle = nullptr;
    return false;
  }
  lastPublishedIndicatorFrame = off;
  indicatorFramePublished = true;
  return true;
}
#endif

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
  feedbackTransitionStampPending = false;
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

// FreeRTOS calls this before aborting when stack canaries are enabled. Open
// CN9 first (IRAM GPIO path), then clear the RTC close marker. Avoid heap or
// Serial here.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char *) {
  openRelayElectricalFromIsr();
  recordRelayCommandedClosed(false);
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
    feedbackTransitionStampPending = EXTERNAL_SAFETY_HARDWARE_PRESENT;
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

void applyBrewRfPreference(bool preferBluetooth) {
#if defined(SHOT_STOPPER_HAS_COEX)
  // Prefer BLE airtime while an automatic brew needs a fresh weight stream.
  // Restore balance as soon as CN9 opens so the Web UI stays responsive.
  (void)esp_coex_preference_set(preferBluetooth ? ESP_COEX_PREFER_BT
                                                : ESP_COEX_PREFER_BALANCE);
#else
  (void)preferBluetooth;
#endif
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
      addDebugEvent(DebugCategory::RELAY, DebugCode::CN9_ARM_FAILED,
                    static_cast<int32_t>(Cn9ArmFailReason::INVALID_LIMIT));
      return false;
    }
    if (before.state == RelaySafetyState::LOCKOUT) {
      addDebugEvent(DebugCategory::RELAY, DebugCode::CN9_ARM_FAILED,
                    static_cast<int32_t>(Cn9ArmFailReason::SAFETY_LOCKOUT));
      return false;
    }
    if (!platformClockReady || !relaySafetyTimersReady || !taskWatchdogReady ||
        relaySafetyTimer == nullptr || operationalLimitTimer == nullptr ||
        !independentSafetyTimer.ready() || criticalTaskWatchdogFault) {
      tripRelaySafety(!taskWatchdogReady || criticalTaskWatchdogFault
                          ? RelaySafetyFault::WATCHDOG_UNAVAILABLE
                          : RelaySafetyFault::INITIALIZATION_FAILED);
      addDebugEvent(
          DebugCategory::RELAY, DebugCode::CN9_ARM_FAILED,
          static_cast<int32_t>(Cn9ArmFailReason::SUPERVISOR_UNAVAILABLE));
      return false;
    }
    if (EXTERNAL_SAFETY_HARDWARE_PRESENT && readCn9FeedbackClosed()) {
      tripRelaySafety(RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
      addDebugEvent(
          DebugCategory::RELAY, DebugCode::CN9_ARM_FAILED,
          static_cast<int32_t>(Cn9ArmFailReason::FEEDBACK_STUCK_CLOSED));
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
      addDebugEvent(DebugCategory::RELAY, DebugCode::CN9_ARM_FAILED,
                    static_cast<int32_t>(Cn9ArmFailReason::TIMER_ARM_FAILED));
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
      feedbackTransitionStampPending = false;
      committed = true;
    }
    portEXIT_CRITICAL(&relayMux);
    if (!committed) {
      stopRelayDeadlineTimers();
      addDebugEvent(DebugCategory::RELAY, DebugCode::CN9_ARM_FAILED,
                    static_cast<int32_t>(Cn9ArmFailReason::ARM_CANCELED));
      return false;
    }
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_CLOSED,
                  static_cast<int32_t>(operationalLimitMs));
    if (session.automaticEnabled) {
      applyBrewRfPreference(true);
    }
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
  feedbackTransitionStampPending = false;
  portEXIT_CRITICAL(&relayMux);
  stopRelayDeadlineTimers();
  applyBrewRfPreference(false);
  if (wasClosed) {
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
  bool pending = false;
  bool expectedClosed = false;
  uint32_t settleStartedAtMs = 0;
  portENTER_CRITICAL(&relayMux);
  if (feedbackTransitionStampPending) {
    feedbackTransitionStartedAtMs = millis();
    feedbackTransitionStampPending = false;
  }
  pending = feedbackTransitionPending;
  expectedClosed = feedbackExpectedClosed;
  settleStartedAtMs = feedbackTransitionStartedAtMs;
  portEXIT_CRITICAL(&relayMux);

  if (pending) {
    if (elapsedMs(settleStartedAtMs) < CN9_FEEDBACK_SETTLE_MS) {
      return;
    }
    portENTER_CRITICAL(&relayMux);
    const bool stillPending = feedbackTransitionPending &&
        elapsedMs(feedbackTransitionStartedAtMs) >= CN9_FEEDBACK_SETTLE_MS;
    expectedClosed = feedbackExpectedClosed;
    if (stillPending) {
      feedbackTransitionPending = false;
    }
    portEXIT_CRITICAL(&relayMux);
    if (stillPending && feedbackClosed != expectedClosed) {
      tripRelaySafety(expectedClosed
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

void requestRemoteTimerStop();
void flushPendingScaleTimerStopNow();
void scheduleScaleTimerStopAfterCycle();
void servicePendingScaleTimerStop();

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
    serialTracef(LogLevel::DEBUG, "Remote timer stop queued for cycle %lu",
                 static_cast<unsigned long>(session.id));
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
    serialTracef(LogLevel::DEBUG, "Remote timer stop deferred for cycle %lu",
                 static_cast<unsigned long>(session.id));
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
// Shot weight-cut prediction and offset analysis
// ---------------------------------------------------------------------------

bool fastExtractionGuardSession();
bool slowExtractionGuardSession();
float effectiveStopThreshold();
float effectiveMaxStopThreshold();
float effectiveMinStopThreshold();

void resetShotTrajectory(uint32_t startedAtMs) {
  shot.startMs = startedAtMs;
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
  shot.automaticBrew = false;
}

float activeWeightCutTargetG() {
  if (session.extractionExtended && fastExtractionGuardSession()) {
    return effectiveMaxStopThreshold();
  }
  if (session.slowExtractionExtended && slowExtractionGuardSession()) {
    return effectiveMinStopThreshold();
  }
  return effectiveStopThreshold();
}

void calculateExpectedEndTime() {
  shot.expectedEndS = predictedWeightStopTimeS(
      shot.timeS, shot.weight, shot.datapoints, activeWeightCutTargetG(),
      session.config.operationalWallMs / 1000.0f);
}

bool shouldTrackWeight() {
  return session.active && !session.config.timerOnly &&
         session.weightControlState != WeightControlState::INACTIVE &&
         session.weightControlState != WeightControlState::FAULT_STOPPED &&
         stopperState == StopperState::BREW;
}

float effectiveStopThreshold() {
  return static_cast<float>(session.config.goalWeightG) -
         session.config.weightOffsetG;
}

float effectiveMaxStopThreshold() {
  return session.config.maxRecoveryWeightG - session.config.weightOffsetG;
}

float effectiveMinStopThreshold() {
  return session.config.minRecoveryWeightG - session.config.weightOffsetG;
}

bool fastExtractionGuardSession() {
  return session.active && session.config.fastExtractionGuardEnabled &&
         !session.config.timerOnly && session.startedWithScale;
}

bool slowExtractionGuardSession() {
  return session.active && session.config.slowExtractionGuardEnabled &&
         !session.config.timerOnly && session.startedWithScale;
}

bool minBrewTimeReached() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (!relay.closed) {
    return false;
  }
  return elapsedMs(relay.closedAtMs) >= session.config.minBrewTimeMs;
}

bool maxBrewTimeReached() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (!relay.closed) {
    return false;
  }
  return elapsedMs(relay.closedAtMs) >= session.config.maxBrewTimeMs;
}

bool targetWeightReached(float weight) {
  return weight >= effectiveStopThreshold();
}

bool minRecoveryWeightReached(float weight) {
  return weight >= effectiveMinStopThreshold();
}

void requestScaleBrewBeep(uint32_t cycleId);
void enterBrewOrManualFromStart();
void demoteActiveCycleToRinseOrEnd();

void recordFirstDropTimestamp(uint32_t receivedAtMs) {
  if (session.firstDropMs == 0) {
    session.firstDropMs = receivedAtMs;
  }
}

void requestFirstDropBeep() {
  if (!session.firstDropsBeepSent && session.config.firstDropBeep) {
    emitAlert(AlertEvent::FIRST_DROP, session.id);
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

bool bbwProtectionActive() {
  if (!bbwAutomaticScaleSession()) {
    return false;
  }
  return !session.bbwProtectionEnded;
}

void resetRetareStabilityStreak();

void markRetareEnded(uint32_t endedAtMs) {
  (void)endedAtMs;
  if (session.retareEnded) {
    return;
  }
  session.retareEnded = true;
  session.retareDisabled = true;
  resetRetareStabilityStreak();
}

bool bbwWeightStopInhibited() {
  if (!bbwAutomaticScaleSession()) {
    return false;
  }
  if (retareWindowOpen()) {
    return true;
  }
  if (bbwProtectionActive()) {
    return true;
  }
  return false;
}

bool withinRinseGestureWindow() {
  return elapsedMs(session.startedAtMs) <= session.config.rinseGestureMs;
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
  requestFirstDropBeep();
}

void performAutomaticRetare() {
  if (session.retarePerformed || session.retareDisabled || !retareWindowOpen()) {
    return;
  }
  session.retarePerformed = true;
  resetRetareStabilityStreak();
  (void)requestRemoteRetare();
  emitImmediateCommandAlertIfBuzzer();
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
  session.bbwProtectionEnded = false;
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
    session.bbwProtectionEnded = true;
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
  if (!session.bbwProtectionEnded &&
      elapsedMs(session.startedAtMs) >= session.config.bbwProtectionMs) {
    session.bbwProtectionEnded = true;
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

void enterFastExtractionExtended(float weightG, uint32_t atMs) {
  if (session.extractionExtended) {
    return;
  }
  session.extractionExtended = true;
  session.targetReachedEarly = true;
  session.targetReachedAtMs = atMs;
  addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_ENTERED,
                static_cast<int32_t>(weightG * 100.0f),
                static_cast<int32_t>(elapsedMs(session.startedAtMs)));
  if (buzzerPatternForExtendedPulseRate(
          runtimeConfig.buzzerExtendedPulseRate) != BuzzerPattern::NONE) {
    emitAlert(AlertEvent::EXTENDED_PULSE, session.id);
  }
  calculateExpectedEndTime();
}

void enterSlowExtractionExtended(float weightG, uint32_t atMs) {
  if (session.slowExtractionExtended || session.extractionExtended) {
    return;
  }
  session.slowExtractionExtended = true;
  addDebugEvent(DebugCategory::SCALE, DebugCode::SLOW_EXTRACTION_ENTERED,
                static_cast<int32_t>(weightG * 100.0f),
                static_cast<int32_t>(elapsedMs(session.startedAtMs)));
  (void)atMs;
  if (buzzerPatternForExtendedPulseRate(
          runtimeConfig.buzzerExtendedPulseRate) != BuzzerPattern::NONE) {
    emitAlert(AlertEvent::EXTENDED_PULSE, session.id);
  }
  calculateExpectedEndTime();
}

void considerDirectStopSample(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence,
                              uint32_t connectionGeneration) {
  if (!shouldTrackWeight() || packetSequence == 0 || !isfinite(weight) ||
      bbwWeightStopInhibited()) {
    return;
  }

  const bool overload = fabsf(weight) > MAX_AUTOMATION_WEIGHT_G;
  const bool active =
      session.weightControlState == WeightControlState::ACTIVE;
  const bool validating =
      session.weightControlState == WeightControlState::VALIDATING;
  if (!overload && !active) {
    return;
  }
  if (overload && !active && !validating) {
    return;
  }
  const bool overMax =
      fastExtractionGuardSession() && session.extractionExtended &&
      weight >= effectiveMaxStopThreshold();
  const bool overMin =
      slowExtractionGuardSession() && session.slowExtractionExtended &&
      weight >= effectiveMinStopThreshold();
  const bool overThreshold = weight >= effectiveStopThreshold();
  if (!overThreshold && !overload && !overMax && !overMin) {
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

  if (overMin) {
    session.directStopPending = true;
    session.directStopReason = EndReason::SLOW_EXTRACTION_MIN_WEIGHT;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SLOW_EXTRACTION_STOP_MIN_WEIGHT,
                  static_cast<int32_t>(weight * 100.0f));
    return;
  }

  if (overThreshold && fastExtractionGuardSession() &&
      (!minBrewTimeReached() || session.extractionExtended)) {
    enterFastExtractionExtended(weight, receivedAtMs);
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
    serialTrace(LogLevel::WARNING,
                "Shot trajectory full; ignoring additional samples");
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

  serialTracef(LogLevel::DEBUG, "%.2fg, t=%.2fs, expected end=%.2fs",
               weight, shot.timeS[index], shot.expectedEndS);
  return true;
}

void considerScaleFlowMarkers(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence);

bool recordWeightSampleWithProvenance(float weight, uint32_t receivedAtMs,
                                      uint32_t packetSequence,
                                      uint32_t connectionGeneration) {
  if (!isfinite(weight) || fabsf(weight) > MAX_PARSED_WEIGHT_G) {
    serialTrace(LogLevel::WARNING, "Invalid or out-of-range weight ignored");
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

  if (weight < MIN_AUTOMATION_WEIGHT_G ||
      weight > MAX_AUTOMATION_WEIGHT_G) {
    if (fabsf(weight) > MAX_AUTOMATION_WEIGHT_G) {
      considerDirectStopSample(weight, receivedAtMs, packetSequence,
                               connectionGeneration);
    }
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

  considerDirectStopSample(weight, receivedAtMs, packetSequence,
                           connectionGeneration);
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
  const bool offsetAnalysis = shot.automaticBrew && !session.config.timerOnly &&
                              session.calibrationEligible &&
                              !session.extractionExtended &&
                              !session.slowExtractionExtended;
  if (!logEligible && !offsetAnalysis) {
    return;
  }

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = offsetAnalysis;
  pendingFinalize.logEligible = logEligible;
  pendingFinalize.endedAtMs = millis();
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.cycleStartedAtMs =
      session.cn9ClosedAtMs != 0U ? session.cn9ClosedAtMs : session.startedAtMs;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs =
      static_cast<uint16_t>(durationMs / 100U);
  const uint32_t shotAnchorMs = pendingFinalize.cycleStartedAtMs;
  if (session.firstDropMs != 0 &&
      static_cast<int32_t>(session.firstDropMs - shotAnchorMs) >= 0) {
    pendingFinalize.firstDropDs = static_cast<uint16_t>(
        (session.firstDropMs - shotAnchorMs) / 100U);
  }
  pendingFinalize.goalWeightG = session.config.goalWeightG;
  pendingFinalize.weightOffsetG = session.config.weightOffsetG;
  pendingFinalize.scaleBaselineG =
      session.scaleBaselineReady ? session.scaleBaselineG : 0.0f;
  pendingFinalize.startedWithScale = session.startedWithScale;
  pendingFinalize.timerOnly = session.config.timerOnly;
  pendingFinalize.automaticBrew = shot.automaticBrew;
  pendingFinalize.finalState = stopperState;
  pendingFinalize.endReason = reason;
  pendingFinalize.extractionGuardEnabled =
      session.config.fastExtractionGuardEnabled;
  pendingFinalize.extractionExtended = session.extractionExtended;
  pendingFinalize.slowExtractionGuardEnabled =
      session.config.slowExtractionGuardEnabled;
  pendingFinalize.slowExtractionExtended = session.slowExtractionExtended;
  pendingFinalize.targetReachedEarly = session.targetReachedEarly;
  pendingFinalize.maxRecoveryWeightG = session.config.maxRecoveryWeightG;
  pendingFinalize.minBrewTimeMs = session.config.minBrewTimeMs;
  if (session.targetReachedAtMs != 0 &&
      static_cast<int32_t>(session.targetReachedAtMs - shotAnchorMs) >= 0) {
    pendingFinalize.targetReachedEarlyDs = static_cast<uint16_t>(
        (session.targetReachedAtMs - shotAnchorMs) / 100U);
  }
  pendingFinalize.lastKnownWeightValid =
      session.hasWeightAnchor && isfinite(session.lastAcceptedWeightG);
  pendingFinalize.lastKnownWeightG = session.lastAcceptedWeightG;
  pendingFinalize.activePresetId = session.activePresetId != 0
                                       ? session.activePresetId
                                       : presetBank.activeId;
  pendingFinalize.cycleId = session.id;
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
      snapshot.automaticBrew));
  record.cutType = static_cast<uint8_t>(
      shotLogCutFromEndReason(snapshot.endReason));
  record.extractionGuardEnabled = shotLogPackGuardFlags(
      snapshot.extractionGuardEnabled, snapshot.slowExtractionGuardEnabled);
  record.extractionExtended = shotLogPackExtendedFlags(
      snapshot.extractionExtended, snapshot.slowExtractionExtended);
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
    const size_t nextCount =
        shotLog.count() < SHOT_LOG_CAPACITY ? shotLog.count() + 1U
                                            : SHOT_LOG_CAPACITY;
    const size_t blobBytes =
        sizeof(ShotLogHeader) + nextCount * sizeof(ShotLogRecord);
    addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                  static_cast<int32_t>(blobBytes),
                  static_cast<int32_t>(shotLog.count()));
  }
}

void cancelPendingFinalize(const char *reason) {
  if (!pendingFinalize.pending) {
    return;
  }
  const PendingShotFinalize snapshot = pendingFinalize;
  pendingFinalize.pending = false;
  serialTrace(LogLevel::INFO, reason != nullptr
                                  ? reason
                                  : "Previous drip analysis cancelled");
  if (!snapshot.logEligible) {
    return;
  }
  ActualWeightSource source = ActualWeightSource::NONE;
  float weightG = 0.0f;
  bool valid = false;
  if (snapshot.lastKnownWeightValid && isfinite(snapshot.lastKnownWeightG)) {
    source = ActualWeightSource::LAST_KNOWN;
    weightG = snapshot.lastKnownWeightG;
    valid = true;
  }
  commitPendingShotLog(snapshot, weightG, valid, source);
}

void maybeQueueAutoToManualGuardSample(const PendingShotFinalize &snapshot,
                                       float finalWeightG,
                                       bool postDripWeightValid) {
  if (!postDripWeightValid || snapshot.timerOnly ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended ||
      snapshot.endReason == EndReason::AUTO_TO_MANUAL_GUARD ||
      snapshot.endReason == EndReason::RINSE_COMPLETE ||
      snapshot.endReason == EndReason::SHORT_SHOT ||
      snapshot.durationDs < (MIN_SHOT_LOG_DURATION_MS / 100U) ||
      !autoToManualGuardSampleErrorOk(finalWeightG, snapshot.goalWeightG)) {
    return;
  }
  ShotPreset *preset = mutableShotPreset(
      presetBank, snapshot.activePresetId != 0 ? snapshot.activePresetId
                                               : presetBank.activeId);
  if (preset == nullptr) {
    serialTrace(LogLevel::WARNING,
                "A->M sample skipped; shot preset no longer exists");
    return;
  }
  pushAutoToManualGuardSample(preset->autoToManualGuardSamplesDs,
                              snapshot.durationDs);
  RuntimeConfig candidate = runtimeConfig;
  applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
  if (preset->id == presetBank.activeId) {
    memcpy(candidate.autoToManualGuardSamplesDs, preset->autoToManualGuardSamplesDs,
           sizeof(candidate.autoToManualGuardSamplesDs));
  }
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_ATM_SAMPLES);
  serialTracef(LogLevel::INFO, "A->M guard sample queued; trend ms=%lu",
               static_cast<unsigned long>(autoToManualGuardTrendMs(
                   preset->autoToManualGuardSamplesDs,
                   preset->operationalWallMs)));
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

  if (logWeightValid && persistedLastShot.valid &&
      persistedLastShot.cycleId == snapshot.cycleId) {
    persistedLastShot.weightValid = true;
    persistedLastShot.currentWeightG = logWeightG;
    persistLastShotSnapshot(persistedLastShot);
  }

  maybeQueueAutoToManualGuardSample(snapshot, finalWeightG, postDripWeightValid);

  if (!snapshot.offsetAnalysis || snapshot.extractionExtended ||
      snapshot.slowExtractionExtended) {
    return;
  }

  if (!scaleAvailable() || !isfinite(currentWeight) ||
      !isfinite(snapshot.weightOffsetG) ||
      currentWeightSequence == snapshot.endedWeightSequence ||
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) <=
          0) {
    serialTrace(LogLevel::INFO,
                "Final weight unavailable; offset unchanged");
    return;
  }

  finalWeightG = currentWeight;

  serialTracef(LogLevel::INFO,
               "Final weight: %.2fg; cycle goal: %.2fg; cycle offset: %.2fg",
               finalWeightG, snapshot.goalWeightG, snapshot.weightOffsetG);

  const float observedError =
      finalWeightG - snapshot.goalWeightG + snapshot.weightOffsetG;
  if (fabsf(observedError) > MAX_OFFSET_G) {
    serialTrace(LogLevel::INFO, "Shot error too large; offset unchanged");
    return;
  }

  float updatedOffset =
      snapshot.weightOffsetG + finalWeightG - snapshot.goalWeightG;
  if (!isfinite(updatedOffset)) {
    serialTrace(LogLevel::WARNING,
                "Calculated offset outside safe range; offset unchanged");
    return;
  }
  if (updatedOffset < 0.0f) {
    updatedOffset = 0.0f;
  } else if (updatedOffset > MAX_OFFSET_G) {
    updatedOffset = MAX_OFFSET_G;
  }

  ShotPreset *preset = mutableShotPreset(
      presetBank, snapshot.activePresetId != 0 ? snapshot.activePresetId
                                               : presetBank.activeId);
  if (preset == nullptr) {
    serialTrace(LogLevel::WARNING,
                "Offset learn skipped; shot preset no longer exists");
    return;
  }
  preset->weightOffsetG = updatedOffset;
  RuntimeConfig candidate = runtimeConfig;
  applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_OFFSET);
  serialTracef(LogLevel::INFO, "New offset pending durable commit: %.2f",
               preset->weightOffsetG);
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
  const bool timerValid = scale.hasTimer();
  const uint32_t timerMs = timerValid ? scale.getTimerMs() : 0;
  const uint32_t timerAgeMs = timerValid ? scale.lastTimerAgeMs() : 0;
  portENTER_CRITICAL(&scaleLinkMux);
  scaleRejectedPackets = scale.rejectedPacketCount();
  scaleReconnects = scale.reconnectCount();
  scaleLastDisconnectReason =
      static_cast<uint8_t>(scale.lastDisconnectReason());
  strncpy(scaleProtocolName, scale.connectedProtocolName(),
          sizeof(scaleProtocolName) - 1);
  scaleProtocolName[sizeof(scaleProtocolName) - 1] = '\0';
  scaleTimerValid = timerValid;
  scaleTimerMs = timerMs;
  scaleTimerAgeMs = timerAgeMs;
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
      event.usedCombinedTareStart = true;
      event.writeSucceeded = scale.tareStartTimer();
    }
    if (!event.writeSucceeded) {
      event.usedCombinedTareStart = false;
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
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
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

bool scaleIsBookooGeneric() {
  return scale.isConnected() &&
         strcmp(scale.connectedProtocolName(), "bookoo_generic") == 0;
}

bool enqueueScaleDebugCommand(BookooDebugAction action, uint8_t beepLevel) {
  if (!scaleIsBookooGeneric()) {
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
  if (!scaleIsBookooGeneric()) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
    return;
  }
  bool succeeded = false;
  switch (action) {
    case BookooDebugAction::START:
      succeeded = scale.startTimer();
      break;
    case BookooDebugAction::STOP:
      succeeded = scale.stopTimer();
      break;
    case BookooDebugAction::TARE:
      succeeded = scale.tare();
      break;
    case BookooDebugAction::COMBINED:
      if (!scale.supportsTareStartTimer()) {
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
        return;
      }
      succeeded = scale.tareStartTimer();
      break;
    case BookooDebugAction::BEEP:
      if (!scale.supportsIndependentBeep()) {
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
        return;
      }
      succeeded = scale.beepWithoutStateChange();
      break;
    case BookooDebugAction::VOLUME:
      if (!scale.supportsIndependentBeep()) {
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_DEBUG_UNSUPPORTED);
        return;
      }
      succeeded = scale.setBeepLevel(beepLevel);
      break;
  }
  addDebugEvent(DebugCategory::SCALE,
                succeeded ? DebugCode::SCALE_DEBUG_OK
                          : DebugCode::SCALE_DEBUG_FAILED);
  updateWorkerLinkState();
}

void requestScaleBrewBeep(uint32_t cycleId);
void requestScalePaddleReturnReminderBeep();
void requestScaleCompletionBeep();
void cancelScalePaddleReturnReminderBeep();

AlertOutputChannel currentAlertOutputChannel() {
  return effectiveAlertOutputChannel(runtimeConfig.alertOutputChannel);
}

void applyBookooConnectBeepPolicy() {
  if (!scaleIsBookooGeneric() || !scale.supportsIndependentBeep()) {
    return;
  }
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (runtimeConfig.bookooMuteOnBuzzerOnly &&
      channel == AlertOutputChannel::BUZZER_ONLY) {
    (void)scale.setBeepLevel(0);
    return;
  }
  if (runtimeConfig.bookooConnectBeepLevel >= 1 &&
      runtimeConfig.bookooConnectBeepLevel <= BOOKOO_BEEP_LEVEL_MAX &&
      (channel == AlertOutputChannel::SCALE_ONLY ||
       channel == AlertOutputChannel::SCALE_PRIORITY)) {
    (void)scale.setBeepLevel(runtimeConfig.bookooConnectBeepLevel);
  }
}

void requestBookooSilenceIfConfigured() {
  if (!runtimeConfig.bookooMuteOnBuzzerOnly) {
    return;
  }
  if (currentAlertOutputChannel() != AlertOutputChannel::BUZZER_ONLY) {
    return;
  }
  (void)enqueueScaleDebugCommand(BookooDebugAction::VOLUME, 0);
}

bool alertEventScaleCapable(AlertEvent event) {
  switch (event) {
    case AlertEvent::SCALE_LOST:
    case AlertEvent::ATM_END:
    case AlertEvent::MANUAL_NO_SCALE:
    case AlertEvent::EXTENDED_PULSE:
    case AlertEvent::SCALE_CONNECTED:
      return false;
    default:
      return true;
  }
}

bool startPulseTrain(BuzzerPattern pattern, uint32_t durationMs) {
  if (!BUZZER_SUPPORT_ENABLED || !localBuzzer.ready ||
      !buzzerPatternIsPulseTrain(pattern)) {
    return false;
  }
  return localBuzzer.request(pattern, durationMs);
}

bool startExtendedPulseTrain(uint32_t durationMs) {
  return startPulseTrain(
      buzzerPatternForExtendedPulseRate(runtimeConfig.buzzerExtendedPulseRate),
      durationMs);
}

void stopPulseTrains() {
  if (buzzerPatternIsPulseTrain(localBuzzer.pending)) {
    localBuzzer.stopIf(localBuzzer.pending);
  }
  if (buzzerPatternIsPulseTrain(localBuzzer.active)) {
    localBuzzer.stopIf(localBuzzer.active);
  }
}

bool emitLocalAlertBuzzer(BuzzerPattern pattern) {
  if (!BUZZER_SUPPORT_ENABLED || !localBuzzer.ready) {
    return false;
  }
  if (buzzerPatternIsPulseTrain(pattern)) {
    return startPulseTrain(pattern, 0);
  }
  return localBuzzer.request(pattern);
}

bool queueScaleIndependentAlert(AlertEvent event, uint32_t cycleId) {
  if (!scaleAvailable()) {
    return false;
  }
  switch (event) {
    case AlertEvent::FIRST_DROP:
      requestScaleBrewBeep(cycleId);
      return true;
    case AlertEvent::PADDLE_REMINDER:
      requestScalePaddleReturnReminderBeep();
      return true;
    case AlertEvent::COMPLETION_EXTRA:
      requestScaleCompletionBeep();
      return true;
    default:
      return false;
  }
}

// Independent / multi-tone alerts: first drop, paddle, completion, triples.
bool emitAlert(AlertEvent event, uint32_t cycleId) {
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (event == AlertEvent::SCALE_CONNECTED) {
    if (channel != AlertOutputChannel::BUZZER_ONLY) {
      return false;
    }
    return emitLocalAlertBuzzer(BuzzerPattern::ECHO);
  }
  const bool scaleCapable = alertEventScaleCapable(event);
  BuzzerPattern buzzerPattern = BuzzerPattern::SINGLE;
  if (event == AlertEvent::SCALE_LOST || event == AlertEvent::ATM_END ||
      event == AlertEvent::MANUAL_NO_SCALE) {
    buzzerPattern = BuzzerPattern::TRIPLE;
  } else if (event == AlertEvent::EXTENDED_PULSE) {
    buzzerPattern =
        buzzerPatternForExtendedPulseRate(runtimeConfig.buzzerExtendedPulseRate);
    if (buzzerPattern == BuzzerPattern::NONE) {
      return false;
    }
  }

  if (!scaleCapable) {
    if (channel == AlertOutputChannel::SCALE_ONLY) {
      return false;
    }
    return emitLocalAlertBuzzer(buzzerPattern);
  }

  if (channel == AlertOutputChannel::BUZZER_ONLY) {
    return emitLocalAlertBuzzer(buzzerPattern);
  }
  if (channel == AlertOutputChannel::SCALE_ONLY) {
    return queueScaleIndependentAlert(event, cycleId);
  }
  // SCALE_PRIORITY: scale if available, else buzzer. Never both.
  if (queueScaleIndependentAlert(event, cycleId)) {
    return true;
  }
  return emitLocalAlertBuzzer(buzzerPattern);
}

bool commandAlertUsesBuzzer() {
  if (!BUZZER_SUPPORT_ENABLED || !localBuzzer.ready) {
    return false;
  }
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (channel == AlertOutputChannel::BUZZER_ONLY) {
    return true;
  }
  if (channel == AlertOutputChannel::SCALE_PRIORITY) {
    return !scaleAvailable();
  }
  return false;
}

// Tare/start/stop replacement sounds: fire at the local CN9/paddle/retare
// moment when the buzzer is the routed output. Never wait for BLE.
void emitImmediateCommandAlertIfBuzzer() {
  if (commandAlertUsesBuzzer()) {
    emitLocalAlertBuzzer(BuzzerPattern::SINGLE);
  }
}

// BLE-result fallback only. Buzzer only already played at the local event.
// SCALE_PRIORITY still falls back here if the write missed, even if the
// scale dropped while the ATT write was in flight.
void emitCommandAlert(AlertEvent event, bool commandAttempted,
                      bool writeSucceeded) {
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (channel == AlertOutputChannel::BUZZER_ONLY) {
    return;
  }
  if (channel == AlertOutputChannel::SCALE_ONLY) {
    (void)event;
    return;
  }
  if (!commandAttempted || !writeSucceeded) {
    emitLocalAlertBuzzer(BuzzerPattern::SINGLE);
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

bool shotCompletionGetsDoubleBeep(EndReason reason) {
  switch (reason) {
    case EndReason::PADDLE:
    case EndReason::SCALE_THRESHOLD:
    case EndReason::WEIGHT_ANOMALY:
    case EndReason::GLOBAL_LIMIT:
    case EndReason::CONFIGURED_WALL_LIMIT:
    case EndReason::WEB_STOP:
    case EndReason::PHYSICAL_OVERRIDE:
    case EndReason::WEB_HEARTBEAT_TIMEOUT:
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
    case EndReason::FAST_EXTRACTION_MIN_TIME:
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
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

void serviceExtendedPulseAlert() {
  const BuzzerPattern pattern =
      buzzerPatternForExtendedPulseRate(runtimeConfig.buzzerExtendedPulseRate);
  const bool want =
      BUZZER_SUPPORT_ENABLED && localBuzzer.ready &&       session.active &&
      (session.extractionExtended || session.slowExtractionExtended) &&
      pattern != BuzzerPattern::NONE &&
      currentAlertOutputChannel() != AlertOutputChannel::SCALE_ONLY;
  if (!want) {
    if (buzzerPatternIsPulseTrain(localBuzzer.active) &&
        localBuzzer.deadlineAtMs == 0) {
      localBuzzer.stopIf(localBuzzer.active);
    }
    return;
  }
  if (localBuzzer.active == pattern || localBuzzer.pending == pattern) {
    return;
  }
  if (localBuzzer.busy()) {
    return;
  }
  emitAlert(AlertEvent::EXTENDED_PULSE, session.id);
}

void serviceScaleCompletionBeep() {
  if (!scaleCompletionBeepScheduled) {
    return;
  }
  if (static_cast<int32_t>(millis() - scaleCompletionBeepDueAtMs) < 0) {
    return;
  }
  if (!emitAlert(AlertEvent::COMPLETION_EXTRA)) {
    // Local buzzer may still be playing ATM TRIPLE; retry briefly.
    const AlertOutputChannel channel = currentAlertOutputChannel();
    if (channel != AlertOutputChannel::SCALE_ONLY && BUZZER_SUPPORT_ENABLED) {
      scaleCompletionBeepDueAtMs = millis() + 50;
      return;
    }
  }
  scaleCompletionBeepScheduled = false;
}

void servicePaddleReturnReminder() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  // Read the GPIO here rather than a debounced state: this reminder describes
  // the physical paddle circuit as it is wired at this instant.
  const bool paddleOnCn9Off = readRawPaddleOn() && !relay.closed;
  const AlertOutputChannel channel = currentAlertOutputChannel();
  const bool localBuzzerUsable =
      BUZZER_SUPPORT_ENABLED && localBuzzer.ready;
  const bool scaleUsable = scaleAvailable();
  bool outputUsable = false;
  switch (channel) {
    case AlertOutputChannel::SCALE_ONLY:
      outputUsable = scaleUsable;
      break;
    case AlertOutputChannel::BUZZER_ONLY:
      outputUsable = localBuzzerUsable;
      break;
    case AlertOutputChannel::SCALE_PRIORITY:
      outputUsable = scaleUsable || localBuzzerUsable;
      break;
  }
  const bool shouldRemind =
      runtimeConfig.paddleReturnReminderBeep && paddleOnCn9Off && outputUsable;
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
    if (emitAlert(AlertEvent::PADDLE_REMINDER)) {
      paddleReturnReminderLastAtMs = now;
    }
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

uint32_t nextScaleConnectRetryMs(uint32_t currentRetryMs) {
  if (currentRetryMs > SCALE_CONNECT_RETRY_MAX_MS / 2U) {
    return SCALE_CONNECT_RETRY_MAX_MS;
  }
  return currentRetryMs * 2U;
}

void copyPreferredScaleMac(char *out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }
  portENTER_CRITICAL(&scalePreferredMacMux);
  strncpy(out, scalePreferredMac, capacity - 1);
  out[capacity - 1] = '\0';
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

void copyPreferredScaleName(char *out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }
  portENTER_CRITICAL(&scalePreferredMacMux);
  strncpy(out, scalePreferredName, capacity - 1);
  out[capacity - 1] = '\0';
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

bool scaleDiscoveryPaused(uint32_t nowMs = millis()) {
  return scaleMacCachePauseRemainingMs(nowMs) > 0;
}

ScaleMacCacheMode currentScaleMacCacheMode() {
  return static_cast<ScaleMacCacheMode>(runtimeConfig.scaleMacCacheMode);
}

void noteScaleHistory(const char *mac, const char *name) {
  if (mac == nullptr || !validPreferredScaleMac(mac) || mac[0] == '\0') {
    return;
  }
  bool changed = false;
  portENTER_CRITICAL(&scalePreferredMacMux);
  changed = upsertScaleHistory(scaleHistory, scaleHistorySeq, mac, name);
  if (changed) {
    scalePreferredMacDirty = true;
  }
  portEXIT_CRITICAL(&scalePreferredMacMux);
}

void notePreferredScale(const char *mac, const char *name) {
  noteScaleHistory(mac, name);
  if (currentScaleMacCacheMode() == ScaleMacCacheMode::OFF) {
    return;
  }
  if (scaleDiscoveryPaused()) {
    return;
  }
  if (mac == nullptr || !validPreferredScaleMac(mac) || mac[0] == '\0') {
    return;
  }
  char canonicalMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  strncpy(canonicalMac, mac, PREFERRED_SCALE_MAC_CAPACITY - 1);
  canonicalizePreferredScaleMac(canonicalMac, sizeof(canonicalMac));
  char safeName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name)) {
    strncpy(safeName, name, PREFERRED_SCALE_NAME_CAPACITY - 1);
  }
  bool changed = false;
  bool macChanged = false;
  portENTER_CRITICAL(&scalePreferredMacMux);
  // FULL: lock first connect when empty; never auto-steal to another MAC
  // (case-insensitive — BLE often reports lowercase).
  if (scalePreferredMac[0] != '\0' &&
      !preferredScaleMacEqual(scalePreferredMac, canonicalMac)) {
    portEXIT_CRITICAL(&scalePreferredMacMux);
    return;
  }
  macChanged =
      strncmp(scalePreferredMac, canonicalMac, PREFERRED_SCALE_MAC_CAPACITY) !=
      0;
  const bool nameChanged =
      strncmp(scalePreferredName, safeName, PREFERRED_SCALE_NAME_CAPACITY) != 0;
  if (macChanged || nameChanged) {
    memcpy(scalePreferredMac, canonicalMac, sizeof(scalePreferredMac));
    strncpy(scalePreferredName, safeName, PREFERRED_SCALE_NAME_CAPACITY - 1);
    scalePreferredName[PREFERRED_SCALE_NAME_CAPACITY - 1] = '\0';
    scalePreferredMacDirty = true;
    changed = true;
  }
  portEXIT_CRITICAL(&scalePreferredMacMux);
  if (changed && macChanged) {
    serialTracef(LogLevel::INFO, "Preferred scale updated: %s — %s",
                 safeName[0] != '\0' ? safeName : "(unknown)", canonicalMac);
  }
}

void clearPreferredScaleCache();

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
  if (scale.isScanning() || scale.isConnected()) {
    // Restart discovery under the new (unlocked) policy.
    scale.disconnect();
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
  }
}

void selectPreferredScale(const char *mac, const char *name) {
  if (mac == nullptr || mac[0] == '\0') {
    clearPreferredScaleSelectionOnly();
    return;
  }
  if (!validPreferredScaleMac(mac)) {
    return;
  }
  char canonicalMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  strncpy(canonicalMac, mac, PREFERRED_SCALE_MAC_CAPACITY - 1);
  canonicalizePreferredScaleMac(canonicalMac, sizeof(canonicalMac));
  char resolvedName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name) && name[0] != '\0') {
    strncpy(resolvedName, name, PREFERRED_SCALE_NAME_CAPACITY - 1);
  } else {
    portENTER_CRITICAL(&scalePreferredMacMux);
    findScaleHistoryName(scaleHistory, canonicalMac, resolvedName,
                         sizeof(resolvedName));
    portEXIT_CRITICAL(&scalePreferredMacMux);
  }
  noteScaleHistory(canonicalMac, resolvedName);
  portENTER_CRITICAL(&scalePreferredMacMux);
  memcpy(scalePreferredMac, canonicalMac, sizeof(scalePreferredMac));
  strncpy(scalePreferredName, resolvedName, PREFERRED_SCALE_NAME_CAPACITY - 1);
  scalePreferredName[PREFERRED_SCALE_NAME_CAPACITY - 1] = '\0';
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
}

void servicePreferredScaleMacPersistence() {
#ifndef SHOT_STOPPER_HOST_TEST
  char mac[PREFERRED_SCALE_MAC_CAPACITY];
  char name[PREFERRED_SCALE_NAME_CAPACITY];
  ScaleHistoryEntry history[SCALE_HISTORY_CAPACITY];
  bool dirty = false;
  portENTER_CRITICAL(&scalePreferredMacMux);
  dirty = scalePreferredMacDirty;
  if (dirty) {
    memcpy(mac, scalePreferredMac, sizeof(mac));
    memcpy(name, scalePreferredName, sizeof(name));
    memcpy(history, scaleHistory, sizeof(history));
  }
  portEXIT_CRITICAL(&scalePreferredMacMux);
  if (!dirty) {
    return;
  }
  if (strncmp(persistedSettings.preferredScaleMac, mac,
              PREFERRED_SCALE_MAC_CAPACITY) == 0 &&
      strncmp(persistedSettings.preferredScaleName, name,
              PREFERRED_SCALE_NAME_CAPACITY) == 0 &&
      memcmp(persistedSettings.scaleHistory, history, sizeof(history)) == 0) {
    portENTER_CRITICAL(&scalePreferredMacMux);
    scalePreferredMacDirty = false;
    portEXIT_CRITICAL(&scalePreferredMacMux);
    return;
  }
  bool persistBusy = false;
  portENTER_CRITICAL(&settingsPersistMux);
  persistBusy = settingsPersistInFlight || settingsPersistResultReady;
  portEXIT_CRITICAL(&settingsPersistMux);
  if (persistBusy) {
    return;
  }
  memcpy(persistedSettings.preferredScaleMac, mac,
         sizeof(persistedSettings.preferredScaleMac));
  memcpy(persistedSettings.preferredScaleName, name,
         sizeof(persistedSettings.preferredScaleName));
  memcpy(persistedSettings.scaleHistory, history,
         sizeof(persistedSettings.scaleHistory));
  if (!runtimePersistPending ||
      (runtimePersistReasonBits & RUNTIME_PERSIST_REASON_SCALE_MAC) == 0) {
    queueRuntimePersist(RUNTIME_PERSIST_REASON_SCALE_MAC);
  }
  networkManager.syncPreferredScale(mac, name);
  if (mac[0] == '\0') {
    serialTrace(LogLevel::INFO, "Preferred scale MAC cleared; persist queued");
  } else {
    serialTracef(LogLevel::INFO, "Preferred scale MAC queued: %s — %s",
                 name[0] != '\0' ? name : "(unknown)", mac);
  }
#endif
}

void logScaleConnectionFailed(bool directed) {
  serialTracef(LogLevel::WARNING, "%s: %s",
               directed ? "Preferred scale connection failed"
                        : "Scale connection failed",
               scale.lastDisconnectReasonName());
}

// FULL + preferred MAC → name scan with connect-filter (not GAP directed).
bool shouldUseDirectedScaleScan(ScaleMacCacheMode cacheMode, bool hasMac) {
  return cacheMode == ScaleMacCacheMode::FULL && hasMac;
}

bool applyScaleDiscoveryPause() {
  if (!scaleDiscoveryPaused()) {
    return false;
  }
  if (scale.isConnected() || scale.isScanning()) {
    scale.disconnect();
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
  }
  return true;
}

void serviceScaleWorkerDiscovery(uint32_t &lastScanCycleMs,
                                 uint32_t &lastConnectLogMs,
                                 uint32_t &connectRetryMs,
                                 bool &connectAttemptSeriesActive,
                                 uint32_t &scanSessionAtMs) {
  // Library drop can happen on a beep/command path that never refreshed the
  // link snapshot. Clear CONNECTED before idle scan work so the UI cannot sit
  // on "BLE connected" for the whole (indefinite) discovery session.
  if (!scale.isConnected()) {
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
  }
  if (applyScaleDiscoveryPause()) {
    return;
  }

  const ScaleMacCacheMode cacheMode = currentScaleMacCacheMode();

  if (scale.isScanning()) {
    const bool connected = scale.pollScan();
    char seenMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
    char seenName[PREFERRED_SCALE_NAME_CAPACITY] = {};
    bool sawCompatibleAd = false;
    if (scale.takeSeenAdvertisement(seenMac, sizeof(seenMac), seenName,
                                    sizeof(seenName))) {
      noteScaleHistory(seenMac, seenName);
      sawCompatibleAd = true;
    }
    if (connected) {
      connectAttemptSeriesActive = false;
      connectRetryMs = SCALE_CONNECT_RETRY_MS;
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
      applyBookooConnectBeepPolicy();
      return;
    }
    if (scale.isScanning()) {
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
        if (directed) {
          serialTrace(LogLevel::WARNING,
                      "Preferred scale attempt: no advertisement");
        } else {
          serialTrace(LogLevel::WARNING,
                      "Scale name scan: no advertisement");
        }
      } else if (logAttempt && directed && sawCompatibleAd) {
        lastConnectLogMs = lastScanCycleMs;
        connectAttemptSeriesActive = true;
        serialTrace(LogLevel::WARNING,
                    "Preferred scale attempt: other scale seen, waiting");
      }
      if (elapsedMs(scanSessionAtMs) >= SCALE_SCAN_HCI_RESTART_MS) {
        char preferredMac[PREFERRED_SCALE_MAC_CAPACITY];
        copyPreferredScaleMac(preferredMac, sizeof(preferredMac));
        const bool hasMac =
            preferredMac[0] != '\0' && validPreferredScaleMac(preferredMac);
        const bool useDirected =
            shouldUseDirectedScaleScan(cacheMode, hasMac);
        if (scale.startScan(useDirected ? preferredMac : nullptr, true)) {
          scanSessionAtMs = lastScanCycleMs;
        }
      }
      return;
    }

    lastScanCycleMs = millis();
    const AcaiaDisconnectReason reason = scale.lastDisconnectReason();
    const bool finishedDirectedAttempt =
        cacheMode == ScaleMacCacheMode::FULL && hasPreferredScaleMac();

    if (finishedDirectedAttempt) {
      serialTracef(LogLevel::WARNING,
                   "Preferred scale attempt: %s",
                   scale.lastDisconnectReasonName());
    }

    if (reason == AcaiaDisconnectReason::SCAN_START_FAILED) {
      connectRetryMs = nextScaleConnectRetryMs(connectRetryMs);
    } else {
      connectRetryMs = SCALE_CONNECT_RETRY_MS;
    }
    const bool logAttempt =
        !connectAttemptSeriesActive ||
        elapsedMs(lastConnectLogMs) >= SCALE_CONNECT_LOG_MS;
    if (finishedDirectedAttempt) {
      lastConnectLogMs = lastScanCycleMs;
      connectAttemptSeriesActive = true;
    } else if (logAttempt) {
      lastConnectLogMs = lastScanCycleMs;
      connectAttemptSeriesActive = true;
      logScaleConnectionFailed(false);
    }
    updateWorkerLinkState();
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return;
  }

  if (elapsedMs(lastScanCycleMs) < connectRetryMs) {
    return;
  }

  lastScanCycleMs = millis();
  char preferredMac[PREFERRED_SCALE_MAC_CAPACITY];
  copyPreferredScaleMac(preferredMac, sizeof(preferredMac));
  const bool hasMac =
      preferredMac[0] != '\0' && validPreferredScaleMac(preferredMac);
  const bool useDirected = shouldUseDirectedScaleScan(cacheMode, hasMac);
  const bool logAttempt =
      !connectAttemptSeriesActive ||
      elapsedMs(lastConnectLogMs) >= SCALE_CONNECT_LOG_MS;
  if (logAttempt) {
    lastConnectLogMs = lastScanCycleMs;
    connectAttemptSeriesActive = true;
    if (useDirected) {
      serialTracef(LogLevel::INFO, "Scanning for preferred scale %s...",
                   preferredMac);
    } else {
      serialTrace(LogLevel::INFO,
                  "Scanning for any compatible scale (name scan)");
    }
    addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECTING);
  }
  if (scale.startScan(useDirected ? preferredMac : nullptr)) {
    scanSessionAtMs = lastScanCycleMs;
    return;
  }

  connectRetryMs = nextScaleConnectRetryMs(connectRetryMs);
  if (useDirected) {
    serialTrace(LogLevel::WARNING, "Preferred scale scan failed to start");
  } else if (logAttempt) {
    logScaleConnectionFailed(false);
  }
  updateWorkerLinkState();
  setScaleLinkState(ScaleLinkState::DISCONNECTED);
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
  uint32_t lastScanCycleMs = 0;
  uint32_t lastConnectLogMs = 0;
  uint32_t connectRetryMs = SCALE_CONNECT_RETRY_MS;
  bool connectAttemptSeriesActive = false;
  uint32_t scanSessionAtMs = 0;
  uint32_t telemetryAtMs = 0;

  if (!subscribeCurrentTaskToWatchdog()) {
    reportTaskWatchdogFault();
  }

  for (;;) {
    // The control task treats a connected link with no worker progress as
    // unavailable, preventing stale prediction data from ending a shot.
    markScaleWorkerProgress();
    BLE.poll();

    // Packet timeout / remote-drop detection must not wait behind beeps or
    // queued commands. Bookoo has no heartbeat; silence is the only watchdog.
    if (scale.isConnected()) {
      connectAttemptSeriesActive = false;
      connectRetryMs = SCALE_CONNECT_RETRY_MS;
      serviceScaleWorkerLink();
    } else if (getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED) {
      updateWorkerLinkState();
      setScaleLinkState(ScaleLinkState::DISCONNECTED);
    }

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
        } else if (!scale.isConnected() && !applyScaleDiscoveryPause()) {
          serviceScaleWorkerDiscovery(lastScanCycleMs, lastConnectLogMs,
                                      connectRetryMs,
                                      connectAttemptSeriesActive,
                                      scanSessionAtMs);
        }
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

  if (xTaskCreatePinnedToCore(scaleWorkerTask, "scale_worker", 8192, nullptr,
                              tskIDLE_PRIORITY + 1, &scaleWorkerTaskHandle,
                              CONTROL_TASK_CORE) != pdPASS) {
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
          serialTrace(LogLevel::WARNING,
                      "Invalid scale weight event ignored");
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
        emitCommandAlert(event.usedCombinedTareStart ? AlertEvent::TARE_START
                                                     : AlertEvent::START_TIMER,
                         event.commandAttempted, event.writeSucceeded);
        serialTracef(LogLevel::DEBUG, "Remote timer start write: %s",
                     event.writeSucceeded ? "successful" : "failed/skipped");
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
        emitCommandAlert(AlertEvent::TARE, event.commandAttempted,
                         event.writeSucceeded);
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
        emitCommandAlert(AlertEvent::STOP_TIMER, event.commandAttempted,
                         event.writeSucceeded);
        // Structured SCALE_TIMER_STOP_* events cover Serial when enabled.
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
#if defined(SHOT_STOPPER_HOST_TEST)
  // Host tests mutate RuntimeConfig brew fields directly; keep the active
  // preset (compose source of truth) aligned before snapshotting a cycle.
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
#endif
  session.config = snapshotConfig(effectiveRuntimeConfig());
  session.activePresetId = presetBank.activeId;
  session.endReason = EndReason::NONE;
}

void maybeRequestNtpSyncOnActivity();
bool beginRinseCycle(ControlSource source, uint32_t webSessionId = 0,
                     uint32_t controlLeaseId = 0);
void enterRinse();

void armNoScaleShotGuard() {
  if (noScaleShotGuardArmed) {
    return;
  }
  noScaleShotGuardArmed = true;
  addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_ARMED);
}

void consumeNoScaleShotGuard() {
  noScaleShotGuardArmed = false;
  noScaleShotGuardActivityAtMs = millis();
  addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED);
}

bool noScaleShotGuardWouldBlock() {
  const RuntimeConfig effective = effectiveRuntimeConfig();
  const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
  const bool scaleUsable =
      scaleLinkAvailable(scaleLink) && currentWeightIsFresh();
  return runtimeConfig.avoidBbwShotWithoutScale && !effective.timerOnly &&
         !scaleUsable && noScaleShotGuardArmed;
}

void maybeEmitManualNoScaleBeep() {
  if (!runtimeConfig.buzzerManualNoScaleBeep) {
    return;
  }
  const RuntimeConfig effective = effectiveRuntimeConfig();
  if (effective.timerOnly) {
    return;
  }
  const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
  const bool scaleUsable =
      scaleLinkAvailable(scaleLink) && currentWeightIsFresh();
  if (scaleUsable) {
    return;
  }
  emitAlert(AlertEvent::MANUAL_NO_SCALE);
}

void blockNoScaleShotGuard() {
  noScaleShotGuardHold = false;
  consumeNoScaleShotGuard();
  addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED);
}

void serviceNoScaleShotGuard() {
  const bool available = scaleAvailable();
  if (available && !noScaleShotGuardScaleWasAvailable) {
    armNoScaleShotGuard();
  }
  noScaleShotGuardScaleWasAvailable = available;
  if (noScaleShotGuardHold) {
    if (!noScaleShotGuardWouldBlock()) {
      noScaleShotGuardHold = false;
    } else if (rawPaddleOn &&
               elapsedMs(noScaleShotGuardHoldAtMs) >
                   runtimeConfig.rinseGestureMs) {
      blockNoScaleShotGuard();
    }
  }
  if (!noScaleShotGuardArmed && !session.active &&
      noScaleShotGuardActivityAtMs != 0 &&
      elapsedMs(noScaleShotGuardActivityAtMs) >=
          runtimeConfig.lastShotCooldownMs) {
    armNoScaleShotGuard();
  }
}

void beginCycle(ControlSource source = ControlSource::PHYSICAL,
                uint32_t webSessionId = 0,
                uint32_t controlLeaseId = 0) {
  maybeEmitManualNoScaleBeep();
  if (noScaleShotGuardWouldBlock()) {
    if (source == ControlSource::PHYSICAL) {
      noScaleShotGuardHold = true;
      noScaleShotGuardHoldAtMs = millis();
      return;
    }
    blockNoScaleShotGuard();
    return;
  }
  noScaleShotGuardHold = false;

  flushPendingScaleTimerStopNow();
  cancelPendingFinalize("Previous drip analysis cancelled by a new cycle");

  resetSessionForNewCycle(source, webSessionId, controlLeaseId);
  session.startedAtMs = millis();
  session.cn9ClosedAtMs = 0;
  session.scaleStartLagCaptured = false;
  session.scaleStartLagMs = 0;
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

  if (!setCn9Closed(true, session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  session.cn9ClosedAtMs = getRelaySafetySnapshot().closedAtMs;

  if (session.startedWithScale) {
    emitImmediateCommandAlertIfBuzzer();
    if (!requestRemoteTimerStart()) {
      session.automaticEnabled = false;
      session.scaleWasLost = true;
      serialTrace(LogLevel::WARNING,
                  "Scale start command unavailable; cycle marked manual");
    }
  }

  enterBrewOrManualFromStart();

  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_STARTED,
                weightToCentigrams(
                    static_cast<float>(session.config.goalWeightG)),
                weightToCentigrams(session.config.weightOffsetG));
  maybeRequestNtpSyncOnActivity();
}

void finalizeCycle(EndReason reason, StopperState nextState) {
  const RelaySafetySnapshot relayBeforeOpen = getRelaySafetySnapshot();
  const uint32_t durationMs = elapsedMs(relayBeforeOpen.closedAtMs);

  // Physical flow always stops before the non-blocking BLE command is queued.
  setCn9Closed(false);
  stopPulseTrains();
  cancelScaleBrewBeep(session.id);
  cancelScaleCompletionBeep();
  session.endReason = reason;
  scheduleScaleTimerStopAfterCycle();
  if (reason == EndReason::AUTO_TO_MANUAL_GUARD &&
      runtimeConfig.buzzerAutoToManualGuardEndBeep) {
    emitAlert(AlertEvent::ATM_END);
  }
  emitImmediateCommandAlertIfBuzzer();
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
  persistLastShotFromEndedCycle(reason, durationMs);
  if (reason != EndReason::RINSE_COMPLETE &&
      stopperState != StopperState::RINSE) {
    noScaleShotGuardActivityAtMs = millis();
  }
  session.active = false;
  virtualPaddleOn = false;
  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_ENDED,
                static_cast<int32_t>(reason));
  transitionTo(nextState);
}

bool beginRinseCycle(ControlSource source, uint32_t webSessionId,
                     uint32_t controlLeaseId) {
  flushPendingScaleTimerStopNow();
  cancelPendingFinalize(source == ControlSource::WEB
                            ? "Previous drip analysis cancelled by a web rinse"
                            : "Previous drip analysis cancelled by a rinse");
  resetSessionForNewCycle(source, webSessionId, controlLeaseId);
  session.startedAtMs = millis();
  session.cn9ClosedAtMs = 0;
  session.scaleStartLagCaptured = false;
  session.scaleStartLagMs = 0;
  session.firstDropMs = 0;
  session.retareFlowFirstDetectedAtMs = 0;
  session.scaleBaselineReady = false;
  session.scaleBaselineG = 0.0f;
  session.weightSequenceAtStart = currentWeightSequence;
  session.startedWithScale = scaleAvailable();
  session.automaticEnabled = false;
  virtualPaddleOn = false;
  resetShotTrajectory(session.startedAtMs);
  if (!setCn9Closed(true, session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    transitionTo(StopperState::REQUIRES_OFF);
    return false;
  }
  session.cn9ClosedAtMs = getRelaySafetySnapshot().closedAtMs;
  if (session.startedWithScale) {
    emitImmediateCommandAlertIfBuzzer();
    if (!requestRemoteTimerStart()) {
      session.startedWithScale = false;
      session.automaticEnabled = false;
      session.scaleWasLost = true;
      serialTrace(LogLevel::WARNING,
                  "Scale start command unavailable; rinse marked manual");
    }
  }
  return true;
}

void enterRinse() {
  // The duration is measured from the beginning of the stable OFF level, not
  // from 30 ms later when debounce accepts the transition.
  session.rinseStartedAtMs = rawPaddleChangedAtMs;
  session.automaticEnabled = false;
  session.autoToManualGuardArmed = false;
  session.autoToManualGuardEnforced = false;
  addDebugEvent(DebugCategory::STATE, DebugCode::RINSE_CLASSIFIED);
  transitionTo(StopperState::RINSE);
  maybeRequestNtpSyncOnActivity();
}

void armAutoToManualGuardForAutomaticBrew() {
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
      session.config.autoToManualGuardSamplesDs,
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

void enterBrewOrManualFromStart() {
  if (session.config.timerOnly && session.startedWithScale) {
    addDebugEvent(DebugCategory::STATE, DebugCode::TIMER_ONLY_BREW_STARTED);
    transitionTo(StopperState::BREW);
    return;
  }
  if (session.automaticEnabled) {
    shot.automaticBrew = true;
    armAutoToManualGuardForAutomaticBrew();
    addDebugEvent(DebugCategory::STATE, DebugCode::BREW_STARTED);
    transitionTo(StopperState::BREW);
    return;
  }
  session.automaticEnabled = false;
  session.retareEnded = true;
  session.bbwProtectionEnded = true;
  addDebugEvent(DebugCategory::STATE, DebugCode::MANUAL_CYCLE_STARTED);
  transitionTo(StopperState::MANUAL_NO_SCALE);
}

void demoteActiveCycleToRinseOrEnd() {
  if (withinRinseGestureWindow()) {
    enterRinse();
    return;
  }
  if (session.automaticEnabled) {
    shot.automaticBrew = true;
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
  const bool directStopHonored =
      directStopFresh &&
      (session.directStopReason == EndReason::WEIGHT_ANOMALY ||
       session.weightControlState == WeightControlState::ACTIVE);
  if (directStopHonored) {
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
  }

  if (slowExtractionGuardSession() && !session.extractionExtended &&
      !session.slowExtractionExtended && maxBrewTimeReached() &&
      !targetWeightReached(session.lastAcceptedWeightG)) {
    if (minRecoveryWeightReached(session.lastAcceptedWeightG)) {
      session.directStopPending = true;
      session.directStopReason = EndReason::SLOW_EXTRACTION_MAX_TIME;
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::SLOW_EXTRACTION_STOP_MAX_TIME,
                    static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
      return true;
    }
    enterSlowExtractionExtended(session.lastAcceptedWeightG, millis());
    return false;
  }

  if (!session.receivedFreshWeightInCycle ||
      session.weightControlState != WeightControlState::ACTIVE ||
      currentWeightSequence == session.weightSequenceAtStart ||
      scaleAutomationUnavailableForSession()) {
    return false;
  }
  const float elapsedS = cycleElapsedSeconds();
  if (elapsedS < shot.expectedEndS) {
    return false;
  }

  if (fastExtractionGuardSession() && !minBrewTimeReached() &&
      !session.extractionExtended) {
    enterFastExtractionExtended(session.lastAcceptedWeightG, millis());
    return false;
  }

  if (session.extractionExtended && fastExtractionGuardSession()) {
    session.directStopPending = true;
    session.directStopReason = EndReason::FAST_EXTRACTION_MAX_WEIGHT;
    addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_STOP_MAX,
                  static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f));
    return true;
  }

  if (session.slowExtractionExtended && slowExtractionGuardSession()) {
    session.directStopPending = true;
    session.directStopReason = EndReason::SLOW_EXTRACTION_MIN_WEIGHT;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SLOW_EXTRACTION_STOP_MIN_WEIGHT,
                  static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f));
    return true;
  }

  session.directStopPending = true;
  session.directStopReason = EndReason::SCALE_THRESHOLD;
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_THRESHOLD_CONFIRMED,
                static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f));
  return true;
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
      if (paddleTurnedOff && noScaleShotGuardHold &&
          elapsedMs(noScaleShotGuardHoldAtMs) <=
              runtimeConfig.rinseGestureMs) {
        noScaleShotGuardHold = false;
        armNoScaleShotGuard();
        if (!beginRinseCycle(ControlSource::PHYSICAL)) {
          return;
        }
        enterRinse();
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
      // Early paddle OFF demotes the brew to a rinse; otherwise ends the shot.
      if (paddleTurnedOff) {
        demoteActiveCycleToRinseOrEnd();
        return;
      }

      expirePostTareBaselineIfNeeded();
      serviceBbwProtectionPhases();

      if ((session.weightControlState == WeightControlState::ACTIVE ||
           session.weightControlState == WeightControlState::VALIDATING) &&
          scaleAutomationUnavailableForSession()) {
        suspendWeightControl();
        weightStreamState = WeightStreamState::STALE;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_STREAM_STALE);
        serialTrace(LogLevel::WARNING, "Scale stream suspended during brew");
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
                                     : EndReason::SCALE_THRESHOLD;
        finalizeCycle(reason,
                      StopperState::REQUIRES_OFF);
      }
      return;

    case StopperState::MANUAL_NO_SCALE:
      if (paddleTurnedOff) {
        demoteActiveCycleToRinseOrEnd();
        return;
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
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  return stopperState == StopperState::READY && !session.active &&
         !relay.closed && !paddleOn && !rawPaddleOn &&
         !maintenanceLease.active && !safetyResetStatus.recoveryRequired &&
         relay.state != RelaySafetyState::LOCKOUT;
}

void beginWebRinse(uint32_t webSessionId, uint32_t controlLeaseId) {
  if (!beginRinseCycle(ControlSource::WEB, webSessionId, controlLeaseId)) {
    return;
  }
  session.rinseStartedAtMs = session.startedAtMs;
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
    const WebCommandType forwardedType = maintenanceLease.command.type;
    if (forwardedType == WebCommandType::SAVE_NETWORK ||
        forwardedType == WebCommandType::FORGET_NETWORK ||
        forwardedType == WebCommandType::CHANGE_AP_PASSWORD ||
        forwardedType == WebCommandType::RESET_AP_PASSWORD ||
        forwardedType == WebCommandType::RESET_NETWORK_UI ||
        forwardedType == WebCommandType::RESTART) {
      hostLastFlushedRuntime = runtimeConfig;
      hostLastFlushedPresets = presetBank;
      hostLastFlushIncludedLive = true;
      ++hostRuntimePersistAttempts;
      if (!hostRuntimePersistSucceeds) {
        result.succeeded = false;
        runtimePersistFailed = true;
        runtimePersistPending = true;
        runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
      } else {
        runtimePersistPending = false;
        runtimePersistReasonBits = 0;
        runtimePersistFailed = false;
      }
    }
    completeMaintenanceLease(result);
#endif
  }
}

void queueRuntimePersist(int32_t reasonBits) {
  runtimePersistPending = true;
  runtimePersistReasonBits |= reasonBits;
  runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_DEBOUNCE_MS;
}

void commitLiveRuntimeConfig(const RuntimeConfig &composed, int32_t reasonBits) {
  runtimeConfig = composed;
  serialLogLevel = runtimeConfig.serialDebugOutput ? LogLevel::INFO
                                                   : LogLevel::NONE;
  ringRetainLogLevel =
      static_cast<LogLevel>(runtimeConfig.ringRetainLogLevel);
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                static_cast<int32_t>(runtimeConfig.revision));
  requestBookooSilenceIfConfigured();
  queueRuntimePersist(reasonBits);
#ifndef SHOT_STOPPER_HOST_TEST
  networkManager.syncLiveRuntime(runtimeConfig, &presetBank);
#endif
}

#ifndef SHOT_STOPPER_HOST_TEST
void settingsPersistTask(void *parameter) {
  (void)parameter;
  if (!subscribeCurrentTaskToWatchdog()) {
    reportTaskWatchdogFault();
  }
  SettingsPersistRequest request;
  for (;;) {
    // Must not block forever: this task is subscribed to the TWDT.
    if (xQueueReceive(settingsPersistQueue, &request,
                      pdMS_TO_TICKS(SETTINGS_PERSIST_IDLE_WAIT_MS)) !=
        pdTRUE) {
      (void)feedCurrentTaskWatchdog();
      continue;
    }
    yieldSettingsNvs();
    (void)feedCurrentTaskWatchdog();
    const bool ok = savePersistedSettings(request.blob);
    (void)feedCurrentTaskWatchdog();
    portENTER_CRITICAL(&settingsPersistMux);
    settingsPersistResultReady = true;
    settingsPersistResultOk = ok;
    settingsPersistResultRuntimeRevision = request.runtimeRevision;
    settingsPersistResultStorageRevision =
        ok ? request.blob.storageRevision : 0;
    portEXIT_CRITICAL(&settingsPersistMux);
  }
}

void serviceSettingsPersistResult() {
  bool ready = false;
  bool ok = false;
  uint32_t runtimeRevision = 0;
  uint32_t storageRevision = 0;
  portENTER_CRITICAL(&settingsPersistMux);
  ready = settingsPersistResultReady;
  if (ready) {
    settingsPersistResultReady = false;
    ok = settingsPersistResultOk;
    runtimeRevision = settingsPersistResultRuntimeRevision;
    storageRevision = settingsPersistResultStorageRevision;
    settingsPersistInFlight = false;
  }
  portEXIT_CRITICAL(&settingsPersistMux);
  if (!ready) {
    return;
  }
  if (ok) {
    persistedSettings.storageRevision = storageRevision;
    networkManager.syncDurableStorageRevision(storageRevision);
    runtimePersistFailed = false;
    addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
                  static_cast<int32_t>(runtimeRevision));
    char liveMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
    char liveName[PREFERRED_SCALE_NAME_CAPACITY] = {};
    ScaleHistoryEntry liveHistory[SCALE_HISTORY_CAPACITY] = {};
    copyPreferredScaleMac(liveMac, sizeof(liveMac));
    copyPreferredScaleName(liveName, sizeof(liveName));
    copyScaleHistory(liveHistory);
    if (strncmp(persistedSettings.preferredScaleMac, liveMac,
                PREFERRED_SCALE_MAC_CAPACITY) == 0 &&
        strncmp(persistedSettings.preferredScaleName, liveName,
                PREFERRED_SCALE_NAME_CAPACITY) == 0 &&
        memcmp(persistedSettings.scaleHistory, liveHistory,
               sizeof(liveHistory)) == 0) {
      portENTER_CRITICAL(&scalePreferredMacMux);
      scalePreferredMacDirty = false;
      portEXIT_CRITICAL(&scalePreferredMacMux);
    } else {
      runtimePersistPending = true;
      runtimePersistReasonBits |= RUNTIME_PERSIST_REASON_SCALE_MAC;
    }
    if (runtimeConfig.revision != runtimeRevision) {
      runtimePersistPending = true;
      runtimePersistRetryAtMs = millis();
    } else if (!runtimePersistPending) {
      runtimePersistReasonBits = 0;
    }
  } else {
    addDebugEvent(DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
                  static_cast<int32_t>(runtimeRevision),
                  runtimePersistReasonBits);
    runtimePersistFailed = true;
    runtimePersistPending = true;
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
}

bool dispatchSettingsPersist() {
  if (settingsPersistQueue == nullptr || settingsPersistInFlight) {
    if (settingsPersistQueue == nullptr) {
      runtimePersistFailed = true;
    }
    return false;
  }
  SettingsPersistRequest request;
  request.blob = networkManager.settingsCopy();
  request.blob.runtime = runtimeConfig;
  request.blob.presets = presetBank;
  copyPreferredScaleMac(request.blob.preferredScaleMac,
                        sizeof(request.blob.preferredScaleMac));
  copyPreferredScaleName(request.blob.preferredScaleName,
                         sizeof(request.blob.preferredScaleName));
  copyScaleHistory(request.blob.scaleHistory);
  memcpy(persistedSettings.preferredScaleMac, request.blob.preferredScaleMac,
         sizeof(persistedSettings.preferredScaleMac));
  memcpy(persistedSettings.preferredScaleName, request.blob.preferredScaleName,
         sizeof(persistedSettings.preferredScaleName));
  memcpy(persistedSettings.scaleHistory, request.blob.scaleHistory,
         sizeof(persistedSettings.scaleHistory));
  request.runtimeRevision = runtimeConfig.revision;
  if (xQueueSend(settingsPersistQueue, &request, 0) != pdTRUE) {
    return false;
  }
  portENTER_CRITICAL(&settingsPersistMux);
  settingsPersistInFlight = true;
  portEXIT_CRITICAL(&settingsPersistMux);
  runtimePersistPending = false;
  return true;
}
#endif

void serviceRuntimePersistence() {
#ifndef SHOT_STOPPER_HOST_TEST
  serviceSettingsPersistResult();
  bool inFlight = false;
  portENTER_CRITICAL(&settingsPersistMux);
  inFlight = settingsPersistInFlight;
  portEXIT_CRITICAL(&settingsPersistMux);
  if (!runtimePersistPending || inFlight || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow()) {
    return;
  }
  if (!dispatchSettingsPersist()) {
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
#else
  if (!runtimePersistPending || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow()) {
    return;
  }
  ++hostRuntimePersistAttempts;
  if (!hostRuntimePersistSucceeds) {
    addDebugEvent(DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
                  static_cast<int32_t>(runtimeConfig.revision),
                  runtimePersistReasonBits);
    runtimePersistFailed = true;
    runtimePersistPending = true;
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
    return;
  }
  hostLastFlushedRuntime = runtimeConfig;
  hostLastFlushedPresets = presetBank;
  hostLastFlushIncludedLive = true;
  runtimePersistPending = false;
  runtimePersistReasonBits = 0;
  runtimePersistFailed = false;
#endif
}

void completeMaintenanceLease(const WebCommand &result) {
  if (!maintenanceLease.active ||
      result.maintenanceLeaseId != maintenanceLease.id) {
    addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                  static_cast<int32_t>(result.type));
    return;
  }
  if (result.succeeded && maintenanceLease.applyRuntimeOnSuccess) {
    // Live runtime is apply-first. Never copy persist results over RAM.
    addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                  static_cast<int32_t>(runtimeConfig.revision));
    if (maintenanceLease.command.type == WebCommandType::PERSIST_RUNTIME) {
      runtimePersistReasonBits = 0;
    }
    requestBookooSilenceIfConfigured();
  }
  if (!result.succeeded &&
      maintenanceLease.command.type == WebCommandType::PERSIST_RUNTIME &&
      maintenanceLease.id == runtimePersistRequestId) {
    addDebugEvent(
        DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
        static_cast<int32_t>(maintenanceLease.command.config.revision),
        runtimePersistReasonBits);
    // RUNTIME_PERSIST_FAILED already formats a detailed Serial line.
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
      // Machine/workflow fields only. Recipe authority stays on the active preset.
      RuntimeConfig candidate = runtimeConfig;
      candidate.autoTare = command.config.autoTare;
      candidate.canTareStartTimer = command.config.canTareStartTimer;
      candidate.scaleTimerStopExtraDelayMs =
          command.config.scaleTimerStopExtraDelayMs;
      candidate.firstDropBeep = command.config.firstDropBeep;
      candidate.paddleReturnReminderBeep = command.config.paddleReturnReminderBeep;
      candidate.paddleReturnReminderIntervalMs =
          command.config.paddleReturnReminderIntervalMs;
      candidate.paddleReturnReminderMaxDurationMs =
          command.config.paddleReturnReminderMaxDurationMs;
      candidate.buzzerScaleLostBeep = command.config.buzzerScaleLostBeep;
      candidate.buzzerAutoToManualGuardEndBeep =
          command.config.buzzerAutoToManualGuardEndBeep;
      candidate.buzzerManualNoScaleBeep = command.config.buzzerManualNoScaleBeep;
      candidate.buzzerScaleConnectedBeep =
          command.config.buzzerScaleConnectedBeep;
      candidate.buzzerExtendedPulseRate = command.config.buzzerExtendedPulseRate;
      candidate.alertOutputChannel = command.config.alertOutputChannel;
      candidate.bookooMuteOnBuzzerOnly = command.config.bookooMuteOnBuzzerOnly;
      candidate.bookooConnectBeepLevel = command.config.bookooConnectBeepLevel;
      candidate.rinseGestureMs = command.config.rinseGestureMs;
      candidate.rinseDurationMs = command.config.rinseDurationMs;
      candidate.autoRetare = command.config.autoRetare;
      candidate.retareWindowMs = command.config.retareWindowMs;
      candidate.minimumCupWeightG = command.config.minimumCupWeightG;
      candidate.retareStabilitySamples = command.config.retareStabilitySamples;
      candidate.retareStabilityToleranceG =
          command.config.retareStabilityToleranceG;
      candidate.retareStabilityMaxGapMs = command.config.retareStabilityMaxGapMs;
      candidate.retareStabilityMinDurationMs =
          command.config.retareStabilityMinDurationMs;
      candidate.timezoneOffsetMinutes = command.config.timezoneOffsetMinutes;
      candidate.ntpServerPreset = command.config.ntpServerPreset;
      memcpy(candidate.ntpServerCustom, command.config.ntpServerCustom,
             sizeof(candidate.ntpServerCustom));
      if (candidate.scaleMacCacheMode != command.config.scaleMacCacheMode) {
                     serialTracef(LogLevel::INFO, "Paired scale lock: %s",
                     scaleMacCacheModeId(command.config.scaleMacCacheMode));
      }
      candidate.scaleMacCacheMode = command.config.scaleMacCacheMode;
      candidate.avoidBbwShotWithoutScale =
          command.config.avoidBbwShotWithoutScale;
      candidate.lastShotCooldownMs = command.config.lastShotCooldownMs;
      candidate.serialDebugOutput = command.config.serialDebugOutput;
      candidate.ringRetainLogLevel = command.config.ringRetainLogLevel;
      // Session Manual switch may arrive via brewByWeight on Home; keep as timerOnly.
      candidate.timerOnly = command.config.timerOnly;
#if defined(SHOT_STOPPER_HOST_TEST)
      // Host APPLY_CONFIG still carries brew fields; update the active recipe.
      {
        ensureShotPresetBank(presetBank, candidate.retareWindowMs,
                             candidate.autoRetare);
        ShotPreset &preset = mutableActiveShotPreset(presetBank);
        copyUserRecipeFromConfig(command.config, preset);
      }
#endif
      RuntimeConfig composed = composeEffectiveConfig(candidate, presetBank);
      if (validateRuntimeConfig(composed) != ConfigValidationError::NONE) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        rejectWebCommand(command);
        return;
      }
      composed.revision = runtimeConfig.revision + 1;
      if (composed.revision == 0) {
        composed.revision = 1;
      }
      ensureShotPresetBank(presetBank, composed.retareWindowMs,
                           composed.autoRetare);
      commitLiveRuntimeConfig(composed, RUNTIME_PERSIST_REASON_USER);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::RESET_WEIGHT_OFFSET: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      pendingFinalize.pending = false;
      ShotPreset &preset = mutableActiveShotPreset(presetBank);
      preset.weightOffsetG = preset.weightOffsetBaselineG;
      RuntimeConfig candidate = runtimeConfig;
      applyShotPresetToConfig(preset, candidate, true);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::RESET_AUTO_TO_MANUAL_GUARD_SAMPLES: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      pendingFinalize.pending = false;
      ShotPreset &preset = mutableActiveShotPreset(presetBank);
      resetAutoToManualGuardSamples(preset.autoToManualGuardSamplesDs,
                                    preset.autoToManualGuardBaselineMs);
      RuntimeConfig candidate = runtimeConfig;
      applyShotPresetToConfig(preset, candidate, true);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      addDebugEvent(DebugCategory::CONFIG,
                    DebugCode::AUTO_TO_MANUAL_GUARD_SAMPLES_RESET);
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::PRESET_OP: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      bool ok = false;
      uint8_t newId = 0;
      const PresetAction action = static_cast<PresetAction>(command.presetAction);
      switch (action) {
        case PresetAction::APPLY:
          ok = setActiveShotPreset(presetBank, command.presetId);
          break;
        case PresetAction::SAVE: {
          ShotPreset *preset = mutableShotPreset(presetBank, command.presetId);
          if (preset == nullptr) {
            ok = false;
            break;
          }
          ShotPreset candidateRecipe = *preset;
          copyUserRecipeFromConfig(command.config, candidateRecipe);
          // Form sends brewByWeight explicitly; do not use session Manual.
          candidateRecipe.brewByWeight = !command.config.timerOnly;
          ok = validateShotPresetRecipe(candidateRecipe,
                                        runtimeConfig.retareWindowMs,
                                        runtimeConfig.autoRetare);
          if (ok) {
            // Preserve learned fields from the live preset.
            candidateRecipe.weightOffsetG = preset->weightOffsetG;
            memcpy(candidateRecipe.autoToManualGuardSamplesDs,
                   preset->autoToManualGuardSamplesDs,
                   sizeof(candidateRecipe.autoToManualGuardSamplesDs));
            *preset = candidateRecipe;
            presetBank.activeId = preset->id;
          }
          break;
        }
        case PresetAction::CREATE:
          ok = createUntitledShotPreset(presetBank, newId);
          break;
        case PresetAction::DELETE:
          ok = deleteShotPreset(presetBank, command.presetId);
          break;
        case PresetAction::DUPLICATE:
          ok = duplicateShotPreset(presetBank, command.presetId, newId);
          break;
        case PresetAction::RENAME:
          ok = renameShotPreset(presetBank, command.presetId, command.presetName);
          break;
        case PresetAction::RESTORE_FACTORY_VALUES:
          ok = restoreFactoryShotPresetValues(presetBank, command.presetId);
          break;
      }
      if (!ok) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED,
                      static_cast<int32_t>(command.presetAction));
        rejectWebCommand(command);
        return;
      }
      ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                           runtimeConfig.autoRetare);
      RuntimeConfig candidate = runtimeConfig;
      applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::SAVE_NETWORK:
    case WebCommandType::FORGET_NETWORK:
    case WebCommandType::CHANGE_AP_PASSWORD:
    case WebCommandType::RESET_AP_PASSWORD:
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

    case WebCommandType::CLEAR_PREFERRED_SCALE:
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      clearPreferredScaleCache();
      servicePreferredScaleMacPersistence();
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::SELECT_PREFERRED_SCALE:
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      if (command.scaleSelectMac[0] == '\0') {
        clearPreferredScaleCache();
      } else if (!validPreferredScaleMac(command.scaleSelectMac)) {
        rejectWebCommand(command);
        return;
      } else {
        selectPreferredScale(command.scaleSelectMac, command.scaleSelectName);
      }
      servicePreferredScaleMacPersistence();
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::START_WIFI_SCAN:
      if (!beginMaintenanceLease(command, false)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::BUZZER_TEST: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      const bool accepted =
          buzzerPatternIsPulseTrain(command.buzzerPattern)
              ? startPulseTrain(command.buzzerPattern,
                                BUZZER_PULSE_TRAIN_DEBUG_MS)
              : localBuzzer.request(command.buzzerPattern);
      if (!accepted) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::BOOKOO_DEBUG: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      if (!enqueueScaleDebugCommand(command.bookooDebugAction,
                                    command.bookooBeepLevel)) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

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
  next.currentTimerValid = next.scaleAvailable && scaleLink.timerValid;
  next.currentTimerMs = next.currentTimerValid ? scaleLink.timerMs : 0;
  next.currentTimerAgeMs = next.currentTimerValid ? scaleLink.timerAgeMs : 0;
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
  next.config = effectiveRuntimeConfig();
  next.presets = presetBank;
  next.lastCycle = lastCycle;
  next.lastShot = persistedLastShot;
  strncpy(next.scaleProtocol, scaleLink.protocolName,
          sizeof(next.scaleProtocol) - 1);
  next.scaleProtocol[sizeof(next.scaleProtocol) - 1] = '\0';
  copyPreferredScaleMac(next.preferredScaleMac, sizeof(next.preferredScaleMac));
  copyPreferredScaleName(next.preferredScaleName,
                         sizeof(next.preferredScaleName));
  copyScaleHistory(next.scaleHistory);
  next.scaleMacCachePauseRemainingMs = scaleMacCachePauseRemainingMs(now);
  if (session.active) {
    next.cycleFlowDuringRetare = session.flowDuringRetare;
    next.cycleRetarePerformed = session.retarePerformed;
    next.cycleStartedWithScale = session.startedWithScale;
    next.cycleAutomaticBrew = shot.automaticBrew;
    next.cycleTimerOnly = session.config.timerOnly;
    next.cycleFirstDropMs = session.firstDropMs;
    next.cycleRetareFlowFirstDetectedAtMs =
        session.retareFlowFirstDetectedAtMs;
    next.cycleStartedAtMs =
        relay.closed ? relay.closedAtMs : 0U;
    next.cycleElapsedMs = cycleShotElapsedMs();
    next.cycleExtractionExtended =
        session.extractionExtended && fastExtractionGuardSession();
    next.cycleSlowExtractionExtended =
        session.slowExtractionExtended && slowExtractionGuardSession();
    next.cycleTargetReachedEarly = session.targetReachedEarly;
    if (next.cycleExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.maxRecoveryWeightG;
      const uint32_t elapsed = next.cycleElapsedMs;
      next.cycleMinBrewTimeRemainingMs =
          elapsed >= session.config.minBrewTimeMs
              ? 0U
              : session.config.minBrewTimeMs - elapsed;
    } else if (next.cycleSlowExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.minRecoveryWeightG;
      next.cycleMinBrewTimeRemainingMs = 0;
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
  {
    const ShotPreset &active = activeShotPreset(presetBank);
    next.autoToManualGuardTrendMs = autoToManualGuardTrendMs(
        active.autoToManualGuardSamplesDs, active.operationalWallMs);
  }
  next.noScaleShotGuardEnabled = runtimeConfig.avoidBbwShotWithoutScale;
  next.noScaleShotGuardArmed = noScaleShotGuardArmed;
  next.configPersistPending = runtimePersistPending;
  next.configPersistFailed = runtimePersistFailed;
#ifndef SHOT_STOPPER_HOST_TEST
  portENTER_CRITICAL(&settingsPersistMux);
  next.configPersistPending =
      runtimePersistPending || settingsPersistInFlight ||
      settingsPersistResultReady;
  portEXIT_CRITICAL(&settingsPersistMux);
#endif
  portENTER_CRITICAL(&debugLogMux);
  next.debugEventsDropped = debugLog.overwritten();
  portEXIT_CRITICAL(&debugLogMux);
  portENTER_CRITICAL(&webStatusMux);
  publishedControlStatus = next;
  portEXIT_CRITICAL(&webStatusMux);
}

SerialCliParser serialCliParser;

void resetSerialCliState() {
  serialCliResetParser(serialCliParser);
}

void serialCliReply(const char *message) {
  Serial.println(message);
}

void serialCliWipeCommandSecrets(WebCommand &command) {
  memset(command.password, 0, sizeof(command.password));
}

void serialCliRejectUnsafe() {
  serialCliReply(
      "ERR not ready: paddle OFF, CN9 open, Ready, no active cycle");
}

void serialCliQueueCommand(WebCommand &command, SerialCliVerb verb) {
  command.requestId = millis();
  if (!enqueueWebCommand(command)) {
    serialCliWipeCommandSecrets(command);
    serialCliReply("ERR control queue full");
    return;
  }
  serialCliWipeCommandSecrets(command);
  char line[48] = {};
  snprintf(line, sizeof(line), "OK queued %s", serialCliVerbName(verb));
  serialCliReply(line);
}

void serialCliQueueIfSafe(WebCommand &command, SerialCliVerb verb) {
  if (!controlAllowsConfigurationNow()) {
    serialCliWipeCommandSecrets(command);
    serialCliRejectUnsafe();
    return;
  }
  serialCliQueueCommand(command, verb);
}

void dispatchSerialCliRequest(SerialCliRequest &request) {
  switch (request.verb) {
    case SerialCliVerb::NONE:
      return;
    case SerialCliVerb::HELP:
      serialCliPrintHelp();
      return;
    case SerialCliVerb::HELLO:
      serialCliReply("how are you");
      return;
    case SerialCliVerb::REBOOT: {
      WebCommand command;
      command.type = WebCommandType::RESTART;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::UNKNOWN:
    case SerialCliVerb::LINE_TOO_LONG:
    case SerialCliVerb::INVALID_ARGS:
      Serial.print("ERR ");
      Serial.println(request.error != nullptr ? request.error
                                              : "invalid arguments");
      return;
    case SerialCliVerb::FACTORY_RESET: {
      WebCommand command;
      command.type = WebCommandType::FACTORY_RESET;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::RESET_AP_PASSWORD: {
      WebCommand command;
      command.type = WebCommandType::RESET_AP_PASSWORD;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::SET_AP_PASSWORD: {
      WebCommand command;
      command.type = WebCommandType::CHANGE_AP_PASSWORD;
      strncpy(command.password, request.arg1, sizeof(command.password) - 1);
      serialCliQueueIfSafe(command, request.verb);
      memset(request.arg1, 0, sizeof(request.arg1));
      return;
    }
    case SerialCliVerb::SET_WIFI: {
      WebCommand command;
      command.type = WebCommandType::SAVE_NETWORK;
      strncpy(command.ssid, request.arg1, sizeof(command.ssid) - 1);
      strncpy(command.password, request.arg2, sizeof(command.password) - 1);
      command.openNetwork = request.openNetwork;
      serialCliQueueIfSafe(command, request.verb);
      memset(request.arg1, 0, sizeof(request.arg1));
      memset(request.arg2, 0, sizeof(request.arg2));
      return;
    }
    case SerialCliVerb::CLEAR_WIFI: {
      WebCommand command;
      command.type = WebCommandType::FORGET_NETWORK;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::RESET_NETWORK_UI: {
      WebCommand command;
      command.type = WebCommandType::RESET_NETWORK_UI;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::CLEAR_SHOTS: {
      if (!controlAllowsConfigurationNow()) {
        serialCliRejectUnsafe();
        return;
      }
      if (!clearShotLog()) {
        serialCliReply("ERR shot history clear failed");
        return;
      }
      serialCliReply("OK shots cleared");
      return;
    }
    case SerialCliVerb::SERIAL_DEBUG_ON:
    case SerialCliVerb::SERIAL_DEBUG_OFF: {
      const bool enable = request.verb == SerialCliVerb::SERIAL_DEBUG_ON;
      if (!enable) {
        serialCliReply("OK serial debug off");
      }
      RuntimeConfig candidate = runtimeConfig;
      candidate.serialDebugOutput = enable;
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      if (enable) {
        serialCliReply("OK serial debug on");
      }
      return;
    }
  }
}

void serviceSerialCli() {
  size_t consumed = 0;
  while (Serial.available() > 0 &&
         consumed < SERIAL_CLI_MAX_BYTES_PER_LOOP) {
    const int incoming = Serial.read();
    ++consumed;
    if (incoming < 0) {
      break;
    }
    SerialCliRequest request;
    if (serialCliFeed(serialCliParser, static_cast<char>(incoming),
                      request)) {
      dispatchSerialCliRequest(request);
    }
  }
}

// ---------------------------------------------------------------------------
// Status indication
// ---------------------------------------------------------------------------

#if SHOT_STOPPER_ENABLE_ALED == 1
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
#endif

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

  localBuzzer.begin(BUZZER_GPIO);

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

  Serial.begin(SERIAL_BAUD);
#if SHOT_STOPPER_ENABLE_ALED == 1
  statusIndicatorsReady = initializeStatusIndicators();
  updateStatusIndicators();
#endif

  persistenceReady = EEPROM.begin(EEPROM_SIZE);
#ifndef SHOT_STOPPER_HOST_TEST
  bool settingsLoaded = false;
  if (persistenceReady && loadPersistedSettings(persistedSettings)) {
    settingsLoaded = true;
    noteDurableStorageRevision(persistedSettings.storageRevision);
  } else if (persistenceReady) {
    if (initializeDefaultSettings(persistedSettings)) {
      settingsLoaded = true;
      if (!savePersistedSettings(persistedSettings)) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                      BOOT_SUBSYSTEM_SETTINGS_SAVE);
      }
    }
  }
  if (settingsLoaded) {
    runtimeConfig = persistedSettings.runtime;
    presetBank = persistedSettings.presets;
    ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                         runtimeConfig.autoRetare);
    if (validPreferredScaleMac(persistedSettings.preferredScaleMac)) {
      strncpy(scalePreferredMac, persistedSettings.preferredScaleMac,
              PREFERRED_SCALE_MAC_CAPACITY - 1);
      scalePreferredMac[PREFERRED_SCALE_MAC_CAPACITY - 1] = '\0';
      canonicalizePreferredScaleMac(scalePreferredMac,
                                    sizeof(scalePreferredMac));
    }
    if (validPreferredScaleName(persistedSettings.preferredScaleName)) {
      strncpy(scalePreferredName, persistedSettings.preferredScaleName,
              PREFERRED_SCALE_NAME_CAPACITY - 1);
      scalePreferredName[PREFERRED_SCALE_NAME_CAPACITY - 1] = '\0';
    }
    memcpy(scaleHistory, persistedSettings.scaleHistory, sizeof(scaleHistory));
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
  }
#else
  runtimeConfig = RuntimeConfig{};
  seedDefaultShotPresetBank(presetBank);
#endif
  serialLogLevel = runtimeConfig.serialDebugOutput ? LogLevel::INFO
                                                   : LogLevel::NONE;
  ringRetainLogLevel =
      static_cast<LogLevel>(runtimeConfig.ringRetainLogLevel);

  logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_RESET_REASON,
          static_cast<int32_t>(safetyResetStatus.reasonCode));
  logEmit(relaySafetyTimersReady ? LogLevel::INFO : LogLevel::CRITICAL,
          DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
          BOOT_SUBSYSTEM_RELAY_TIMERS, relaySafetyTimersReady ? 1 : 0);
  logEmit(platformClockReady ? LogLevel::INFO : LogLevel::CRITICAL,
          DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM, BOOT_SUBSYSTEM_CPU,
          platformClockReady ? 1 : 0);
  if (!platformClockReady) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_CPU);
  }
  logEmit(taskWatchdogReady ? LogLevel::INFO : LogLevel::CRITICAL,
          DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
          BOOT_SUBSYSTEM_TASK_WDT, taskWatchdogReady ? 1 : 0);
#if SHOT_STOPPER_ENABLE_ALED == 1
  if (!statusIndicatorsReady) {
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_INDICATORS, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_INDICATORS, 1);
  }
#endif
  if (safetyResetStatus.recoveryRequired) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::SAFETY_LOCKOUT_ACTIVE);
  }

#ifndef SHOT_STOPPER_HOST_TEST
  shotLog.load();
  shotLog.onBoot();
  shotLog.save();
  lastShotStore.load();
  persistedLastShot = lastShotStore.get();
#else
  shotLog.load();
  shotLog.onBoot();
  lastShotStore.load();
  persistedLastShot = lastShotStore.get();
#endif

  addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_BANNER,
                static_cast<int32_t>(shotLog.bootId()));

  if (!persistenceReady) {
    addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_PERSISTENCE);
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_PERSISTENCE, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_PERSISTENCE, 1);
  }

  addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_RUNTIME_CONFIG,
                weightToCentigrams(
                    static_cast<float>(runtimeConfig.goalWeightG)),
                weightToCentigrams(runtimeConfig.weightOffsetG));

  bleStackReady = BLE.begin();
  if (bleStackReady) {
    // ArduinoBLE defaults ATT operations to five seconds. Keep the BLE owner
    // isolated, but also bound connect/discovery/subscribe/write waits so its
    // watchdog and stream-staleness telemetry react promptly.
    BLE.setTimeout(SCALE_ATT_TIMEOUT_MS);
  }
  if (!bleStackReady) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_BLE);
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_BLE, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_BLE, 1);
  }

  if (bleStackReady && !initializeScaleWorker()) {
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SCALE_WORKER, 0);
    bleStackReady = false;
  } else if (bleStackReady) {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SCALE_WORKER, 1);
  }

  webCommandQueue =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  if (webCommandQueue == nullptr) {
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_WEB_QUEUE, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_WEB_QUEUE, 1);
  }

#ifndef SHOT_STOPPER_HOST_TEST
  settingsPersistQueue =
      xQueueCreate(1, sizeof(SettingsPersistRequest));
  if (settingsPersistQueue == nullptr ||
      xTaskCreatePinnedToCore(
          settingsPersistTask, "settings_persist", 8192, nullptr,
          tskIDLE_PRIORITY, &settingsPersistTaskHandle,
          CONTROL_TASK_CORE) != pdPASS) {
    settingsPersistTaskHandle = nullptr;
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SETTINGS_SAVE, 0);
  }
#endif

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
    callbacks.clearLastShot = clearLastShot;
    callbacks.copyPreferredScaleMac = copyPreferredScaleMac;
    callbacks.copyPreferredScaleName = copyPreferredScaleName;
    callbacks.copyScaleHistory = copyScaleHistory;
    callbacks.copyPresetBank = copyPresetBank;
    callbacks.copyRuntimeConfig = copyRuntimeConfig;
    if (!networkManager.begin(persistedSettings, callbacks)) {
      logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
              BOOT_SUBSYSTEM_NETWORK, 0);
    } else {
      logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
              BOOT_SUBSYSTEM_NETWORK, 1);
    }
  } else if (!settingsLoaded) {
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_NETWORK, 0);
  }
#endif

  addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_READY);
  firmwareInitializationComplete = true;
#if SHOT_STOPPER_ENABLE_ALED == 1
  updateStatusIndicators();
#endif
}

void serviceHealthThresholdAlerts(uint32_t intervalMaxGapMs) {
  const bool heapLow =
      freeHeapBytes > 0 &&
      (freeHeapBytes < HEALTH_HEAP_FREE_ALERT_BYTES ||
       (largestFreeHeapBlockBytes > 0 &&
        largestFreeHeapBlockBytes < HEALTH_HEAP_LARGEST_ALERT_BYTES));
  const bool heapClear =
      freeHeapBytes == 0 ||
      (freeHeapBytes >= HEALTH_HEAP_FREE_CLEAR_BYTES &&
       (largestFreeHeapBlockBytes == 0 ||
        largestFreeHeapBlockBytes >= HEALTH_HEAP_LARGEST_CLEAR_BYTES));
  if (heapLow && !healthHeapAlertLatched) {
    healthHeapAlertLatched = true;
    addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_HEAP_LOW,
                  static_cast<int32_t>(freeHeapBytes),
                  static_cast<int32_t>(largestFreeHeapBlockBytes));
  } else if (heapClear) {
    healthHeapAlertLatched = false;
  }

  uint32_t worstStackWords = UINT32_MAX;
  if (loopStackMinWords > 0 && loopStackMinWords < worstStackWords) {
    worstStackWords = loopStackMinWords;
  }
  if (scaleWorkerStackMinWords > 0 &&
      scaleWorkerStackMinWords < worstStackWords) {
    worstStackWords = scaleWorkerStackMinWords;
  }
#ifndef SHOT_STOPPER_HOST_TEST
  const uint32_t networkStack = networkManager.snapshot().taskStackMinWords;
  if (networkStack > 0 && networkStack < worstStackWords) {
    worstStackWords = networkStack;
  }
#endif
  const bool stackLow =
      worstStackWords != UINT32_MAX &&
      worstStackWords < HEALTH_STACK_MIN_ALERT_WORDS;
  const bool stackClear =
      worstStackWords == UINT32_MAX ||
      worstStackWords >= HEALTH_STACK_MIN_CLEAR_WORDS;
  if (stackLow && !healthStackAlertLatched) {
    healthStackAlertLatched = true;
    addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_STACK_LOW,
                  static_cast<int32_t>(loopStackMinWords),
                  static_cast<int32_t>(scaleWorkerStackMinWords));
  } else if (stackClear) {
    healthStackAlertLatched = false;
  }

  const bool gapHigh = intervalMaxGapMs >= HEALTH_LOOP_GAP_ALERT_MS;
  const bool gapClear = intervalMaxGapMs <= HEALTH_LOOP_GAP_CLEAR_MS;
  if (gapHigh && !healthLoopGapAlertLatched) {
    healthLoopGapAlertLatched = true;
    addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_LOOP_GAP,
                  static_cast<int32_t>(intervalMaxGapMs), 0);
  } else if (gapClear) {
    healthLoopGapAlertLatched = false;
  }
}

void loop() {
  const uint32_t loopStartedAtMs = millis();
  if (lastLoopAtMs != 0) {
    const uint32_t gap = loopStartedAtMs - lastLoopAtMs;
    if (gap > loopMaxGapMs) {
      loopMaxGapMs = gap;
    }
    if (gap > healthIntervalMaxGapMs) {
      healthIntervalMaxGapMs = gap;
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
    serviceHealthThresholdAlerts(healthIntervalMaxGapMs);
    healthIntervalMaxGapMs = 0;
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
  serviceNoScaleShotGuard();
  stateMachineTask();
  servicePaddleReturnReminder();
  serviceExtendedPulseAlert();
  localBuzzer.service(millis());
  serviceScaleCompletionBeep();
  maybeCaptureScaleStartLag();
  servicePendingScaleTimerStop();
  serviceRemoteTimerStopRetry();
  pendingShotFinalizeTask();
  serviceSerialCli();
  serviceMaintenanceCancellation();
  serviceControlCommandResult();
  processWebCommands();
  serviceControlCommandResult();
  serviceRuntimePersistence();
  servicePreferredScaleMacPersistence();
  serviceMaintenanceLease();
  publishControlStatus();
#if SHOT_STOPPER_ENABLE_ALED == 1
  updateStatusIndicators();
#endif
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
