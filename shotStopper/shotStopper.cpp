/*
  Shot Stopper for La Marzocco Micra

  An activator on ACTIVATOR_GPIO (paddle, switch, or another compatible
  mechanism) reports user intention. The stopper is the sole controller of the
  machine circuit (the intercepted brew-switch contact that makes the machine
  run) through a normally-open relay.

  Activator ON  (control closed) -> GPIO LOW
  Activator OFF (control open)   -> GPIO HIGH
  Relay de-energized             -> machine circuit open (safe state)

  LAYER: Shot stopper (orchestrator)
  ----------------------------------
  This translation unit wires brew, cup, scale, and the generic machine façade.
  It must keep responsibilities decoupled:

  - The activator reads GPIO and translates it to UserIntent / MachineIntention.
    The stopper talks to Machine only through that abstract façade
    (machinePollIntention, machineRequestStart/Stop, machineRunState,
    MachineSense, machineSetActivatorDriveAllowed). It must NOT include
    paddle/momentary specialization headers, branch on MachineType, or know
    reed/pulse/PaddleMode internals.
  - Paddle logic stays in ShotStopperMachinePaddle*; momentary in
    ShotStopperMachineMomentary*. Run state is owned only by each type's
    *State* file. Brew / cup / scale / guards likewise never talk to switch
    or paddle details — if a guard uses paddle- or momentary-specific code,
    that is a layer bug.
  - Brew, cup, and scale do not call each other; this file polls machine once
    per loop, builds GuardInputs, pushes MachineSense and activator-drive
    permission, and applies scale/cup effects. Guards never call machine.

  Author: Felipe Urzúa <cheerpipe@gmail.com>
  https://github.com/Cheerpipe/AcaiaArduinoBLE
  Released under the GNU Affero General Public License v3.0.
*/

#if defined(SHOT_STOPPER_HOST_TEST)
#include "tests/shot_stopper_host_stubs.h"
#else
#include <EspressoScaleBLE.h>
#include <EEPROM.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <math.h>
#include "ShotStopperNetwork.h"
#include "ShotStopperOta.h"
#include "ShotStopperPersistence.h"
#include "ShotStopperDurableStores.h"
#include "ShotStopperRecovery.h"
#endif

#include "ShotStopperDomain.h"
#include "ShotStopperDebugExport.h"
#include "ShotStopperBleCompanion.h"
#if !defined(SHOT_STOPPER_HOST_TEST)
#include "ShotStopperBleCompanionPersistence.h"
#endif
#include "ShotStopperBuzzer.h"
#include "ShotStopperAlert.h"
#include "ShotStopperAlertChannel.h"
#include "ShotStopperAlertTone.h"
#include "ShotStopperPresets.h"
#include "ShotStopperSerialCli.h"
#if defined(SHOT_STOPPER_USB_CONSOLE_OWN_HWCDC)
HWCDC shotStopperUsbConsole;
#endif
#include "ShotStopperVersion.h"
#include "ShotStopperHardwareTimer.h"
#include "ShotStopperResetGuard.h"
#include "ShotStopperRecoveryGesture.h"
#include "ShotStopperResetHistoryStore.h"
#include "ShotStopperSafety.h"
#include "ShotStopperShotLog.h"
#include "ShotStopperShotCurve.h"
#include "ShotStopperLastShot.h"
#include "ShotStopperTime.h"
#include "ShotStopperWatchdog.h"
#include "ShotStopperHwmon.h"
#include "ShotStopperTaskProfiler.h"
#include "ShotStopperPsram.h"
#ifndef SHOT_STOPPER_HOST_TEST
#include "ShotStopperJsonArena.h"
#endif
#include "ShotStopperHardware.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include <stdlib.h>

using namespace shotstopper;

#include "ShotStopperScaleWorker.h"
#include "ShotStopperRfCoex.h"

#if !defined(SHOT_STOPPER_HOST_TEST)
// ArduinoBLE BLEHostAlloc counters (patches/ArduinoBLE-2.1.0-ble-host-psram).
extern "C" uint32_t BLEHostAllocPsramCount(void);
extern "C" uint32_t BLEHostAllocFallbackCount(void);
extern "C" uint32_t BLEHostHciRxDropped(void);
extern "C" uint32_t BLEHostHciTxDropped(void);
#else
static inline uint32_t BLEHostAllocPsramCount(void) { return 0; }
static inline uint32_t BLEHostAllocFallbackCount(void) { return 0; }
static inline uint32_t BLEHostHciRxDropped(void) { return 0; }
static inline uint32_t BLEHostHciTxDropped(void) { return 0; }
#endif

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

constexpr uint32_t CONTROL_STATUS_REFRESH_WAIT_MS = 50;
constexpr uint32_t LOOP_NO_SCALE_DELAY_MS = 5;
constexpr uint32_t BLE_COMPANION_NO_SCALE_PUBLISH_MS = 50;
constexpr uint32_t BLE_COMPANION_RUNTIME_PUBLISH_MS = 100;
// Persist blob is PSRAM BSS, not a 3 KiB stack local. Keep headroom for
// Preferences / TWDT on the flash-writing task (stack stays internal).
constexpr uint32_t SETTINGS_PERSIST_TASK_STACK_SIZE = 4096;
constexpr uint32_t SCALE_STOP_RETRY_INTERVAL_MS = 250;
constexpr uint32_t SCALE_STOP_RETRY_WINDOW_MS = 5000;
constexpr uint8_t SCALE_STOP_MAX_ATTEMPTS = 3;
constexpr uint32_t MAINTENANCE_LEASE_SETTLE_MS = 100;
constexpr uint32_t RUNTIME_PERSIST_RETRY_MS = 500;
constexpr uint32_t SHOT_STORE_PERSIST_RETRY_MS = 500;
constexpr uint32_t RUNTIME_PERSIST_DEBOUNCE_MS = 300;
constexpr uint32_t SETTINGS_PERSIST_IDLE_WAIT_MS = 1000;
// Pin control/BLE/LED work with Arduino loopTask on APP_CPU (core 1).
// network_manager is pinned to PRO_CPU (core 0) in ShotStopperNetwork.cpp.
constexpr BaseType_t CONTROL_TASK_CORE = 1;
static_assert(SCALE_WORKER_TASK_CORE == CONTROL_TASK_CORE,
              "Scale worker must stay pinned with the control/BLE task");

static_assert(SCALE_WORKER_STALE_MS > ACTIVATOR_DEBOUNCE_MS &&
                  SCALE_WORKER_STALE_MS < HARD_MAX_CIRCUIT_CLOSED_MS,
              "Scale worker stale timeout must be useful and safety-bounded");
static_assert(FLASH_IO_CONTROL_LOCK_TIMEOUT_MS * 20U <
                  TASK_WATCHDOG_TIMEOUT_MS,
              "Control flash lock must stay well under the task watchdog");
static_assert(BLE_CONNECT_TIMEOUT_MS + SCALE_ATT_TIMEOUT_MS <
                  TASK_WATCHDOG_TIMEOUT_MS,
              "GAP connect plus one ATT wait must fit under the task watchdog");
static_assert(BLE_DISCOVER_TIMEOUT_MS < TASK_WATCHDOG_TIMEOUT_MS,
              "GATT discovery must fit under the task watchdog");
static_assert(SCALE_STREAM_GAP_MS < MAX_AUTOMATION_WEIGHT_AGE_MS,
              "Stream gaps must stay below the 1 s STALE window");
static_assert(CONTROL_STATUS_REFRESH_WAIT_MS >= 20U &&
                  CONTROL_STATUS_REFRESH_WAIT_MS <= 50U,
              "GET snapshot wait must cover one control tick without hanging httpd");

// ---------------------------------------------------------------------------
// Persistent storage and scale prediction
// ---------------------------------------------------------------------------

constexpr size_t EEPROM_SIZE = 2;
constexpr size_t WEIGHT_ADDR = 0;
constexpr size_t OFFSET_ADDR = 1;
constexpr size_t TREND_POINT_COUNT = WEIGHT_TREND_POINT_COUNT;
static_assert(MAX_SHOT_DATAPOINTS >= WEIGHT_TREND_POINT_COUNT,
              "Shot trajectory must hold the prediction window");
static_assert(SHOT_CURVE_MAX_POINTS == 31,
              "ControlStatusSnapshot shotCurveWeightCg must match sampler");

bool startExtendedPulseTrain(uint32_t durationMs);
bool startPulseTrain(BuzzerPattern pattern, uint32_t durationMs);
bool emitAlert(AlertEvent event, uint32_t cycleId = 0);
bool commandAlertUsesBuzzer();
void emitImmediateCommandAlertIfBuzzer(AlertEvent event);
bool emitCircuitCycleAlert(AlertEvent event, bool preempt);
void emitCommandAlert(AlertEvent event, bool commandAttempted,
                      bool writeSucceeded, bool commandFeedbackExpected);

enum class TimerStopResult : uint8_t {
  NOT_REQUIRED,
  NOT_ATTEMPTED,
  PENDING,
  WRITE_SUCCEEDED,
  WRITE_FAILED
};

struct ShotTrajectory {
  uint32_t startMs = 0;
  float expectedEndS = DEFAULT_OPERATIONAL_WALL_MS / 1000.0f;
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
  bool remoteTimerStartSettled = false;
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
  uint32_t scaleDisconnectSequenceAtStart = 0;
  uint32_t weightSequenceAtStart = 0;
  uint32_t connectionGenerationAtStart = 0;
  uint32_t ownedConnectionGeneration = 0;
  uint32_t startedAtMs = 0;
  uint32_t circuitClosedAtMs = 0;
  // Mirror of rinseClockStartedAtMs after the stopper accepts. Tests only.
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
  FirstFlowState firstFlow = {};
  uint8_t firstFlowAcceptedConfirmations = 0;
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
  bool cupRemovedPending = false;
  AccidentalTouchPhase accidentalTouchPhase = AccidentalTouchPhase::STARTUP;
  AccidentalTouchClass accidentalTouchClass = AccidentalTouchClass::OK;
  bool accidentalTouchHolding = false;
  uint8_t accidentalTouchPendingCount = 0;
  float accidentalTouchPendingG[ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES] = {};
};

struct PendingShotFinalize {
  bool pending = false;
  bool offsetAnalysis = false;
  bool logEligible = false;
  uint32_t endedAtMs = 0;
  uint32_t dripDelayMs = DEFAULT_DRIP_DELAY_MS;
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
  uint32_t minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  bool lastKnownWeightValid = false;
  float lastKnownWeightG = 0.0f;
  uint8_t activePresetId = 0;
  uint32_t cycleId = 0;
  bool retarePerformed = false;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  bool autoToManualGuardEnabled = false;
  bool autoToManualGuardEnforced = false;
  bool autoToManualGuardArmed = false;
  uint32_t autoToManualGuardRemainingMs = 0;
  bool noScaleShotGuardEnabled = false;
  bool noScaleShotGuardArmed = false;
  uint8_t noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
  char scaleProtocol[20] = "none";
  ShotCurveRecord curve = {};
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
// BLE companion, application state and input state
// ---------------------------------------------------------------------------

#if !defined(SHOT_STOPPER_HOST_TEST)
ShotStopperBleCompanion *bleCompanion = nullptr;
BleCompanionPersistedSettings bleCompanionPersistedSettings;
#endif
BleCompanionRuntimeSnapshot bleCompanionRuntimeSnapshot;
BleCompanionStatusSnapshot bleCompanionStatusSnapshot;

bool bleCompanionProfileAllocated() {
#if !defined(SHOT_STOPPER_HOST_TEST)
  return bleCompanion != nullptr;
#else
  return false;
#endif
}

StopperState stopperState = StopperState::REQUIRES_OFF;
ShotTrajectory shot;
CycleSession session;
PendingShotFinalize pendingFinalize;
RuntimeConfig runtimeConfig;
LocalBuzzer localBuzzer;
ShotPresetBank presetBank;
LastCycleSummary lastCycle;
// Debug events are not a flash DMA source and are not added from an ISR.
SHOT_STOPPER_PSRAM_BSS DebugRingBuffer debugLog;
LogLevel serialLogLevel = LogLevel::NONE;
LogLevel ringRetainLogLevel = LogLevel::NONE;
uint32_t lastReportedLogOverwritten = 0;
// Working copies: NVS/partition I/O copies through internal flash scratch
// first. Safe in PSRAM BSS because putBytes/erase/write never DMA these.
SHOT_STOPPER_PSRAM_BSS ShotLog shotLog;
SHOT_STOPPER_PSRAM_BSS ShotCurveLog shotCurves;
ShotCurveSampler shotCurveSampler;
ShotCurveRecord lastShotCurve = emptyShotCurveRecord();
LastShotStore lastShotStore;
PersistedLastShot persistedLastShot;
bool lastShotNvsDirty = false;
bool shotLogPersistFailLatched = false;
bool shotCurvePersistFailLatched = false;
bool lastShotPersistFailLatched = false;
uint32_t shotStorePersistRetryAtMs = 0;

bool noScaleShotGuardArmed = true;
uint32_t noScaleShotGuardActivityAtMs = 0;
bool noScaleShotGuardScaleWasAvailable = false;
bool noScaleShotGuardHold = false;
uint32_t noScaleShotGuardHoldAtMs = 0;
bool noScaleShotGuardNeedsFreshActivator = false;
bool noScaleRequireBypassReady = false;
bool noScaleRequireBypassHoldSeen = false;
uint8_t noScaleRequireBypassCycles = 0;
uint32_t noScaleRequireBypassStartedAtMs = 0;
bool noScaleRequireBypassCompletedThisLoop = false;
bool cupStartGuardHold = false;
uint32_t cupStartGuardHoldAtMs = 0;

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
QueueHandle_t webCommandQueue = nullptr;
QueueHandle_t bleCompanionRequestQueue = nullptr;
QueueHandle_t bleCompanionResultQueue = nullptr;
portMUX_TYPE bleCompanionMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE debugLogMux = portMUX_INITIALIZER_UNLOCKED;
bool pendingCupRemovedSettle = false;
uint32_t scaleRecoveredStaleCount = 0;
uint32_t scaleRecoveredStaleMs = 0;
bool recoverableStaleOpen = false;
uint32_t recoverableStaleStartedAtMs = 0;
uint32_t recoverableStaleDisconnectSequence = 0;
uint32_t recoverableStaleConnectionGeneration = 0;
WeightStreamState telemetryWeightStreamState = WeightStreamState::NO_SAMPLE;
bool scaleCompletionBeepScheduled = false;

bool virtualHoldOn = false;
// Full status snapshots stay in internal DRAM (check_web_assets forbids
// SHOT_STOPPER_PSRAM_BSS on this type). The 32-byte gate is published every
// loop tick; the ~876 B blob is rebuilt in-place on GET and control/safety
// edges under the seqlock — no staging copy.
ControlStatusSnapshot publishedControlStatus;
uint32_t controlStatusSeq = 0;
ControlGateSnapshot publishedControlGate;
uint32_t controlGateSeq = 0;
bool controlStatusPublishRequested = false;
constexpr uint32_t kControlStatusSeqlockTries = 64;
SHOT_STOPPER_PSRAM_BSS RuntimeConfig publishedRuntimeConfig;
SHOT_STOPPER_PSRAM_BSS ShotPresetBank publishedPresetBank;
uint32_t recipeSeq = 0;
bool bootDegraded = false;
uint32_t bleCompanionResultDropped = 0;
MaintenanceLease maintenanceLease;
WebCommand maintenanceCancellationCommand;
bool maintenanceCancellationPending = false;
// Planned ESP.restart (OTA flash, Admin, CLI) waits here while a shot is
// running. The paddle is never blocked; the restart proceeds once idle.
bool plannedRestartHeld = false;
WebCommand pendingPlannedRestart = {};
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
SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistRequest;
SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistReceive;
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
uint32_t loopIntervalGapMs = 0;
uint32_t healthIntervalMaxGapMs = 0;
uint32_t loopStackMinWords = 0;
uint32_t healthTelemetryAtMs = 0;
uint32_t freeHeapBytes = 0;
uint32_t minimumFreeHeapBytes = 0;
uint32_t largestFreeHeapBlockBytes = 0;
uint32_t psramSizeBytes = 0;
uint32_t psramFreeBytes = 0;
uint32_t psramLargestFreeBlockBytes = 0;
uint32_t bleHostAllocPsramCount = 0;
uint32_t bleHostAllocFallbackCount = 0;
uint32_t bleHostHciRxDropped = 0;
uint32_t bleHostHciTxDropped = 0;
bool healthHeapAlertLatched = false;
bool healthHeapRestartLatched = false;
uint32_t healthHeapLowSinceMs = 0;
bool healthStackAlertLatched = false;
bool healthLoopGapAlertLatched = false;
Hwmon hwmon;
HwmonSnapshot hwmonSnapshot = {};
TaskProfiler taskProfiler;
bool platformClockReady = false;
bool persistenceReady = false;
bool firmwareInitializationComplete = false;

bool beginMaintenanceLease(const WebCommand &networkCommand,
                           bool applyRuntimeOnSuccess);
void completeMaintenanceLease(const WebCommand &result);
void rejectWebCommand(const WebCommand &command);
void holdOrBeginPlannedRestart(const WebCommand &command);
void servicePendingPlannedRestart();
void queueRuntimePersist(int32_t reasonBits);
void commitLiveRuntimeConfig(const RuntimeConfig &composed, int32_t reasonBits);

#ifndef SHOT_STOPPER_HOST_TEST
SHOT_STOPPER_PSRAM_BSS PersistedSettings persistedSettings;
ShotStopperNetwork networkManager;
#endif

// The esp_timer callback independently opens the machine circuit at the hard limit even if the
// normal control loop is delayed or unavailable.
esp_timer_handle_t relaySafetyTimer = nullptr;
esp_timer_handle_t operationalLimitTimer = nullptr;
IndependentSafetyTimer independentSafetyTimer;
portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;
bool circuitClosed = false;
bool relaySafetyTripped = false;
bool operationalLimitTripped = false;
uint32_t circuitClosedAtMs = 0;
struct PendingScaleTimerStop {
  bool pending = false;
  uint32_t targetMs = 0;
  uint32_t extraDelayMs = 0;
  uint32_t catchupDeadlineAtMs = 0;
  uint32_t extraDueAtMs = 0;
};
PendingScaleTimerStop pendingScaleTimerStop;
bool pendingBrewRfRestore = false;
uint32_t operationalLimitAtArmMs = HARD_MAX_CIRCUIT_CLOSED_MS;
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
UsbSerialEnableSource usbSerialEnableSource = UsbSerialEnableSource::OFF;

bool usbConsoleJumperPresent();

bool scaleConnectedLedInitialized = false;
bool lastScaleConnectedLedOn = false;
enum class ScaleConnectedLedPattern : uint8_t {
  OFF,
  SOLID,
  FAST_BLINK,
  SLOW_BLINK
};
ScaleConnectedLedPattern lastScaleConnectedLedPattern =
    ScaleConnectedLedPattern::OFF;
uint32_t lastScaleConnectedLedToggleAtMs = 0;

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
    snprintf(message, capacity, "Advanced Shot Stopper %s (bootId=%ld)",
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
      event.argument1 >= static_cast<int32_t>(WebCommandType::REMOTE_ON) &&
      event.argument1 <=
          static_cast<int32_t>(WebCommandType::MAINTENANCE_COMPLETE)) {
    snprintf(message, capacity, "%s: %s", debugCodeName(event.code),
             webCommandTypeName(
                 static_cast<WebCommandType>(event.argument1)));
    return;
  }
  copyCString(message, capacity, debugCodeName(event.code));
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
  if (session.active || circuitClosed) {
    return;
  }
  Serial.println(message);
}

void serialTracef(LogLevel level, const char *fmt, ...) {
  if (fmt == nullptr || !logLevelAtMost(level, serialLogLevel)) {
    return;
  }
  if (session.active || circuitClosed) {
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

  // USB CDC TX can block if the host is not draining. During a pour keep the
  // control path off Serial; the RAM ring still captures the event.
  if (toSerial && !session.active && !circuitClosed) {
    writeSerialLogLine(event);
  }
}

void addDebugEvent(DebugCategory category, DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0) {
  logEmit(debugCodeDefaultLevel(code), category, code, argument1, argument2);
}

size_t copyDebugEvents(uint32_t afterSequence, DebugEvent *output,
                       size_t capacity) {
  size_t copied = 0;
  uint32_t after = afterSequence;
  while (copied < capacity) {
    DebugEvent event;
    bool have = false;
    portENTER_CRITICAL(&debugLogMux);
    have = debugLog.copyFirstAfter(after, event);
    portEXIT_CRITICAL(&debugLogMux);
    if (!have) {
      break;
    }
    output[copied++] = event;
    after = event.sequence;
  }
  return copied;
}

void copyTaskProfiler(TaskProfilerSnapshot &output) {
  taskProfiler.copySnapshot(output);
}

void copyControlStatus(ControlStatusSnapshot &output) {
  for (uint32_t attempt = 0; attempt < kControlStatusSeqlockTries; ++attempt) {
    const uint32_t s0 = __atomic_load_n(&controlStatusSeq, __ATOMIC_ACQUIRE);
    if ((s0 & 1U) != 0U) {
      taskYIELD();
      continue;
    }
    output = publishedControlStatus;
    const uint32_t s1 = __atomic_load_n(&controlStatusSeq, __ATOMIC_ACQUIRE);
    if (s0 == s1) {
      return;
    }
  }
  output = publishedControlStatus;
}

void copyControlGate(ControlGateSnapshot &output) {
  for (uint32_t attempt = 0; attempt < kControlStatusSeqlockTries; ++attempt) {
    const uint32_t s0 = __atomic_load_n(&controlGateSeq, __ATOMIC_ACQUIRE);
    if ((s0 & 1U) != 0U) {
      taskYIELD();
      continue;
    }
    output = publishedControlGate;
    const uint32_t s1 = __atomic_load_n(&controlGateSeq, __ATOMIC_ACQUIRE);
    if (s0 == s1) {
      return;
    }
  }
  output = publishedControlGate;
}


void reportTaskWatchdogFault() {
  criticalTaskWatchdogFault = true;
}

void feedOrTripCurrentTaskWatchdog() {
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
  }
}

void requestSafeRestart() {
  safeRestartRequested = true;
}

#ifndef SHOT_STOPPER_HOST_TEST
// The Arduino core would otherwise cancel the bootloader rollback inside
// initArduino(), before this firmware has proven it can do anything. Deferring
// hands that decision to ShotStopperNetwork, which confirms the image only
// after it has served its Web UI.
extern "C" bool verifyRollbackLater() {
  return true;
}
#endif

size_t copyShotRecords(ShotLogRecord *output, size_t capacity) {
  return shotLog.copyNewestFirst(output, capacity);
}

size_t copyShotCurves(ShotCurveRecord *output, size_t capacity) {
  return shotCurves.copyNewestFirst(output, capacity);
}

bool deleteShotRecord(uint32_t id) {
  const bool hadCurve = shotCurves.containsShotId(id);
  const bool hadLog = shotLog.containsId(id);
  if (!hadLog && !hadCurve) {
    return false;
  }
  if (hadCurve && !shotCurves.removeById(id)) {
    return false;
  }
  if (hadLog && !shotLog.removeById(id)) {
    return false;
  }
  return true;
}

bool clearShotLog() {
  if (!shotCurves.clear()) {
    return false;
  }
  return shotLog.clear();
}

bool clearLastShot() {
  persistedLastShot = PersistedLastShot{};
  lastShotCurve = emptyShotCurveRecord();
  lastShotNvsDirty = false;
  return lastShotStore.clear();
}

void clearLastShotSnapshot() {
  persistedLastShot = PersistedLastShot{};
  lastShotCurve = emptyShotCurveRecord();
  lastShotNvsDirty = false;
}

#ifndef SHOT_STOPPER_HOST_TEST
bool resetAllDurableStoresForNetwork(PersistedSettings &settings) {
  if (!resetAllDurableStores(settings, bleCompanionPersistedSettings, shotLog,
                             lastShotStore, shotCurves)) {
    return false;
  }
  // Status/UI read this snapshot, not LastShotStore. Drop it only after NVS
  // factory succeeded so a failed reset does not blank a still-durable shot.
  clearLastShotSnapshot();
  return true;
}
#endif

void persistLastShotSnapshot(const PersistedLastShot &snapshot) {
  persistedLastShot = snapshot;
  lastShotStore.adopt(snapshot);
  lastShotNvsDirty = true;
}

void applyLastShotManualFields(PersistedLastShot &last) {
  if (persistedLastShot.valid && persistedLastShot.cycleId == last.cycleId) {
    last.rating = persistedLastShot.rating;
    last.shotLogId = persistedLastShot.shotLogId;
  }
}

bool persistLastShotRating(uint8_t rating) {
  if (!persistedLastShot.valid || rating > SHOT_LOG_RATING_MAX) {
    return false;
  }
  persistedLastShot.rating = rating;
  lastShotStore.adopt(persistedLastShot);
  if (lastShotStore.save()) {
    lastShotNvsDirty = false;
    return true;
  }
  lastShotNvsDirty = true;
  return false;
}

bool rateShotRecord(uint32_t id, uint8_t rating) {
  if (id == 0 || rating > SHOT_LOG_RATING_MAX) {
    return false;
  }
  if (!shotLog.updateRating(id, rating)) {
    return false;
  }
  if (persistedLastShot.valid && persistedLastShot.shotLogId == id) {
    (void)persistLastShotRating(rating);
  }
  return true;
}

bool rateLastShot(uint8_t rating) {
  if (rating > SHOT_LOG_RATING_MAX || !persistedLastShot.valid) {
    return false;
  }
  if (persistedLastShot.shotLogId != 0 &&
      shotLog.containsId(persistedLastShot.shotLogId) &&
      !shotLog.updateRating(persistedLastShot.shotLogId, rating)) {
    return false;
  }
  return persistLastShotRating(rating);
}

void persistLastShotFromFinalize(const PendingShotFinalize &snapshot,
                                 float finalWeightG, bool finalWeightValid) {
  PersistedLastShot last = {};
  last.valid = true;
  last.cycleId = snapshot.cycleId;
  last.durationMs = static_cast<uint32_t>(snapshot.durationDs) * 100U;
  last.endReason = snapshot.endReason;
  last.weightValid = finalWeightValid;
  last.currentWeightG = finalWeightValid ? finalWeightG : 0.0f;
  last.goalWeightG = snapshot.goalWeightG;
  last.extractionExtended =
      snapshot.extractionExtended && snapshot.extractionGuardEnabled;
  last.slowExtractionExtended =
      snapshot.slowExtractionExtended && snapshot.slowExtractionGuardEnabled;
  last.activeStopWeightG =
      last.extractionExtended
          ? snapshot.maxRecoveryWeightG
          : (last.slowExtractionExtended
                 ? snapshot.minRecoveryWeightG
                 : static_cast<float>(snapshot.goalWeightG));
  if (snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING) {
    last.firstDropElapsedMs =
        static_cast<uint32_t>(snapshot.firstDropDs) * 100U;
  }
  last.retarePerformed = snapshot.retarePerformed;
  last.shotType = static_cast<uint8_t>(lastShotTypeFromCycle(
      snapshot.finalState, snapshot.startedWithScale, snapshot.timerOnly,
      snapshot.automaticBrew));
  last.scaleAvailable = snapshot.startedWithScale;
  last.fastExtractionGuardEnabled = snapshot.extractionGuardEnabled;
  last.slowExtractionGuardEnabled = snapshot.slowExtractionGuardEnabled;
  last.autoToManualGuardEnabled = snapshot.autoToManualGuardEnabled;
  last.autoToManualGuardEnforced = snapshot.autoToManualGuardEnforced;
  last.autoToManualGuardArmed = snapshot.autoToManualGuardArmed;
  last.autoToManualGuardRemainingMs = snapshot.autoToManualGuardRemainingMs;
  last.noScaleShotGuardEnabled = snapshot.noScaleShotGuardEnabled;
  last.noScaleShotGuardArmed = snapshot.noScaleShotGuardArmed;
  last.noScaleBbwMode = snapshot.noScaleBbwMode;
  copyCString(last.scaleProtocol, sizeof(last.scaleProtocol),
              snapshot.scaleProtocol);
  last.scaleProtocol[sizeof(last.scaleProtocol) - 1] = '\0';
  if (last.extractionExtended) {
    last.minBbwBrewTimeRemainingMs =
        last.durationMs >= snapshot.minBbwBrewTimeMs
            ? 0U
            : snapshot.minBbwBrewTimeMs - last.durationMs;
  }
  applyLastShotManualFields(last);
  persistLastShotSnapshot(last);
  lastShotCurve = snapshot.curve;
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
  const bool lastAcceptedValid =
      session.hasWeightAnchor && isfinite(session.lastAcceptedWeightG);
  if (lastAcceptedValid &&
      (!last.weightValid ||
       !plausibleSettledBrewWeight(last.currentWeightG,
                                   session.lastAcceptedWeightG, true))) {
    last.weightValid = true;
    last.currentWeightG = session.lastAcceptedWeightG;
  }
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
      session.circuitClosedAtMs != 0U ? session.circuitClosedAtMs : session.startedAtMs;
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
  last.noScaleShotGuardEnabled = noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
  last.noScaleShotGuardArmed = noScaleShotGuardArmed;
  last.noScaleBbwMode = runtimeConfig.noScaleBbwMode;
  if (session.autoToManualGuardEnforced) {
    const uint32_t nowMs = millis();
    last.autoToManualGuardRemainingMs =
        static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs - nowMs) <=
                0
            ? 0U
            : session.autoToManualGuardDeadlineAtMs - nowMs;
  }
  if (last.extractionExtended) {
    last.minBbwBrewTimeRemainingMs =
        durationMs >= session.config.minBbwBrewTimeMs
            ? 0U
            : session.config.minBbwBrewTimeMs - durationMs;
  }
  copyCString(last.scaleProtocol, sizeof(last.scaleProtocol),
              scaleProtocolName);
  last.scaleProtocol[sizeof(last.scaleProtocol) - 1] = '\0';
  applyLastShotManualFields(last);
  persistLastShotSnapshot(last);
  lastShotCurve = shotCurveSampler.snapshot();
}

bool controlAllowsConfigurationNow();
RuntimeConfig effectiveRuntimeConfig();
bool scaleLinkAvailable(const ScaleLinkSnapshot &snapshot);
void resetCupPresence();

bool enqueueWebCommand(const WebCommand &command) {
  return webCommandQueue != nullptr &&
         xQueueSend(webCommandQueue, &command, 0) == pdTRUE;
}

bool enqueueBleCompanionRequest(const BleCompanionRequest &request) {
  return bleCompanionRequestQueue != nullptr &&
         xQueueSend(bleCompanionRequestQueue, &request, 0) == pdTRUE;
}

void copyBleCompanionRuntimeSnapshot(BleCompanionRuntimeSnapshot &output) {
  portENTER_CRITICAL(&bleCompanionMux);
  output = bleCompanionRuntimeSnapshot;
  portEXIT_CRITICAL(&bleCompanionMux);
}

void publishBleCompanionStatus(BleCompanionStatusSnapshot status) {
  portENTER_CRITICAL(&bleCompanionMux);
  status.configuredEnabled =
      bleCompanionRuntimeSnapshot.configuredEnabled;
  status.restartRequired =
      status.configuredEnabled != status.enabled;
  bleCompanionStatusSnapshot = status;
  portEXIT_CRITICAL(&bleCompanionMux);
}

BleCompanionStatusSnapshot copyBleCompanionStatus() {
  BleCompanionStatusSnapshot output;
  portENTER_CRITICAL(&bleCompanionMux);
  output = bleCompanionStatusSnapshot;
  portEXIT_CRITICAL(&bleCompanionMux);
  return output;
}

void publishBleCompanionRuntimeSnapshot() {
  static uint32_t lastPublishedMs = 0;
  const uint32_t nowMs = millis();
  if (lastPublishedMs != 0 &&
      static_cast<uint32_t>(nowMs - lastPublishedMs) <
          BLE_COMPANION_RUNTIME_PUBLISH_MS) {
    return;
  }
  lastPublishedMs = nowMs;
  BleCompanionRuntimeSnapshot next;
  next.configurationAllowed = controlAllowsConfigurationNow();
  const RuntimeConfig effective = effectiveRuntimeConfig();
  next.brewByWeight = !effective.timerOnly;
  next.goalWeightG = effective.goalWeightG;
  next.autoTare = effective.autoTare;
  next.bbwProtectionMs = effective.bbwProtectionMs;
  next.operationalWallMs = effective.operationalWallMs;
  next.dripDelayMs = effective.dripDelayMs;
  next.scaleConnected =
      getScaleLinkSnapshot().state == ScaleLinkState::CONNECTED;
  next.shotActive = session.active;
#if !defined(SHOT_STOPPER_HOST_TEST)
  const NetworkStatusSnapshot network = networkManager.snapshot();
  next.apActive = network.apActive;
  copyCString(next.wifiSsid, sizeof(next.wifiSsid), network.staSsid);
  copyCString(next.wifiIp, sizeof(next.wifiIp), network.staIp);
#endif
  portENTER_CRITICAL(&bleCompanionMux);
  // Active state is immutable until reboot; the configured state may change
  // through Admin/CLI and is applied only by the next boot.
  next.enabled = bleCompanionStatusSnapshot.enabled;
  next.configuredEnabled =
      bleCompanionStatusSnapshot.configuredEnabled;
  bleCompanionRuntimeSnapshot = next;
  portEXIT_CRITICAL(&bleCompanionMux);
}

RuntimeConfig effectiveRuntimeConfig() {
  return composeEffectiveConfig(runtimeConfig, presetBank);
}

void publishRecipeState() {
  __atomic_fetch_add(&recipeSeq, 1U, __ATOMIC_RELAXED);
  publishedRuntimeConfig = runtimeConfig;
  publishedPresetBank = presetBank;
  __atomic_fetch_add(&recipeSeq, 1U, __ATOMIC_RELEASE);
}

void copyPresetBank(ShotPresetBank *out) {
  if (out == nullptr) {
    return;
  }
  for (uint32_t attempt = 0; attempt < kControlStatusSeqlockTries; ++attempt) {
    const uint32_t s0 = __atomic_load_n(&recipeSeq, __ATOMIC_ACQUIRE);
    if ((s0 & 1U) != 0U) {
      taskYIELD();
      continue;
    }
    *out = publishedPresetBank;
    const uint32_t s1 = __atomic_load_n(&recipeSeq, __ATOMIC_ACQUIRE);
    if (s0 == s1) {
      return;
    }
  }
  *out = publishedPresetBank;
}

void copyRuntimeConfig(RuntimeConfig *out) {
  if (out == nullptr) {
    return;
  }
  for (uint32_t attempt = 0; attempt < kControlStatusSeqlockTries; ++attempt) {
    const uint32_t s0 = __atomic_load_n(&recipeSeq, __ATOMIC_ACQUIRE);
    if ((s0 & 1U) != 0U) {
      taskYIELD();
      continue;
    }
    *out = publishedRuntimeConfig;
    const uint32_t s1 = __atomic_load_n(&recipeSeq, __ATOMIC_ACQUIRE);
    if (s0 == s1) {
      return;
    }
  }
  *out = publishedRuntimeConfig;
}


bool scaleLinkAvailable(const ScaleLinkSnapshot &snapshot) {
  return snapshot.state == ScaleLinkState::CONNECTED &&
         elapsedMs(snapshot.workerProgressAtMs) <= SCALE_WORKER_STALE_MS;
}

bool scaleAvailable() {
  return scaleLinkAvailable(getScaleLinkSnapshot());
}


uint32_t controlLoopTickDelayMs() {
  return scaleAvailable() ? 1 : LOOP_NO_SCALE_DELAY_MS;
}

bool bleCompanionStatusUnchanged(const BleCompanionStatusSnapshot &a,
                                 const BleCompanionStatusSnapshot &b) {
  return a.enabled == b.enabled &&
         a.configuredEnabled == b.configuredEnabled &&
         a.restartRequired == b.restartRequired &&
         a.stackReady == b.stackReady && a.advertising == b.advertising &&
         a.connected == b.connected &&
         a.protocolVersion == b.protocolVersion && a.apActive == b.apActive &&
         a.acceptedWrites == b.acceptedWrites &&
         a.rejectedWrites == b.rejectedWrites && a.lastReject == b.lastReject;
}

bool bleCompanionStatusShouldPublish(bool /*scaleLinked*/, bool changed,
                                     uint32_t lastPublishMs, uint32_t nowMs) {
  if (changed) {
    return true;
  }
  return lastPublishMs == 0U ||
         static_cast<uint32_t>(nowMs - lastPublishMs) >=
             BLE_COMPANION_NO_SCALE_PUBLISH_MS;
}

bool weightStreamIsLive(WeightStreamState state) {
  return state == WeightStreamState::FRESH ||
         state == WeightStreamState::OVERLOAD ||
         state == WeightStreamState::ANOMALOUS;
}

void noteRecoverableStaleTransition(WeightStreamState previous,
                                    WeightStreamState next,
                                    const ScaleLinkSnapshot &link,
                                    uint32_t now) {
  if (recoverableStaleOpen) {
    const bool sameLink =
        link.state == ScaleLinkState::CONNECTED &&
        link.disconnectSequence == recoverableStaleDisconnectSequence &&
        link.connectionGeneration == recoverableStaleConnectionGeneration;
    if (!sameLink) {
      recoverableStaleOpen = false;
    } else if (weightStreamIsLive(next)) {
      ++scaleRecoveredStaleCount;
      scaleRecoveredStaleMs +=
          static_cast<uint32_t>(now - recoverableStaleStartedAtMs);
      recoverableStaleOpen = false;
    }
  }
  if (!recoverableStaleOpen && next == WeightStreamState::STALE &&
      weightStreamIsLive(previous) &&
      link.state == ScaleLinkState::CONNECTED &&
      observedWeightConnectionGeneration == link.connectionGeneration) {
    recoverableStaleOpen = true;
    recoverableStaleStartedAtMs = now;
    recoverableStaleDisconnectSequence = link.disconnectSequence;
    recoverableStaleConnectionGeneration = link.connectionGeneration;
  }
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

WeightStreamState observedWeightStreamStateAt(uint32_t now) {
  if (observedWeightSequence == 0) {
    return WeightStreamState::NO_SAMPLE;
  }
  if (!observedWeightIsFresh(now)) {
    return WeightStreamState::STALE;
  }
  return weightStreamState;
}

void serviceWeightStreamTelemetry() {
  const uint32_t now = millis();
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  const WeightStreamState next = observedWeightStreamStateAt(now);
  noteRecoverableStaleTransition(telemetryWeightStreamState, next, link, now);
  telemetryWeightStreamState = next;
}

void latchAtmCurveFromSession(uint32_t atMs) {
  if (session.hasWeightAnchor && isfinite(session.lastAcceptedWeightG)) {
    shotCurveSampler.latchAtm(atMs, session.lastAcceptedWeightG);
  }
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
      latchAtmCurveFromSession(millis());
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
      shotCurveSampler.latchAtmCleared(millis());
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::AUTO_TO_MANUAL_GUARD_CLEARED);
    }
  }
}

void resetAccidentalTouchState() {
  session.accidentalTouchPhase = AccidentalTouchPhase::STARTUP;
  session.accidentalTouchClass = AccidentalTouchClass::OK;
  session.accidentalTouchHolding = false;
  session.accidentalTouchPendingCount = 0;
}

void resetWeightTrend() {
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
  resetAccidentalTouchState();
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
         !observedWeightIsFresh() ||
         snapshot.disconnectSequence !=
             session.scaleDisconnectSequenceAtStart;
}


RelaySafetySnapshot getRelaySafetySnapshot();
bool machineRunningElapsed(uint32_t &elapsedOut);
uint32_t machineElapsedMs();
bool machineIsRunning();
bool machineRequestStart(uint32_t operationalLimitMs,
                         bool remoteActuation = false);
bool machineRequestStop();
void requestRemoteTimerStop();
uint32_t cycleShotElapsedMs();

float cycleElapsedSeconds() {
  return cycleShotElapsedMs() / 1000.0f;
}

uint32_t cycleShotElapsedMs() {
  if (!session.active) {
    return 0U;
  }
  return machineElapsedMs();
}

void captureCycleCircuitStart() {
  const RelaySafetySnapshot startedRelay = getRelaySafetySnapshot();
  session.circuitClosedAtMs =
      startedRelay.closed ? startedRelay.closedAtMs : session.startedAtMs;
}

uint32_t endedCycleDurationMs() {
  uint32_t durationMs = machineElapsedMs();
  if (durationMs != 0U) {
    return durationMs;
  }
  const uint32_t startMs = session.circuitClosedAtMs != 0U
                               ? session.circuitClosedAtMs
                               : session.startedAtMs;
  return startMs != 0U ? elapsedMs(startMs) : 0U;
}

void flushPendingScaleTimerStopNow() {
  if (!pendingScaleTimerStop.pending) {
    return;
  }
  pendingScaleTimerStop = PendingScaleTimerStop{};
  requestRemoteTimerStop();
}

uint32_t scaleTimerDisplayTargetMs(uint32_t internalMs) {
  return (internalMs / 1000U) * 1000U;
}

void completePendingScaleTimerStop() {
  pendingScaleTimerStop = PendingScaleTimerStop{};
  requestRemoteTimerStop();
}

void armScaleTimerStopExtraDelay(uint32_t extraDelayMs) {
  if (extraDelayMs == 0U) {
    completePendingScaleTimerStop();
    return;
  }
  pendingScaleTimerStop.pending = true;
  pendingScaleTimerStop.extraDueAtMs = millis() + extraDelayMs;
}

void servicePendingScaleTimerStop() {
  if (!pendingScaleTimerStop.pending) {
    return;
  }
  const uint32_t nowMs = millis();
  if (pendingScaleTimerStop.extraDueAtMs != 0U) {
    if (static_cast<int32_t>(nowMs - pendingScaleTimerStop.extraDueAtMs) >= 0 ||
        !scaleAvailable()) {
      completePendingScaleTimerStop();
    }
    return;
  }
  if (!scaleAvailable()) {
    completePendingScaleTimerStop();
    return;
  }
  const bool timedOut =
      static_cast<int32_t>(nowMs -
                           pendingScaleTimerStop.catchupDeadlineAtMs) >= 0;
  if (!session.remoteTimerStartSettled && !timedOut) {
    return;
  }
  const uint32_t extraDelayMs = pendingScaleTimerStop.extraDelayMs;
  if (session.remoteTimerStartSettled && !session.remoteTimerStarted) {
    armScaleTimerStopExtraDelay(extraDelayMs);
    return;
  }
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (timedOut || !link.timerValid ||
      link.timerMs >= pendingScaleTimerStop.targetMs) {
    armScaleTimerStopExtraDelay(extraDelayMs);
  }
}

void scheduleScaleTimerStopAfterCycle(uint32_t internalElapsedMs) {
  if (!session.timerStartCommandQueued || session.stopTimerRequested) {
    return;
  }
  pendingScaleTimerStop = PendingScaleTimerStop{};
  pendingScaleTimerStop.pending = true;
  pendingScaleTimerStop.targetMs = scaleTimerDisplayTargetMs(internalElapsedMs);
  pendingScaleTimerStop.extraDelayMs = session.config.scaleTimerStopExtraDelayMs;
  pendingScaleTimerStop.catchupDeadlineAtMs =
      millis() + MAX_SCALE_TIMER_STOP_CATCHUP_MS;
  servicePendingScaleTimerStop();
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

void initializeScaleConnectedLed() {
  pinMode(SCALE_CONNECTED_LED_GPIO, OUTPUT);
  digitalWrite(SCALE_CONNECTED_LED_GPIO, LOW);
  lastScaleConnectedLedOn = false;
  lastScaleConnectedLedPattern = ScaleConnectedLedPattern::OFF;
  lastScaleConnectedLedToggleAtMs = 0;
  scaleConnectedLedInitialized = true;
}

ScaleConnectedLedPattern desiredScaleConnectedLedPattern() {
  if (!runtimeConfig.scaleConnectedLed) {
    return ScaleConnectedLedPattern::OFF;
  }
  const ScaleLinkSnapshot snapshot = getScaleLinkSnapshot();
  if (snapshot.state == ScaleLinkState::CONNECTED) {
    if (observedWeightStreamStateAt(millis()) == WeightStreamState::STALE) {
      return ScaleConnectedLedPattern::SLOW_BLINK;
    }
    return ScaleConnectedLedPattern::SOLID;
  }
  if (snapshot.connecting) {
    return ScaleConnectedLedPattern::FAST_BLINK;
  }
  return ScaleConnectedLedPattern::OFF;
}

void serviceScaleConnectedLed() {
  const ScaleConnectedLedPattern pattern = desiredScaleConnectedLedPattern();
  const uint32_t now = millis();
  bool on = false;
  uint32_t periodMs = 0;
  switch (pattern) {
    case ScaleConnectedLedPattern::SOLID:
      on = true;
      break;
    case ScaleConnectedLedPattern::FAST_BLINK:
      periodMs = SCALE_LED_FAST_BLINK_MS;
      break;
    case ScaleConnectedLedPattern::SLOW_BLINK:
      periodMs = SCALE_LED_SLOW_BLINK_MS;
      break;
    case ScaleConnectedLedPattern::OFF:
    default:
      on = false;
      break;
  }

  if (periodMs != 0) {
    if (!scaleConnectedLedInitialized ||
        pattern != lastScaleConnectedLedPattern) {
      on = true;
      lastScaleConnectedLedToggleAtMs = now;
    } else if (elapsedMs(lastScaleConnectedLedToggleAtMs) >= periodMs) {
      on = !lastScaleConnectedLedOn;
      lastScaleConnectedLedToggleAtMs = now;
    } else {
      on = lastScaleConnectedLedOn;
    }
  }

  if (scaleConnectedLedInitialized && on == lastScaleConnectedLedOn &&
      pattern == lastScaleConnectedLedPattern) {
    return;
  }
  digitalWrite(SCALE_CONNECTED_LED_GPIO, on ? HIGH : LOW);
  lastScaleConnectedLedOn = on;
  lastScaleConnectedLedPattern = pattern;
  scaleConnectedLedInitialized = true;
}

AlertOutputChannel currentAlertOutputChannel() {
  return effectiveAlertOutputChannel(runtimeConfig.alertOutputChannel);
}

bool soundAlertsEnabled() { return !runtimeConfig.soundAlertsMuted; }

#ifndef SHOT_STOPPER_HOST_TEST
void serviceBootRecoverySafety();
#endif

#include "ShotStopperMachine.h"

StopperState nextStateForUserHold(const MachineIntention &intent) {
  return intent.holdActive ? StopperState::REQUIRES_OFF : StopperState::READY;
}

void servicePendingBrewRfRestore() {
  if (!pendingBrewRfRestore) {
    return;
  }
  pendingBrewRfRestore = false;
}

// ---------------------------------------------------------------------------
// Scale timer session
// ---------------------------------------------------------------------------

void requestRemoteTimerStop();
void flushPendingScaleTimerStopNow();
void scheduleScaleTimerStopAfterCycle(uint32_t internalElapsedMs);
void servicePendingScaleTimerStop();

bool requestRemoteTimerStart() {
  ScaleCommand command;
  command.type = ScaleCommandType::START_TIMER_AND_TARE;
  command.cycleId = session.id;
  command.autoTare = session.config.autoTare;
  command.canTareStartTimer = session.config.canTareStartTimer;
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
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
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
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
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
  ++session.stopTimerAttempts;
  session.stopTimerLastAttemptMs = millis();
  if (!enqueueScaleCommand(command, session.remoteTimerStartSettled)) {
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
// Brew policy + scale sense + cup (domain headers). Orchestrator glue follows.
// These layers stay decoupled: no paddle/momentary internals, no direct calls
// between brew ↔ cup ↔ scale — this file applies their effects.
// ---------------------------------------------------------------------------

#include "ShotStopperCupPresence.h"
#include "ShotStopperBrew.h"
#include "ShotStopperRinse.h"
#include "ShotStopperScaleSense.h"

#if defined(SHOT_STOPPER_HOST_TEST)
#define SHOT_STOPPER_SCALE_WORKER_IN_ORCHESTRATOR
#include "ShotStopperScaleWorker.cpp"
#endif

void copyDebugExportExtras(DebugExportExtras &out,
                           const ControlStatusSnapshot &control) {
  out = DebugExportExtras{};
  if (session.active) {
    out.sessionActive = true;
    out.sessionAutomaticEnabled = session.automaticEnabled;
    out.sessionBbwProtectionEnabled = session.bbwProtectionEnabled;
    out.sessionBbwProtectionEnded = session.bbwProtectionEnded;
    out.sessionStartedWithScale = session.startedWithScale;
    out.sessionScaleWasLost = session.scaleWasLost;
    out.sessionCupRemovedPending = session.cupRemovedPending;
    out.sessionExtractionExtended = session.extractionExtended;
    out.sessionSlowExtractionExtended = session.slowExtractionExtended;
    out.sessionTargetReachedEarly = session.targetReachedEarly;
    out.sessionAutoToManualGuardArmed = session.autoToManualGuardArmed;
    out.sessionAutoToManualGuardEnforced = session.autoToManualGuardEnforced;
    out.sessionAccidentalTouchHolding = session.accidentalTouchHolding;
    out.sessionAccidentalTouchPhase =
        static_cast<uint8_t>(session.accidentalTouchPhase);
    out.sessionAccidentalTouchClass =
        static_cast<uint8_t>(session.accidentalTouchClass);
    out.sessionAccidentalTouchPendingCount = session.accidentalTouchPendingCount;
    out.sessionActivePresetId = session.activePresetId;
    out.sessionWeightControlState =
        static_cast<uint8_t>(session.weightControlState);
    out.sessionId = session.id;
    out.sessionStartedAtMs = session.startedAtMs;
    out.sessionFirstDropMs = session.firstDropMs;
    out.sessionAutoToManualGuardDeadlineAtMs =
        session.autoToManualGuardDeadlineAtMs;
    out.sessionTargetReachedAtMs = session.targetReachedAtMs;
  }
  {
    CupPresenceState cupState = CupPresenceState::ABSENT;
    copyCupPresenceDebug(
        cupState, out.cupHoldTransitions, out.cupInNegativeHole,
        out.cupRemovedArmed, out.cupRemovedConfirmations,
        out.cupPlaceStabilitySamples, out.cupHoleWeightG,
        out.cupPlaceCandidateWeightG);
    out.cupState = static_cast<uint8_t>(cupState);
  }
  {
    const ScaleLinkSnapshot link = getScaleLinkSnapshot();
    out.scaleLinkState = static_cast<uint8_t>(link.state);
    out.scaleDisconnectSequence = link.disconnectSequence;
    out.scaleConnectionGeneration = link.connectionGeneration;
    out.scalePacketSequence = link.packetSequence;
    out.scaleWorkerProgressAtMs = link.workerProgressAtMs;
    out.scaleTimerValid = link.timerValid;
    out.scaleTimerMs = link.timerMs;
    out.scaleTimerAgeMs = link.timerAgeMs;
    copyCString(out.scaleProtocolName, sizeof(out.scaleProtocolName),
                link.protocolName);
  }
  out.relay = getRelaySafetySnapshot();
  out.rawActivatorOn = control.rawActivatorOn;
  out.physicalActivatorOn = control.physicalActivatorOn;
  out.machineRunState = static_cast<uint8_t>(control.machineRunState);
  out.machineStartAckPending = control.machineStartAckPending;
  out.machineStopAckPending = control.machineStopAckPending;
  out.machineOrphanRun = control.machineOrphanRun;
  out.noScaleShotGuardHold = control.noScaleShotGuardHold;
  out.noScaleShotGuardScaleWasAvailable =
      control.noScaleShotGuardScaleWasAvailable;
  out.cupStartGuardHold = control.cupStartGuardHold;
  out.healthHeapAlertLatched = healthHeapAlertLatched;
  out.healthStackAlertLatched = healthStackAlertLatched;
  out.healthLoopGapAlertLatched = healthLoopGapAlertLatched;
}

GuardInputs loopGuardInputs;

void observeMachineSenseFromSession() {
  MachineSense sense;
  if (session.active && session.hasWeightAnchor &&
      isfinite(session.lastAcceptedWeightG)) {
    sense.weightG = session.lastAcceptedWeightG;
    sense.weightSequence = session.lastAcceptedPacketSequence != 0
                               ? session.lastAcceptedPacketSequence
                               : currentWeightSequence;
  } else {
    sense.weightG = currentWeight;
    sense.weightSequence = currentWeightSequence;
  }
  sense.weightFresh = observedWeightIsFresh();
  sense.accidentalHold = session.accidentalTouchHolding;
  sense.brewCycleActive = session.active;
  sense.firstDropSeen = session.firstDropMs != 0;
  sense.scaleConnectedEdge = scaleWorkerTakeConnectedEdge();
  sense.cupRemovedEdge = pendingCupRemovedSettle;
  pendingCupRemovedSettle = false;
  machineObserveSense(sense);
}

void fillLoopGuardsFromIntention(const MachineIntention &intention) {
  loopGuardInputs.holdActive = intention.holdActive;
  loopGuardInputs.physicalOn = intention.physicalOn;
  loopGuardInputs.stablyOff = intention.stablyOff;
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  loopGuardInputs.scaleAvailable = scaleLinkAvailable(link);
  loopGuardInputs.scaleUsable =
      loopGuardInputs.scaleAvailable && currentWeightIsFresh();
  loopGuardInputs.cup = cupPresenceState();
  loopGuardInputs.currentWeightG = currentWeight;
  // Product reuses the machine rinse-gesture window as the blocked-start hold
  // timeout. Brew sees only this composed field, not rinseGestureMs.
  loopGuardInputs.blockedHoldTimeoutMs = runtimeConfig.rinseGestureMs;
}

void captureLoopGuards() {
  fillLoopGuardsFromIntention(machinePollIntention());
}

void refreshLoopGuardsFromLastIntention() {
  fillLoopGuardsFromIntention(machineLastIntention());
}

void pushActivatorDrivePermission() {
  refreshLoopGuardsFromLastIntention();
  machineSetActivatorDriveAllowed(
      session.active || !guardsWouldBlockActivatorDrive(loopGuardInputs));
}

void orchestratePostTareBaselineArm() {
  armPostTareBaselineWindow();
  notifyCupPresenceTare();
  holdCupPresenceTransitions(true);
}

bool acceptWeightAndNotifyFirstFlow(float weight, uint32_t receivedAtMs,
                                    uint32_t packetSequence) {
  if (!acceptWeightIntoTrajectory(weight, receivedAtMs, packetSequence,
                                  activeWeightCutTargetG())) {
    return false;
  }
  const uint32_t latchAtMs =
      maybeLatchFirstFlowFromAcceptedWeight(weight, receivedAtMs);
  if (latchAtMs != 0) {
    onFirstDropsDetected(latchAtMs);
  }
  return true;
}

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

  const CupPresenceEvent cupEvent =
      feedCupPresence(weight, receivedAtMs, packetSequence);
  if (cupEvent == CupPresenceEvent::REMOVED) {
    // Run-state settle for momentary-only ASSUMED_OFF — independent of cut.
    pendingCupRemovedSettle = true;
    if (session.active && stopperState == StopperState::BREW &&
        !session.config.timerOnly && session.config.cupProtectionEnabled &&
        session.config.stopIfCupRemoved && session.startedWithScale) {
      session.cupRemovedPending = true;
      addDebugEvent(DebugCategory::SCALE, DebugCode::CUP_REMOVED_CONFIRMED,
                    weightToCentigrams(weight),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
    }
  }
  if (cupEvent == CupPresenceEvent::PLACED && session.active &&
      session.startedWithScale && retareWindowOpen() &&
      !session.awaitingPostTareBaseline && !session.flowDuringRetare &&
      session.firstDropMs == 0 && weight >= MIN_AUTOMATION_WEIGHT_G &&
      weight <= MAX_AUTOMATION_WEIGHT_G) {
    performAutomaticRetare();
  }

  if (session.active && session.startedWithScale) {
    const uint32_t firstDropAtMs =
        considerScaleFlowMarkers(weight, receivedAtMs, packetSequence);
    if (firstDropAtMs != 0) {
      onFirstDropsDetected(firstDropAtMs);
    }
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
      holdCupPresenceTransitions(false);
      weightStreamState = WeightStreamState::FRESH;
      return acceptWeightAndNotifyFirstFlow(weight, receivedAtMs,
                                            packetSequence);
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
    session.hasWeightAnchor = true;
    session.lastAcceptedWeightG = weight;
    session.lastAcceptedWeightAtMs = receivedAtMs;
    session.lastAcceptedPacketSequence = packetSequence;
    setWeightControlState(WeightControlState::ACTIVE);
  }

  if (accidentalTouchSessionActive()) {
    const AccidentalTouchClass classified =
        evaluateAccidentalTouchSample(weight, receivedAtMs);
    if (classified == AccidentalTouchClass::TOUCH) {
      resetDirectStopConfirmation();
      weightStreamState = WeightStreamState::FRESH;
      return true;
    }
  }

  considerDirectStopSample(weight, receivedAtMs, packetSequence,
                           connectionGeneration);
  weightStreamState = WeightStreamState::FRESH;
  return acceptWeightAndNotifyFirstFlow(weight, receivedAtMs, packetSequence);
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


bool endingRinseCycle(EndReason reason) {
  return stopperState == StopperState::RINSE ||
         reason == EndReason::RINSE_COMPLETE;
}

bool pendingFinalizeIsRinse(const PendingShotFinalize &snapshot) {
  return snapshot.endReason == EndReason::RINSE_COMPLETE ||
         snapshot.finalState == StopperState::RINSE;
}

void schedulePendingShotFinalize(EndReason reason, uint32_t durationMs) {
  if (brewEndIsAbandonedStart(reason) || endingRinseCycle(reason)) {
    return;
  }
  const bool logEligible = shotLogEligible(reason, durationMs);
  const bool bbwEligible =
      shotLogBbwEligible(session.startedWithScale, session.config.timerOnly,
                         shot.automaticBrew);
  const bool offsetAnalysis = shot.automaticBrew && !session.config.timerOnly &&
                              session.calibrationEligible &&
                              !session.extractionExtended &&
                              !session.slowExtractionExtended;
  if (!(logEligible && bbwEligible) && !offsetAnalysis) {
    return;
  }

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = offsetAnalysis;
  pendingFinalize.logEligible = logEligible;
  pendingFinalize.endedAtMs = millis();
  pendingFinalize.dripDelayMs = session.config.dripDelayMs;
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.cycleStartedAtMs =
      session.circuitClosedAtMs != 0U ? session.circuitClosedAtMs : session.startedAtMs;
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
  pendingFinalize.minBbwBrewTimeMs = session.config.minBbwBrewTimeMs;
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
  pendingFinalize.retarePerformed = session.retarePerformed;
  pendingFinalize.minRecoveryWeightG = session.config.minRecoveryWeightG;
  pendingFinalize.autoToManualGuardEnabled =
      session.config.autoToManualGuardEnabled;
  pendingFinalize.autoToManualGuardEnforced = session.autoToManualGuardEnforced;
  pendingFinalize.autoToManualGuardArmed = session.autoToManualGuardArmed;
  if (session.autoToManualGuardEnforced) {
    const uint32_t nowMs = millis();
    pendingFinalize.autoToManualGuardRemainingMs =
        static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs - nowMs) <=
                0
            ? 0U
            : session.autoToManualGuardDeadlineAtMs - nowMs;
  }
  pendingFinalize.noScaleShotGuardEnabled =
      noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
  pendingFinalize.noScaleShotGuardArmed = noScaleShotGuardArmed;
  pendingFinalize.noScaleBbwMode = runtimeConfig.noScaleBbwMode;
  copyCString(pendingFinalize.scaleProtocol,
              sizeof(pendingFinalize.scaleProtocol), scaleProtocolName);
  pendingFinalize.scaleProtocol[sizeof(pendingFinalize.scaleProtocol) - 1] =
      '\0';
  if (session.hasWeightAnchor) {
    shotCurveSampler.accept(session.lastAcceptedWeightG, millis());
    shotCurveSampler.captureEnd(millis(), session.lastAcceptedWeightG);
  } else {
    shotCurveSampler.captureEnd(millis());
  }
  pendingFinalize.curve = shotCurveSampler.snapshot();
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
  if (persistedLastShot.valid && persistedLastShot.cycleId == snapshot.cycleId) {
    record.extractionGuardEnabled = shotLogPackRating(
        record.extractionGuardEnabled, persistedLastShot.rating);
  }
  record.extractionExtended = shotLogPackExtendedFlags(
      snapshot.extractionExtended, snapshot.slowExtractionExtended);
  record.stopDetail = static_cast<uint8_t>(shotLogStopDetailFromEndReason(
      snapshot.endReason, snapshot.extractionGuardEnabled,
      snapshot.extractionExtended));
  if (snapshot.extractionGuardEnabled) {
    record.maxRecoveryWeightCg =
        shotLogWeightToCentigrams(snapshot.maxRecoveryWeightG);
    record.minBbwBrewTimeDs =
        static_cast<uint16_t>(snapshot.minBbwBrewTimeMs / 100U);
  } else {
    record.maxRecoveryWeightCg = SHOT_LOG_WEIGHT_MISSING;
    record.minBbwBrewTimeDs = SHOT_LOG_METRIC_MISSING;
  }
  record.targetReachedEarlyDs = snapshot.targetReachedEarlyDs;
  record.actualWeightSource = static_cast<uint8_t>(weightSource);

  if (finalWeightValid) {
    record.actualWeightCg = shotLogWeightToCentigrams(finalWeightG);
    record.errorCg =
        shotLogWeightToCentigrams(finalWeightG -
                           static_cast<float>(snapshot.goalWeightG));
    if (snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING &&
        (weightSource == ActualWeightSource::POST_DRIP ||
         weightSource == ActualWeightSource::LAST_KNOWN)) {
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

  const uint32_t newId = shotLog.nextRecordId();
  if (!shotLog.append(record, false)) {
    const size_t nextCount =
        shotLog.count() < SHOT_LOG_CAPACITY ? shotLog.count() + 1U
                                            : SHOT_LOG_CAPACITY;
    const size_t blobBytes =
        sizeof(ShotLogHeader) + nextCount * sizeof(ShotLogRecord);
    addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                  static_cast<int32_t>(blobBytes),
                  static_cast<int32_t>(shotLog.count()));
    return;
  }
  if (persistedLastShot.valid && persistedLastShot.cycleId == snapshot.cycleId) {
    persistedLastShot.shotLogId = newId;
    lastShotStore.adopt(persistedLastShot);
    lastShotNvsDirty = true;
  }
  if (snapshot.curve.count > 0) {
    ShotCurveRecord curve = snapshot.curve;
    curve.shotId = newId;
    if (!shotCurves.append(curve, false)) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                    static_cast<int32_t>(sizeof(ShotCurveStore)),
                    static_cast<int32_t>(shotCurves.count()));
    }
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
  ActualWeightSource source = ActualWeightSource::NONE;
  float weightG = 0.0f;
  bool valid = false;
  if (snapshot.lastKnownWeightValid && isfinite(snapshot.lastKnownWeightG)) {
    source = ActualWeightSource::LAST_KNOWN;
    weightG = snapshot.lastKnownWeightG;
    valid = true;
  }
  const bool persistEligible =
      snapshot.logEligible &&
      shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                         snapshot.automaticBrew) &&
      shotLogWeightEligible(weightG, valid);
  if (persistEligible && !pendingFinalizeIsRinse(snapshot)) {
    commitPendingShotLog(snapshot, weightG, valid, source);
  }
  // Home last shot is independent of history eligibility (incl. weight < 1 g).
  if (!pendingFinalizeIsRinse(snapshot) &&
      !brewEndIsAbandonedStart(snapshot.endReason)) {
    persistLastShotFromFinalize(snapshot, weightG, valid);
  }
}

void maybeQueueAutoToManualGuardSample(const PendingShotFinalize &snapshot,
                                       float finalWeightG,
                                       bool postDripWeightValid) {
  if (!postDripWeightValid || snapshot.timerOnly ||
      !shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                          snapshot.automaticBrew) ||
      !shotLogWeightEligible(finalWeightG, postDripWeightValid) ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended ||
      snapshot.endReason == EndReason::AUTO_TO_MANUAL_GUARD ||
      pendingFinalizeIsRinse(snapshot) ||
      brewEndIsAbandonedStart(snapshot.endReason) ||
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
      elapsedMs(pendingFinalize.endedAtMs) < pendingFinalize.dripDelayMs) {
    return;
  }

  PendingShotFinalize snapshot = pendingFinalize;
  pendingFinalize.pending = false;

  float finalWeightG = 0.0f;
  bool postDripWeightValid = false;
  if (scaleAvailable() && isfinite(currentWeight) &&
      currentWeightSequence != snapshot.endedWeightSequence &&
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) >
          0 &&
      plausibleSettledBrewWeight(currentWeight, snapshot.lastKnownWeightG,
                                 snapshot.lastKnownWeightValid)) {
    finalWeightG = currentWeight;
    postDripWeightValid = snapshot.startedWithScale;
  }

  if (brewWeightCutSettlesMachineOff(snapshot.endReason)) {
    if (postDripWeightValid) {
      machineNoteSettledWeightCutOff();
    } else {
      machineCancelSettledWeightCutOff();
    }
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

  // Drip delay determines the settled final weight, not extra shot time. The
  // curve was snapshotted when the relay opened, so revise that same endpoint
  // instead of leaving a false flat tail or appending drip-delay samples.
  if (logWeightValid) {
    (void)settleShotCurveEndWeight(snapshot.curve, logWeightG);
  }

  const bool persistEligible =
      snapshot.logEligible &&
      shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                         snapshot.automaticBrew) &&
      shotLogWeightEligible(logWeightG, logWeightValid);

  if (persistEligible && !pendingFinalizeIsRinse(snapshot)) {
    commitPendingShotLog(snapshot, logWeightG, logWeightValid, weightSource);
  }
  // Home last shot always reflects the finished cycle, even when history
  // skips (weight < 1 g, non-AUTO, etc.). Rinses and abandoned starts do not
  // overwrite it.
  if (!pendingFinalizeIsRinse(snapshot) &&
      !brewEndIsAbandonedStart(snapshot.endReason)) {
    persistLastShotFromFinalize(snapshot, logWeightG, logWeightValid);
  }

  maybeQueueAutoToManualGuardSample(snapshot, finalWeightG, postDripWeightValid);

  if (pendingFinalizeIsRinse(snapshot) || !snapshot.offsetAnalysis ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended) {
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

  if (!shotLogWeightEligible(finalWeightG, true)) {
    serialTrace(LogLevel::INFO,
                "Final weight below history threshold; offset unchanged");
    return;
  }

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


















AlertChannelContext currentAlertChannelContext() {
  AlertChannelContext ctx;
  ctx.channel = currentAlertOutputChannel();
  ctx.soundAlertsEnabled = soundAlertsEnabled();
  ctx.buzzerSupportEnabled = BUZZER_SUPPORT_ENABLED;
  ctx.buzzerReady = BUZZER_SUPPORT_ENABLED && localBuzzer.ready;
  ctx.scaleAvailable = scaleAvailable();
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  ctx.scaleSupportsIndependentBeep =
      link.features.has(ScaleFeatureIndependentBeep);
  ctx.scaleSupportsCommandFeedback =
      link.features.has(ScaleFeatureCommandAudibleFeedback);
  return ctx;
}

bool playLocalAlertTone(const BuzzerToneCommand &cmd, bool honorMute) {
  if (honorMute && !soundAlertsEnabled()) {
    return false;
  }
  if (!BUZZER_SUPPORT_ENABLED || !localBuzzer.ready || !cmd.valid) {
    return false;
  }
  return localBuzzer.requestTone(cmd);
}

bool startPulseTrain(BuzzerPattern pattern, uint32_t durationMs) {
  if (!BUZZER_SUPPORT_ENABLED || !localBuzzer.ready ||
      !buzzerPatternIsPulseTrain(pattern)) {
    return false;
  }
  return localBuzzer.request(pattern, durationMs);
}

bool startExtendedPulseTrain(uint32_t durationMs) {
  BuzzerToneCommand cmd =
      deriveBuzzerTone(AlertEvent::EXTENDED_PULSE, session.slowExtractionExtended,
                       runtimeConfig.buzzerExtendedPulseRate,
                       runtimeConfig.buzzerSlowExtendedPulseRate);
  if (!cmd.valid) {
    return false;
  }
  cmd.durationMs = durationMs;
  return playLocalAlertTone(cmd, false);
}

void stopPulseTrains() {
  localBuzzer.stopPulseTrains();
}

bool queueScaleIndependentAlert(AlertEvent event, uint32_t cycleId) {
  if (!scaleAvailable() ||
      !getScaleLinkSnapshot().features.has(ScaleFeatureIndependentBeep)) {
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

bool dispatchAlert(AlertKind kind, AlertEvent event, uint32_t cycleId,
                   bool commandAttempted, bool writeSucceeded,
                   bool commandFeedbackExpected) {
  AlertChannelContext ctx = currentAlertChannelContext();
  ctx.commandAttempted = commandAttempted;
  ctx.writeSucceeded = writeSucceeded;
  ctx.commandFeedbackExpected = commandFeedbackExpected;
  const AlertSink sink = selectAlertSink(kind, event, ctx);
  if (sink == AlertSink::None) {
    return false;
  }
  if (sink == AlertSink::Scale) {
    return queueScaleIndependentAlert(event, cycleId);
  }
  const BuzzerToneCommand tone =
      deriveBuzzerTone(event, session.slowExtractionExtended,
                       runtimeConfig.buzzerExtendedPulseRate,
                       runtimeConfig.buzzerSlowExtendedPulseRate);
  return playLocalAlertTone(tone, kind != AlertKind::Recovery);
}

// Independent / multi-tone alerts: first drop, paddle, completion, triples.
bool emitAlert(AlertEvent event, uint32_t cycleId) {
  return dispatchAlert(AlertKind::Independent, event, cycleId, false, false,
                       false);
}

bool commandAlertUsesBuzzer() {
  AlertChannelContext ctx = currentAlertChannelContext();
  return selectAlertSink(AlertKind::CommandImmediate, AlertEvent::START_TIMER,
                         ctx) == AlertSink::Buzzer;
}

// Tare/start/stop replacement sounds: fire at the local circuit/paddle/retare
// moment when the buzzer is the routed output. Never wait for BLE.
void emitImmediateCommandAlertIfBuzzer(AlertEvent event) {
  (void)dispatchAlert(AlertKind::CommandImmediate, event, 0, false, false,
                      false);
}

// Shot start/stop follow the relay, not the scale timer or Output channel.
// Both preempt so the circuit edge is not stuck behind a warning, echo, or pulse.
bool emitCircuitCycleAlert(AlertEvent event, bool preempt) {
  if (!soundAlertsEnabled()) {
    return false;
  }
  if (BUZZER_SUPPORT_ENABLED && localBuzzer.ready) {
    if (preempt) {
      localBuzzer.stopAll();
    }
    const BuzzerToneCommand tone =
        deriveBuzzerTone(event, session.slowExtractionExtended,
                         runtimeConfig.buzzerExtendedPulseRate,
                         runtimeConfig.buzzerSlowExtendedPulseRate);
    return playLocalAlertTone(tone, true);
  }
  if (event == AlertEvent::COMPLETION_EXTRA) {
    return emitAlert(event);
  }
  return false;
}

void playRecoveryCue(BuzzerCue cue) {
  AlertChannelContext ctx = currentAlertChannelContext();
  if (selectAlertSink(AlertKind::Recovery, AlertEvent::TARE, ctx) !=
      AlertSink::Buzzer) {
    return;
  }
  (void)playLocalAlertTone(deriveRecoveryTone(cue), false);
}

// BLE-result fallback only. Buzzer only already played at the local event.
// SCALE_PRIORITY still falls back here if the write missed, even if the
// scale dropped while the ATT write was in flight.
void emitCommandAlert(AlertEvent event, bool commandAttempted,
                      bool writeSucceeded, bool commandFeedbackExpected) {
  (void)dispatchAlert(AlertKind::CommandFallback, event, 0, commandAttempted,
                      writeSucceeded, commandFeedbackExpected);
}

bool shotCompletionGetsLongBeep(EndReason reason) {
  switch (reason) {
    case EndReason::ACTIVATOR:
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
    case EndReason::CUP_REMOVED:
    case EndReason::RINSE_COMPLETE:
      return true;
    default:
      return false;
  }
}



void requestCompletionAlert() {
  if (!soundAlertsEnabled()) {
    return;
  }
  if (emitCircuitCycleAlert(AlertEvent::COMPLETION_EXTRA, true)) {
    scaleCompletionBeepScheduled = false;
    return;
  }
  if (BUZZER_SUPPORT_ENABLED) {
    scaleCompletionBeepScheduled = true;
    return;
  }
  scaleCompletionBeepScheduled = false;
}

void cancelScaleCompletionBeep() {
  scaleCompletionBeepScheduled = false;
  cancelScaleCompletionBeepMailbox();
}

void cancelOperationalAlerts() {
  scaleCompletionBeepScheduled = false;
  cancelOperationalScaleBeeps();
  if (localBuzzer.busy()) {
    localBuzzer.stopAll();
  }
}

void serviceExtendedPulseAlert() {
  const BuzzerToneCommand cmd = deriveBuzzerTone(
      AlertEvent::EXTENDED_PULSE, session.slowExtractionExtended,
      runtimeConfig.buzzerExtendedPulseRate,
      runtimeConfig.buzzerSlowExtendedPulseRate);
  const bool want =
      soundAlertsEnabled() && BUZZER_SUPPORT_ENABLED && localBuzzer.ready &&
      session.active &&
      (session.extractionExtended || session.slowExtractionExtended) &&
      cmd.valid &&
      currentAlertOutputChannel() != AlertOutputChannel::SCALE_ONLY;
  if (!want) {
    localBuzzer.stopExtendedPulse();
    return;
  }
  if (localBuzzer.playingExtendedPulse(cmd)) {
    return;
  }
  if (localBuzzer.busy()) {
    localBuzzer.stopExtendedPulse();
    if (localBuzzer.busy()) {
      return;
    }
  }
  emitAlert(AlertEvent::EXTENDED_PULSE, session.id);
}

void serviceScaleCompletionBeep() {
  if (!soundAlertsEnabled()) {
    cancelScaleCompletionBeep();
    return;
  }
  if (!scaleCompletionBeepScheduled) {
    return;
  }
  requestCompletionAlert();
}














void servicePreferredScaleMacPersistence() {
#ifndef SHOT_STOPPER_HOST_TEST
  char mac[PREFERRED_SCALE_MAC_CAPACITY];
  char name[PREFERRED_SCALE_NAME_CAPACITY];
  ScaleHistoryEntry history[SCALE_HISTORY_CAPACITY];
  if (!scaleWorkerCopyPreferredIfDirty(mac, name, history)) {
    return;
  }
  const bool identityUnchanged =
      strncmp(persistedSettings.preferredScaleMac, mac,
              PREFERRED_SCALE_MAC_CAPACITY) == 0 &&
      strncmp(persistedSettings.preferredScaleName, name,
              PREFERRED_SCALE_NAME_CAPACITY) == 0 &&
      scaleHistoryIdentityEqual(persistedSettings.scaleHistory, history);
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  const ScaleMacNvsAction action = decideScaleMacNvsAction(
      identityUnchanged, link.connecting,
      link.state == ScaleLinkState::CONNECTED);
  if (action == ScaleMacNvsAction::CLEAR_DIRTY) {
    scaleWorkerClearPreferredDirty();
    return;
  }
  if (action == ScaleMacNvsAction::DEFER) {
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
          session.remoteTimerStartSettled = true;
          if (event.writeSucceeded && session.config.autoTare &&
              session.startedWithScale && session.active &&
              stopperState == StopperState::BREW &&
              (event.usedCombinedTareStart ||
               session.awaitingPostTareBaseline)) {
            const bool skipCombinedRearm =
                event.usedCombinedTareStart &&
                (session.retarePerformed ||
                 !session.awaitingPostTareBaseline);
            if (!skipCombinedRearm) {
              orchestratePostTareBaselineArm();
              markTareZeroReady();
            }
          }
        }
        emitCommandAlert(event.usedCombinedTareStart ? AlertEvent::TARE_START
                                                     : AlertEvent::START_TIMER,
                         event.commandAttempted, event.writeSucceeded,
                         event.commandFeedbackExpected);
        serialTracef(LogLevel::DEBUG, "Remote timer start write: %s",
                     event.writeSucceeded ? "successful" : "failed/skipped");
        addDebugEvent(DebugCategory::SCALE,
                      event.writeSucceeded ? DebugCode::SCALE_TIMER_START_OK
                                           : DebugCode::SCALE_TIMER_START_FAILED,
                      static_cast<int32_t>(event.cycleId));
        break;

      case ScaleEventType::TARE_RESULT:
        if (event.cycleId == session.id && event.writeSucceeded &&
            session.startedWithScale && session.active &&
            stopperState == StopperState::BREW) {
          orchestratePostTareBaselineArm();
          markTareZeroReady();
          resetDirectStopConfirmation();
        }
        emitCommandAlert(AlertEvent::TARE, event.commandAttempted,
                         event.writeSucceeded, event.commandFeedbackExpected);
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
                         event.writeSucceeded, event.commandFeedbackExpected);
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

void resetSessionForNewCycle(ControlSource source) {
  session = CycleSession{};
  session.id = nextCycleId++;
  if (nextCycleId == 0) {
    nextCycleId = 1;
  }
  session.active = true;
  session.source = source;
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
bool beginRinseCycle(ControlSource source);
bool enterRinse();

void beginCycle(ControlSource source = ControlSource::PHYSICAL) {
  refreshLoopGuardsFromLastIntention();
  if (source == ControlSource::PHYSICAL &&
      noScaleShotGuardNeedsFreshActivator) {
    if (!machineLastIntention().turnedOn) {
      return;
    }
    noScaleShotGuardNeedsFreshActivator = false;
  }
  maybeEmitManualNoScaleBeep(loopGuardInputs);
  if (noScaleShotGuardWouldBlock(loopGuardInputs)) {
    if (source == ControlSource::PHYSICAL) {
      noScaleShotGuardHold = true;
      noScaleShotGuardHoldAtMs = millis();
      noScaleShotGuardNeedsFreshActivator = true;
      return;
    }
    blockNoScaleShotGuard();
    return;
  }
  noScaleShotGuardHold = false;

  if (cupStartGuardWouldBlock(loopGuardInputs)) {
    if (source == ControlSource::PHYSICAL) {
      cupStartGuardHold = true;
      cupStartGuardHoldAtMs = millis();
      return;
    }
    cupStartGuardHold = false;
    addDebugEvent(DebugCategory::STATE, DebugCode::CUP_START_GUARD_BLOCKED,
                  weightToCentigrams(currentWeight));
    emitAlert(AlertEvent::CUP_START_BLOCKED);
    return;
  }
  cupStartGuardHold = false;

  flushPendingScaleTimerStopNow();
  cancelPendingFinalize("Previous drip analysis cancelled by a new cycle");

  resetSessionForNewCycle(source);
  session.startedAtMs = millis();
  session.circuitClosedAtMs = 0;
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
    resyncCupPresenceIfPanEmpty(currentWeight);
    if (session.config.autoTare) {
      orchestratePostTareBaselineArm();
      markTareZeroReady();
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

  const bool automaticBbw =
      session.startedWithScale && !session.config.timerOnly;
  machineSetPreferBleAirtime(automaticBbw);
  machineBeginCycle(automaticBbw);
  const uint32_t closeLimitMs =
      automaticBbw ? machineCloseLimitMs(session.config.operationalWallMs)
                   : HARD_MAX_CIRCUIT_CLOSED_MS;
  if (!machineRequestStart(closeLimitMs, source == ControlSource::WEB)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    machineSetPreferBleAirtime(false);
    machineEndCycle();
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  captureCycleCircuitStart();

  emitCircuitCycleAlert(session.startedWithScale && session.config.autoTare &&
                            session.config.canTareStartTimer
                        ? AlertEvent::TARE_START
                        : AlertEvent::START_TIMER,
                    true);
  if (session.startedWithScale) {
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
  const uint32_t durationMs = endedCycleDurationMs();

  stopPulseTrains();
  cancelScaleBrewBeep(session.id);
  cancelScaleCompletionBeep();
  // GPIO first, then the machine circuit-open cue. BLE advertising resume is
  // owned by the BLE worker (companion pause + syncScaleRadioCoex).
  if (brewWeightCutSettlesMachineOff(reason)) {
    machineArmSettledWeightCutOff();
  } else {
    machineCancelSettledWeightCutOff();
  }
  const bool rinseEnd = endingRinseCycle(reason);
  if (rinseEnd) {
    machineEndRinse();
  } else {
    machineRequestStop();
  }
  rinseClear();
  session.rinseStartedAtMs = 0;
  session.endReason = reason;
  if (!brewEndIsAbandonedStart(reason)) {
    if (shotCompletionGetsLongBeep(reason)) {
      // Completion LONG replaces the stop-timer SINGLE so ends are one cue.
      requestCompletionAlert();
    } else {
      emitCircuitCycleAlert(AlertEvent::STOP_TIMER, true);
    }
  }
  if (reason == EndReason::AUTO_TO_MANUAL_GUARD &&
      runtimeConfig.buzzerAutoToManualGuardEndBeep) {
    emitAlert(AlertEvent::ATM_END);
  }
  scheduleScaleTimerStopAfterCycle(durationMs);

  schedulePendingShotFinalize(reason, durationMs);
  // Home status must show the last shot immediately (during drip delay),
  // including empty / sub-1 g results. Rinses (including early abort) and
  // abandoned starts do not overwrite last shot.
  if (!rinseEnd && !brewEndIsAbandonedStart(reason)) {
    persistLastShotFromEndedCycle(reason, durationMs);
  }

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
  if (!rinseEnd && !brewEndIsAbandonedStart(reason)) {
    noScaleShotGuardActivityAtMs = millis();
  }
  session.active = false;
  session.awaitingPostTareBaseline = false;
  holdCupPresenceTransitions(false);
  virtualHoldOn = false;
  machineSetPreferBleAirtime(false);
  machineEndCycle();
  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_ENDED,
                static_cast<int32_t>(reason));
  transitionTo(nextState);
}

bool beginRinseCycle(ControlSource source) {
  refreshLoopGuardsFromLastIntention();
  if (noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode) &&
      noScaleShotGuardWouldBlock(loopGuardInputs)) {
    if (source == ControlSource::WEB) {
      maybeEmitManualNoScaleBeep(loopGuardInputs);
    }
    addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED);
    return false;
  }
  noScaleShotGuardNeedsFreshActivator = false;
  flushPendingScaleTimerStopNow();
  cancelPendingFinalize(source == ControlSource::WEB
                            ? "Previous drip analysis cancelled by a web rinse"
                            : "Previous drip analysis cancelled by a rinse");
  resetSessionForNewCycle(source);
  session.startedAtMs = millis();
  session.circuitClosedAtMs = 0;
  session.firstDropMs = 0;
  session.retareFlowFirstDetectedAtMs = 0;
  session.scaleBaselineReady = false;
  session.scaleBaselineG = 0.0f;
  session.weightSequenceAtStart = currentWeightSequence;
  session.startedWithScale = scaleAvailable();
  session.automaticEnabled = false;
  virtualHoldOn = false;
  resetShotTrajectory(session.startedAtMs);
  machineSetPreferBleAirtime(false);
  machineBeginCycle(false);
  if (!machineBeginRinse(session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    machineEndCycle();
    transitionTo(StopperState::REQUIRES_OFF);
    return false;
  }
  captureCycleCircuitStart();
  emitCircuitCycleAlert(AlertEvent::START_TIMER, true);
  // Only an Armed no-scale rinse clears the latch; keep Armed when the scale
  // is usable (e.g. web rinse with a connected scale).
  if (noScaleShotGuardArmed) {
    const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
    const bool scaleUsable =
        scaleLinkAvailable(scaleLink) && currentWeightIsFresh();
    if (!scaleUsable &&
        !noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode)) {
      consumeNoScaleShotGuard();
    }
  }
  if (session.startedWithScale) {
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

bool enterRinse() {
  if (!machineBeginRinse(session.config.operationalWallMs)) {
    // Do not machineEndRinse(): that would double-pulse on momentary.
    // BREW/MANUAL already get machineRequestStop + machineEndCycle here.
    if (stopperState == StopperState::BREW ||
        stopperState == StopperState::MANUAL_NO_SCALE) {
      finalizeCycle(EndReason::RELAY_SAFETY_FAILURE,
                    StopperState::REQUIRES_OFF);
    } else {
      session.active = false;
      session.endReason = EndReason::RELAY_SAFETY_FAILURE;
      machineEndCycle();
      transitionTo(StopperState::REQUIRES_OFF);
    }
    return false;
  }
  session.rinseStartedAtMs = rinseBegin(session.config.rinseDurationMs);
  session.automaticEnabled = false;
  shot.automaticBrew = false;
  session.calibrationEligible = false;
  session.autoToManualGuardArmed = false;
  session.autoToManualGuardEnforced = false;
  session.awaitingPostTareBaseline = false;
  holdCupPresenceTransitions(false);
  addDebugEvent(DebugCategory::STATE, DebugCode::RINSE_CLASSIFIED);
  transitionTo(StopperState::RINSE);
  maybeRequestNtpSyncOnActivity();
  return true;
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

void handleGlobalLimitTrip() {
  const bool wasAlreadyOpenedByTimer = consumeRelaySafetyTrip();
  if (wasAlreadyOpenedByTimer) {
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_OPENED);
  }
  if (!wasAlreadyOpenedByTimer && getRelaySafetySnapshot().closed) {
    machineRequestStop();
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
       elapsedMs(relay.closedAtMs) >= HARD_MAX_CIRCUIT_CLOSED_MS)) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::HARD_LIMIT);
    handleGlobalLimitTrip();
    return;
  }
  if (relay.operationalTripped ||
      (relay.closed && relay.operationalLimitMs < HARD_MAX_CIRCUIT_CLOSED_MS &&
       elapsedMs(relay.closedAtMs) >= relay.operationalLimitMs)) {
    handleOperationalLimitTrip();
    return;
  }
  if (relay.closed && session.active &&
      machineCloseLimitMs(session.config.operationalWallMs) ==
          HARD_MAX_CIRCUIT_CLOSED_MS &&
      machineAllowsAutomationStop() &&
      elapsedMs(relay.closedAtMs) >= session.config.operationalWallMs) {
    handleOperationalLimitTrip();
    return;
  }

  const MachineIntention intent = machineLastIntention();

  // The OFF edge that completes the emergency gesture is confirmation only.
  // This also covers momentary builds configured to start on button release.
  if (noScaleRequireBypassCompletedThisLoop) {
    noScaleRequireBypassCompletedThisLoop = false;
    return;
  }

  if (maintenanceLease.active) {
    if (relay.closed) {
      machineRequestStop();
    }
    if (intent.holdActive) {
      if (!maintenanceLease.forwarded) {
        if (maintenanceLease.command.type == WebCommandType::RESTART) {
          // Shot wins: keep the planned restart and let the paddle proceed.
          // Do not report CANCELED — the reboot still happens after idle.
          pendingPlannedRestart = maintenanceLease.command;
          plannedRestartHeld = true;
          maintenanceLease = MaintenanceLease{};
        } else {
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
      }
      transitionTo(StopperState::REQUIRES_OFF);
    }
    return;
  }

  if (session.active && session.source == ControlSource::WEB &&
      (intent.turnedOn || intent.turnedOff)) {
    finalizeCycle(EndReason::PHYSICAL_OVERRIDE, nextStateForUserHold(intent));
    return;
  }

  switch (stopperState) {
    case StopperState::REQUIRES_OFF:
      if (intent.intent == UserIntent::STABLE_IDLE || intent.stablyOff) {
        transitionTo(StopperState::READY);
      }
      return;

    case StopperState::READY:
      if (intent.intent == UserIntent::REQUEST_START) {
        beginCycle(ControlSource::PHYSICAL);
      }
      if (intent.intent == UserIntent::REQUEST_RINSE && machineSupportsRinse()) {
        if (noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode) &&
            noScaleShotGuardNeedsFreshActivator) {
          return;
        }
        noScaleShotGuardHold = false;
        cupStartGuardHold = false;
        if (!beginRinseCycle(ControlSource::PHYSICAL)) {
          return;
        }
        (void)enterRinse();
      }
      return;

    case StopperState::RINSE:
      // Physical edges are consumed by the machine while rinsing.
      if (rinseDeadlineReached()) {
        finalizeCycle(EndReason::RINSE_COMPLETE, nextStateForUserHold(intent));
      }
      return;

    case StopperState::BREW:
      if (intent.intent == UserIntent::REQUEST_RINSE && machineSupportsRinse()) {
        (void)enterRinse();
        return;
      }
      if (intent.intent == UserIntent::REQUEST_STOP) {
        if (session.automaticEnabled) {
          shot.automaticBrew = true;
        }
        finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
        if (!session.active || stopperState != StopperState::BREW) {
          return;
        }
      }

      if (expirePostTareBaselineIfNeeded()) {
        holdCupPresenceTransitions(false);
      }
      serviceBbwProtectionPhases();

      if (session.cupRemovedPending) {
        finalizeCycle(EndReason::CUP_REMOVED, nextStateForUserHold(intent));
        return;
      }
      if (machineTakeNoFlowIdle()) {
        finalizeCycle(EndReason::UNCONFIRMED_START,
                      nextStateForUserHold(intent));
        return;
      }

      if ((session.weightControlState == WeightControlState::ACTIVE ||
           session.weightControlState == WeightControlState::VALIDATING) &&
          scaleAutomationUnavailableForSession()) {
        suspendWeightControl();
        weightStreamState = WeightStreamState::STALE;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_STREAM_STALE);
        serialTrace(LogLevel::WARNING, "Scale stream suspended during brew");
      }

      if (machineAllowsAutomationStop()) {
        const StopperState afterAutomation = nextStateForUserHold(intent);
        if (autoToManualGuardDeadlineDue()) {
          addDebugEvent(DebugCategory::SCALE,
                        DebugCode::AUTO_TO_MANUAL_GUARD_FIRED,
                        static_cast<int32_t>(elapsedMs(session.startedAtMs)));
          finalizeCycle(EndReason::AUTO_TO_MANUAL_GUARD, afterAutomation);
          return;
        }

        if (automaticScaleStopDue()) {
          const EndReason reason = session.directStopPending
                                       ? session.directStopReason
                                       : EndReason::SCALE_THRESHOLD;
          finalizeCycle(reason, afterAutomation);
        }
      }
      return;

    case StopperState::MANUAL_NO_SCALE:
      if (intent.intent == UserIntent::REQUEST_RINSE && machineSupportsRinse()) {
        (void)enterRinse();
        return;
      }
      if (intent.intent == UserIntent::REQUEST_STOP) {
        if (session.automaticEnabled) {
          shot.automaticBrew = true;
        }
        finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
        return;
      }
      if (machineTakeNoFlowIdle()) {
        finalizeCycle(EndReason::UNCONFIRMED_START,
                      nextStateForUserHold(intent));
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
  return stopperState == StopperState::READY && !session.active &&
         !machineIsRunning() && !machineLastIntention().holdActive &&
         !maintenanceLease.active;
}

void beginWebRinse() {
  if (!beginRinseCycle(ControlSource::WEB)) {
    return;
  }
  (void)enterRinse();
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
  if (!controlAllowsConfigurationNow() && !networkCommand.unsafeWebUiOverride) {
    return false;
  }
  // A planned restart must never cut a shot. Override cannot change that.
  if (networkCommand.type == WebCommandType::RESTART &&
      (session.active || getRelaySafetySnapshot().closed ||
       machineLastIntention().holdActive)) {
    return false;
  }
  machineRequestStop();
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

bool webCommandAllowsUnsafeConfiguration(const WebCommand &command) {
  return controlAllowsConfigurationNow() || command.unsafeWebUiOverride;
}

void holdOrBeginPlannedRestart(const WebCommand &command) {
  WebCommand restart = command;
  restart.type = WebCommandType::RESTART;
  restart.unsafeWebUiOverride = false;
  if (!controlAllowsConfigurationNow() ||
      session.active || getRelaySafetySnapshot().closed ||
      machineLastIntention().holdActive) {
    pendingPlannedRestart = restart;
    plannedRestartHeld = true;
    return;
  }
  plannedRestartHeld = false;
  pendingPlannedRestart = WebCommand{};
  if (!beginMaintenanceLease(restart, false)) {
    rejectWebCommand(restart);
  }
}

void servicePendingPlannedRestart() {
  if (!plannedRestartHeld || maintenanceLease.active) {
    return;
  }
  if (!controlAllowsConfigurationNow() || session.active ||
      getRelaySafetySnapshot().closed || machineLastIntention().holdActive) {
    return;
  }
  const WebCommand restart = pendingPlannedRestart;
  plannedRestartHeld = false;
  pendingPlannedRestart = WebCommand{};
  if (!beginMaintenanceLease(restart, false)) {
    rejectWebCommand(restart);
  }
}

void serviceMaintenanceLease() {
  if (!maintenanceLease.active || maintenanceLease.forwarded ||
      !machineLastIntention().stablyOff ||
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
        forwardedType == WebCommandType::CHANGE_DEVICE_PASSWORD ||
        forwardedType == WebCommandType::RESET_DEVICE_PASSWORD ||
        forwardedType == WebCommandType::RESET_NETWORK_AP ||
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
  const bool alertsWereEnabled = soundAlertsEnabled();
  const uint8_t previousNoScaleMode = runtimeConfig.noScaleBbwMode;
  runtimeConfig = composed;
  if (runtimeConfig.noScaleBbwMode != previousNoScaleMode) {
    noScaleShotGuardActivityAtMs = 0;
    noScaleShotGuardArmed = noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
    resetNoScaleRequireBypassGesture();
    if (!noScaleBbwEnabled(runtimeConfig.noScaleBbwMode)) {
      // Changing settings must not turn an already-held activator into a new
      // relay close. The hold clears on release in serviceNoScaleShotGuard().
      noScaleShotGuardHold = machineLastIntention().holdActive;
    }
  }
  if (!soundAlertsEnabled()) {
    cancelOperationalAlerts();
  }
  serialLogLevel = serialLogLevelFromRuntime(runtimeConfig);
  ringRetainLogLevel =
      static_cast<LogLevel>(runtimeConfig.ringRetainLogLevel);
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                static_cast<int32_t>(runtimeConfig.revision));
  requestBookooSilenceIfConfigured();
  if (!alertsWereEnabled && soundAlertsEnabled()) {
    requestBookooAlertVolumeRestore();
  }
  queueRuntimePersist(reasonBits);
#ifndef SHOT_STOPPER_HOST_TEST
  networkManager.syncLiveRuntime(runtimeConfig, &presetBank);
#endif
  publishRecipeState();
}

#ifndef SHOT_STOPPER_HOST_TEST
void settingsPersistTask(void *parameter) {
  (void)parameter;
  if (!subscribeCurrentTaskToWatchdog()) {
    reportTaskWatchdogFault();
  }
  for (;;) {
    // Must not block forever: this task is subscribed to the TWDT.
    if (xQueueReceive(settingsPersistQueue, &settingsPersistReceive,
                      pdMS_TO_TICKS(SETTINGS_PERSIST_IDLE_WAIT_MS)) !=
        pdTRUE) {
      (void)persistResetUptimeCheckpoint(millis());
      feedOrTripCurrentTaskWatchdog();
      continue;
    }
    yieldSettingsNvs();
    feedOrTripCurrentTaskWatchdog();
    const bool ok = savePersistedSettings(settingsPersistReceive.blob);
    feedOrTripCurrentTaskWatchdog();
    portENTER_CRITICAL(&settingsPersistMux);
    settingsPersistResultReady = true;
    settingsPersistResultOk = ok;
    settingsPersistResultRuntimeRevision =
        settingsPersistReceive.runtimeRevision;
    settingsPersistResultStorageRevision =
        ok ? settingsPersistReceive.blob.storageRevision : 0;
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
        scaleHistoryIdentityEqual(persistedSettings.scaleHistory,
                                  liveHistory)) {
      scaleWorkerClearPreferredDirty();
    } else {
      const ScaleLinkSnapshot link = getScaleLinkSnapshot();
      if (scaleMacNvsWriteAllowed(link.connecting,
                                  link.state == ScaleLinkState::CONNECTED)) {
        runtimePersistPending = true;
        runtimePersistReasonBits |= RUNTIME_PERSIST_REASON_SCALE_MAC;
      }
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
  SettingsPersistRequest &request = settingsPersistRequest;
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

void serviceShotStorePersistence() {
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!durableFlashWriteAllowed(session.active || machineIsRunning(),
                                link.connecting)) {
    return;
  }
  if (static_cast<int32_t>(millis() - shotStorePersistRetryAtMs) < 0) {
    return;
  }
  // Do not wait on the durable flash mutex from the 1 ms control loop: a
  // 50 ms take while settings_persist holds the lock stalls pala/weight and
  // can spam SHOT_LOG_PERSIST_FAILED into the 96-event ring.
  constexpr uint32_t kTryLockMs = 0;
  bool anyFail = false;
  if (shotLog.dirty()) {
    if (shotLog.flush(kTryLockMs)) {
      shotLogPersistFailLatched = false;
    } else {
      anyFail = true;
      if (!shotLogPersistFailLatched) {
        shotLogPersistFailLatched = true;
        addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                      static_cast<int32_t>(shotLog.count()), 0);
      }
    }
  }
  if (shotCurves.dirty()) {
    if (shotCurves.flush(kTryLockMs)) {
      shotCurvePersistFailLatched = false;
    } else {
      anyFail = true;
      if (!shotCurvePersistFailLatched) {
        shotCurvePersistFailLatched = true;
        addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                      static_cast<int32_t>(shotCurves.count()), 1);
      }
    }
  }
  if (lastShotNvsDirty) {
    if (lastShotStore.save(kTryLockMs)) {
      lastShotNvsDirty = false;
      lastShotPersistFailLatched = false;
    } else {
      anyFail = true;
      if (!lastShotPersistFailLatched) {
        lastShotPersistFailLatched = true;
        addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                      0, 2);
      }
    }
  }
  shotStorePersistRetryAtMs =
      anyFail ? millis() + SHOT_STORE_PERSIST_RETRY_MS : 0;
}

void serviceRuntimePersistence() {
#ifndef SHOT_STOPPER_HOST_TEST
  serviceSettingsPersistResult();
  bool inFlight = false;
  portENTER_CRITICAL(&settingsPersistMux);
  inFlight = settingsPersistInFlight;
  portEXIT_CRITICAL(&settingsPersistMux);
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!runtimePersistPending || inFlight || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow() ||
      !durableFlashWriteAllowed(false, link.connecting)) {
    return;
  }
  if (!dispatchSettingsPersist()) {
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
#else
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!runtimePersistPending || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow() ||
      !durableFlashWriteAllowed(false, link.connecting)) {
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

bool persistBleCompanionEnabled(bool enabled);
bool persistBleScanIntensity(BleScanIntensity intensity);

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
  if (machineLastIntention().holdActive) {
    transitionTo(StopperState::REQUIRES_OFF);
  }
}

void rejectWebCommand(const WebCommand &command) {
  addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                static_cast<int32_t>(command.type));
  reportControlCommandResult(command, CommandResultState::FAILED);
}

bool persistBleCompanionEnabled(bool enabled);
bool persistBleScanIntensity(BleScanIntensity intensity);

void processWebCommand(const WebCommand &command) {
  switch (command.type) {
    case WebCommandType::STOP:
    case WebCommandType::STOP_HEARTBEAT:
    case WebCommandType::REMOTE_OFF:
      if (!machineIsRunning() ||
          (command.type == WebCommandType::REMOTE_OFF &&
           (!session.active || session.source != ControlSource::WEB))) {
        rejectWebCommand(command);
        return;
      }
      machineNoteFirmwareStop();
      addDebugEvent(DebugCategory::WEB,
                    command.type == WebCommandType::REMOTE_OFF
                        ? DebugCode::WEB_REMOTE_OFF
                        : DebugCode::WEB_STOP,
                    static_cast<int32_t>(command.type),
                    static_cast<int32_t>(session.id));
      if (!session.active) {
        (void)machineRequestStop();
        reportControlCommandResult(command, CommandResultState::APPLIED);
        return;
      }
      finalizeCycle(
          command.type == WebCommandType::STOP_HEARTBEAT
              ? EndReason::WEB_HEARTBEAT_TIMEOUT
              : EndReason::WEB_STOP,
          nextStateForUserHold(machineLastIntention()));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::STATE_OVERRIDE_OFF:
      machineOverrideInferredOff();
      if (session.active) {
        finalizeCycle(EndReason::WEB_STOP,
                      nextStateForUserHold(machineLastIntention()));
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::STATE_OVERRIDE_ON:
      machineOverrideInferredOn();
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::REMOTE_ON:
      if (!REMOTE_MACHINE_CONTROL_ENABLED || !webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      virtualHoldOn = true;
      beginCycle(ControlSource::WEB);
      if (!session.active) {
        virtualHoldOn = false;
        reportControlCommandResult(command, CommandResultState::FAILED);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_REMOTE_ON,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::RINSE:
      if (!machineSupportsRinse() || !REMOTE_MACHINE_CONTROL_ENABLED ||
          !webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      beginWebRinse();
      if (!session.active) {
        reportControlCommandResult(command, CommandResultState::FAILED);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_RINSE,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::APPLY_CONFIG: {
      if (!webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      // Machine/workflow fields only. Recipe authority stays on the active preset.
      RuntimeConfig candidate = runtimeConfig;
      candidate.autoTare = command.config.autoTare;
      candidate.postTareBaselineGraceMs = command.config.postTareBaselineGraceMs;
      candidate.canTareStartTimer = command.config.canTareStartTimer;
      candidate.scaleTimerStopExtraDelayMs =
          command.config.scaleTimerStopExtraDelayMs;
      candidate.dripDelayMs = command.config.dripDelayMs;
      candidate.soundAlertsMuted = command.config.soundAlertsMuted;
      candidate.firstDropBeep = command.config.firstDropBeep;
      machineApplyWorkflowConfig(candidate, command.config);
      candidate.rinseDurationMs = command.config.rinseDurationMs;
      candidate.rinseEnabled = command.config.rinseEnabled;
      candidate.buzzerScaleLostBeep = command.config.buzzerScaleLostBeep;
      candidate.buzzerAutoToManualGuardEndBeep =
          command.config.buzzerAutoToManualGuardEndBeep;
      candidate.buzzerManualNoScaleBeep = command.config.buzzerManualNoScaleBeep;
      candidate.buzzerScaleConnectedBeep =
          command.config.buzzerScaleConnectedBeep;
      candidate.scaleConnectedLed = command.config.scaleConnectedLed;
      candidate.buzzerExtendedPulseRate = command.config.buzzerExtendedPulseRate;
      candidate.buzzerSlowExtendedPulseRate =
          command.config.buzzerSlowExtendedPulseRate;
      candidate.alertOutputChannel = command.config.alertOutputChannel;
      candidate.bookooMuteOnBuzzerOnly = command.config.bookooMuteOnBuzzerOnly;
      candidate.bookooConnectBeepLevel = command.config.bookooConnectBeepLevel;
      candidate.autoRetare = command.config.autoRetare;
      candidate.retareWindowMs = command.config.retareWindowMs;
      candidate.minimumCupWeightG = command.config.minimumCupWeightG;
      candidate.cupRemovedWeightG = command.config.cupRemovedWeightG;
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
        serialTracef(LogLevel::INFO, "Scale preference: %s",
                     scaleMacCacheModeId(command.config.scaleMacCacheMode));
      }
      candidate.scaleMacCacheMode = command.config.scaleMacCacheMode;
      if (scaleMacCacheModeRequiresPreferred(candidate.scaleMacCacheMode) &&
          !hasPreferredScaleMac()) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        rejectWebCommand(command);
        return;
      }
      candidate.noScaleBbwMode = command.config.noScaleBbwMode;
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
      if (!webCommandAllowsUnsafeConfiguration(command)) {
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
      if (!webCommandAllowsUnsafeConfiguration(command)) {
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
      if (!webCommandAllowsUnsafeConfiguration(command)) {
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

    case WebCommandType::RESTART:
      holdOrBeginPlannedRestart(command);
      return;

    case WebCommandType::SAVE_NETWORK:
    case WebCommandType::FORGET_NETWORK:
    case WebCommandType::CHANGE_DEVICE_PASSWORD:
    case WebCommandType::RESET_DEVICE_PASSWORD:
    case WebCommandType::RESET_NETWORK_AP:
    case WebCommandType::FACTORY_RESET:
      if (!webCommandAllowsUnsafeConfiguration(command)) {
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
      if (!webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      clearPreferredScaleCache();
      servicePreferredScaleMacPersistence();
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::SELECT_PREFERRED_SCALE:
      if (!webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      if (command.scaleSelectMac[0] == '\0') {
        selectPreferredScale("", "");
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
      if (!webCommandAllowsUnsafeConfiguration(command)) {
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

    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
      if (!forwardAcceptedNetworkCommand(command)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::BLE_COMPAT_ENABLE:
    case WebCommandType::BLE_COMPAT_DISABLE: {
      const bool enabled =
          command.type == WebCommandType::BLE_COMPAT_ENABLE;
      if (!persistBleCompanionEnabled(enabled)) {
        rejectWebCommand(command);
        return;
      }
      if (command.bleScanIntensitySpecified &&
          !persistBleScanIntensity(clampBleScanIntensity(
              command.bleScanIntensity))) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                    enabled ? 1 : 0);
      reportControlCommandResult(command, CommandResultState::PERSISTED);
      return;
    }

    case WebCommandType::BLE_SCAN_INTENSITY: {
      if (!persistBleScanIntensity(clampBleScanIntensity(
              command.bleScanIntensity))) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                    static_cast<int32_t>(command.bleScanIntensity));
      reportControlCommandResult(command, CommandResultState::PERSISTED);
      return;
    }

    case WebCommandType::TASK_PROFILER_START:
      (void)taskProfiler.start(millis());
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::TASK_PROFILER_STOP:
      (void)taskProfiler.stop(millis());
      reportControlCommandResult(command, CommandResultState::APPLIED);
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

void reportBleCompanionResult(const BleCompanionRequest &request,
                              bool accepted,
                              BleCompanionRejectReason reason) {
  BleCompanionResult result;
  result.sequence = request.sequence;
  result.accepted = accepted;
  result.reason = accepted ? BleCompanionRejectReason::NONE : reason;
  if (bleCompanionResultQueue != nullptr) {
    if (xQueueSend(bleCompanionResultQueue, &result, 0) != pdTRUE) {
      ++bleCompanionResultDropped;
      addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_EVENT_DROPPED,
                    static_cast<int32_t>(request.type));
      logEmit(LogLevel::WARNING, DebugCategory::SCALE,
              DebugCode::SCALE_EVENT_DROPPED,
              static_cast<int32_t>(request.sequence),
              static_cast<int32_t>(request.type));
    }
  }
}

bool persistBleScanIntensity(BleScanIntensity intensity) {
  intensity = clampBleScanIntensity(static_cast<uint8_t>(intensity));
#if !defined(SHOT_STOPPER_HOST_TEST)
  const uint8_t stored = static_cast<uint8_t>(intensity);
  if (bleCompanionPersistedSettings.scanIntensity != stored) {
    BleCompanionPersistedSettings candidate = bleCompanionPersistedSettings;
    candidate.scanIntensity = stored;
    if (!saveBleCompanionSettings(candidate)) {
      return false;
    }
    bleCompanionPersistedSettings = candidate;
  }
#endif
  applyLiveBleScanIntensity(intensity);
  return true;
}

bool persistBleCompanionEnabled(bool enabled) {
#if !defined(SHOT_STOPPER_HOST_TEST)
  if ((bleCompanionPersistedSettings.enabled != 0) != enabled) {
    BleCompanionPersistedSettings candidate = bleCompanionPersistedSettings;
    candidate.enabled = enabled ? 1 : 0;
    if (!saveBleCompanionSettings(candidate)) {
      return false;
    }
    bleCompanionPersistedSettings = candidate;
  }
#endif
  portENTER_CRITICAL(&bleCompanionMux);
  bleCompanionStatusSnapshot.configuredEnabled = enabled;
  bleCompanionStatusSnapshot.restartRequired =
      enabled != bleCompanionStatusSnapshot.enabled;
  bleCompanionRuntimeSnapshot.configuredEnabled = enabled;
  portEXIT_CRITICAL(&bleCompanionMux);
  return true;
}

bool applyBleCompanionRecipeRequest(const BleCompanionRequest &request) {
  RuntimeConfig candidateRuntime = runtimeConfig;
  ShotPresetBank candidateBank = presetBank;
  ensureShotPresetBank(candidateBank, candidateRuntime.retareWindowMs,
                       candidateRuntime.autoRetare);
  ShotPreset &candidatePreset = mutableActiveShotPreset(candidateBank);
  uint32_t milliseconds = 0;

  switch (request.type) {
    case BleCompanionRequestType::SET_BREW_BY_WEIGHT:
      if (request.value > 1) return false;
      candidatePreset.brewByWeight = request.value != 0;
      break;
    case BleCompanionRequestType::SET_GOAL_WEIGHT:
      candidatePreset.goalWeightG = request.value;
      break;
    case BleCompanionRequestType::SET_AUTO_TARE:
      if (request.value > 1) return false;
      candidateRuntime.autoTare = request.value != 0;
      break;
    case BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS:
      bleCompanionSecondsToMs(request.value, milliseconds);
      candidatePreset.bbwProtectionMs = milliseconds;
      break;
    case BleCompanionRequestType::SET_OPERATIONAL_WALL_SECONDS:
      bleCompanionSecondsToMs(request.value, milliseconds);
      candidatePreset.operationalWallMs = milliseconds;
      break;
    case BleCompanionRequestType::SET_DRIP_DELAY_SECONDS:
      bleCompanionSecondsToMs(request.value, milliseconds);
      candidateRuntime.dripDelayMs = milliseconds;
      break;
    default:
      return false;
  }

  const RuntimeConfig composed =
      composeEffectiveConfig(candidateRuntime, candidateBank);
  if (validateRuntimeConfig(composed) != ConfigValidationError::NONE ||
      !validateShotPresetBank(candidateBank, candidateRuntime.retareWindowMs,
                              candidateRuntime.autoRetare)) {
    return false;
  }
  const RuntimeConfig currentEffective = effectiveRuntimeConfig();
  if (memcmp(&candidateBank, &presetBank, sizeof(candidateBank)) == 0 &&
      memcmp(&composed, &currentEffective, sizeof(composed)) == 0) {
    return true;
  }
  presetBank = candidateBank;
  RuntimeConfig committed = composed;
  committed.revision = runtimeConfig.revision + 1;
  if (committed.revision == 0) {
    committed.revision = 1;
  }
  commitLiveRuntimeConfig(committed, RUNTIME_PERSIST_REASON_USER);
  return true;
}

void processBleCompanionRequests() {
  if (bleCompanionRequestQueue == nullptr) {
    return;
  }
  BleCompanionRequest request;
  if (xQueueReceive(bleCompanionRequestQueue, &request, 0) != pdTRUE) {
    return;
  }

  if (!copyBleCompanionStatus().enabled) {
    reportBleCompanionResult(request, false,
                             BleCompanionRejectReason::SUPPORT_DISABLED);
    return;
  }

  if (request.type == BleCompanionRequestType::SET_AP_ENABLED) {
    if (request.value > 1) {
      reportBleCompanionResult(request, false,
                               BleCompanionRejectReason::INVALID_VALUE);
      return;
    }
    WebCommand command;
    command.type = request.value != 0 ? WebCommandType::AP_START
                                      : WebCommandType::AP_STOP;
    command.requestId = request.sequence;
    const bool queued = enqueueWebCommand(command);
    reportBleCompanionResult(
        request, queued, queued ? BleCompanionRejectReason::NONE
                                : BleCompanionRejectReason::QUEUE_FULL);
    return;
  }

  if (!controlAllowsConfigurationNow()) {
    reportBleCompanionResult(request, false,
                             BleCompanionRejectReason::NOT_READY);
    return;
  }

  switch (request.type) {
    case BleCompanionRequestType::SET_BREW_BY_WEIGHT:
    case BleCompanionRequestType::SET_GOAL_WEIGHT:
    case BleCompanionRequestType::SET_AUTO_TARE:
    case BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS:
    case BleCompanionRequestType::SET_OPERATIONAL_WALL_SECONDS:
    case BleCompanionRequestType::SET_DRIP_DELAY_SECONDS: {
      const bool accepted = applyBleCompanionRecipeRequest(request);
      reportBleCompanionResult(
          request, accepted,
          accepted ? BleCompanionRejectReason::NONE
                   : BleCompanionRejectReason::INVALID_VALUE);
      return;
    }
    case BleCompanionRequestType::SAVE_WIFI: {
      if (!validWifiSsid(request.ssid) ||
          !validWifiPassword(request.openNetwork ? "" : request.password,
                             request.openNetwork)) {
        reportBleCompanionResult(request, false,
                                 BleCompanionRejectReason::INVALID_VALUE);
        return;
      }
      WebCommand command;
      command.type = WebCommandType::SAVE_NETWORK;
      command.requestId = request.sequence;
      copyCString(command.ssid, sizeof(command.ssid), request.ssid);
      command.openNetwork = request.openNetwork;
      if (!request.openNetwork) {
        copyCString(command.password, sizeof(command.password),
                    request.password);
      }
      const bool queued = enqueueWebCommand(command);
      memset(command.password, 0, sizeof(command.password));
      reportBleCompanionResult(
          request, queued, queued ? BleCompanionRejectReason::NONE
                                  : BleCompanionRejectReason::QUEUE_FULL);
      return;
    }
    case BleCompanionRequestType::REBOOT: {
      if (request.value != 1) {
        reportBleCompanionResult(request, false,
                                 BleCompanionRejectReason::INVALID_PAYLOAD);
        return;
      }
      WebCommand command;
      command.type = WebCommandType::RESTART;
      command.requestId = request.sequence;
      const bool queued = enqueueWebCommand(command);
      reportBleCompanionResult(
          request, queued, queued ? BleCompanionRejectReason::NONE
                                  : BleCompanionRejectReason::QUEUE_FULL);
      return;
    }
    case BleCompanionRequestType::SET_AP_ENABLED:
      return;
  }
}

bool liveConfigPersistPending() {
#ifndef SHOT_STOPPER_HOST_TEST
  portENTER_CRITICAL(&settingsPersistMux);
  const bool pending = runtimePersistPending || settingsPersistInFlight ||
                       settingsPersistResultReady;
  portEXIT_CRITICAL(&settingsPersistMux);
  return pending;
#else
  return runtimePersistPending;
#endif
}

void publishControlGate() {
  ControlGateSnapshot next;
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  next.state = stopperState;
  next.activeCycle = session.active;
  next.relayClosed = relay.closed;
  next.machineRunning = machineIsRunning();
  next.physicalActivatorOn = rawActivatorOn;
  next.maintenanceLeaseActive = maintenanceLease.active;
  next.maintenanceLeaseId = maintenanceLease.active ? maintenanceLease.id : 0;
  next.source = session.active ? session.source : ControlSource::NONE;
  __atomic_fetch_add(&controlGateSeq, 1U, __ATOMIC_RELAXED);
  publishedControlGate = next;
  __atomic_fetch_add(&controlGateSeq, 1U, __ATOMIC_RELEASE);
}

void publishControlStatus() {
  const uint32_t now = millis();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
  __atomic_fetch_add(&controlStatusSeq, 1U, __ATOMIC_RELAXED);
  ControlStatusSnapshot &next = publishedControlStatus;
  next = ControlStatusSnapshot{};
  next.state = stopperState;
  next.activeCycle = session.active;
  next.relayClosed = relay.closed;
  next.machineRunning = machineIsRunning();
  machineFillStatus(next);
  next.virtualHoldOn = virtualHoldOn;
  next.remoteControlEnabled = REMOTE_MACHINE_CONTROL_ENABLED;
  next.source = session.active ? session.source : ControlSource::NONE;
  next.cycleId = session.active ? session.id : 0;
  next.bootId = shotLog.bootId();
  next.maintenanceLeaseActive = maintenanceLease.active;
  next.maintenanceLeaseId = maintenanceLease.active ? maintenanceLease.id : 0;
  next.maintenanceStartedAtMs =
      maintenanceLease.active ? maintenanceLease.startedAtMs : 0;
  next.safetyState = relay.state;
  next.safetyFault = relay.fault;
  next.safetyGeneration = relay.generation;
  next.safetyTimersReady = relay.timersReady;
  next.taskWatchdogReady = relay.watchdogReady;
  next.externalSafetyPresent = relay.externalSafetyPresent;
  next.circuitFeedbackClosed = relay.feedbackClosed;
  next.resetReasonCode = relay.resetReasonCode;
  next.unsafeResetCount = relay.unsafeResetCount;
  next.resetRecoveryRequired = relay.resetRecoveryRequired;
  next.bootLoopDetected = relay.bootLoopDetected;
  next.resetHistoryCount = relay.resetHistoryCount;
  for (uint8_t i = 0; i < next.resetHistoryCount; ++i)
    next.resetHistory[i] = relay.resetHistory[i];
  next.scaleAvailable = scaleLinkAvailable(scaleLink);
  next.weightControlState = session.active
                                ? session.weightControlState
                                : WeightControlState::INACTIVE;
  serviceWeightStreamTelemetry();
  next.weightStreamState = telemetryWeightStreamState;
  next.scaleRecoveredStaleCount = scaleRecoveredStaleCount;
  next.scaleRecoveredStaleMs = scaleRecoveredStaleMs;
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
  next.scaleRssiValid = scaleLink.rssiValid;
  next.scaleRssi = scaleLink.rssi;
  next.uptimeMs = elapsedMs(bootStartedAtMs);
  next.loopIntervalGapMs =
      healthIntervalMaxGapMs > loopIntervalGapMs ? healthIntervalMaxGapMs
                                                 : loopIntervalGapMs;
  next.loopMaxGapMs = loopMaxGapMs;
  next.loopStackMinWords = loopStackMinWords;
  next.scaleStackMinWords = scaleWorkerStackMinWords;
  next.freeHeapBytes = freeHeapBytes;
  next.minimumFreeHeapBytes = minimumFreeHeapBytes;
  next.largestFreeHeapBlockBytes = largestFreeHeapBlockBytes;
  next.psramSizeBytes = psramSizeBytes;
  next.psramFreeBytes = psramFreeBytes;
  next.psramLargestFreeBlockBytes = psramLargestFreeBlockBytes;
  next.bleHostAllocPsramCount = bleHostAllocPsramCount;
  next.bleHostAllocFallbackCount = bleHostAllocFallbackCount;
  next.bleHostHciRxDropped = bleHostHciRxDropped;
  next.bleHostHciTxDropped = bleHostHciTxDropped;
  next.workBufExternal = workBufIsExternal();
#ifndef SHOT_STOPPER_HOST_TEST
  next.jsonArenaExternal = jsonArenaIsExternal();
#else
  next.jsonArenaExternal = false;
#endif
  next.allocExternalFallbackCount = allocExternalFallbackCount();
  next.hwmon = hwmonSnapshot;
  next.scaleEventsDropped = scaleEventsDropped;
  next.config = effectiveRuntimeConfig();
  next.lastCycle = lastCycle;
  next.lastShot = persistedLastShot;
  {
    const ShotCurveRecord curve =
        session.active ? shotCurveSampler.snapshot() : lastShotCurve;
    copyShotCurveRecordToStatusFields(
        curve, next.shotCurveCount, next.shotCurveIntervalS,
        next.shotCurveFirstDropDs, next.shotCurveFirstDropCg,
        next.shotCurveExtendedDs, next.shotCurveExtendedCg,
        next.shotCurveAtmDs, next.shotCurveAtmCg,
        next.shotCurveAtmClearedDs, next.shotCurveEndedDs,
        next.shotCurveEndedCg, next.shotCurveWeightCg,
        sizeof(next.shotCurveWeightCg) / sizeof(next.shotCurveWeightCg[0]));
  }
  copyCString(next.scaleProtocol, sizeof(next.scaleProtocol),
              scaleLink.protocolName);
  copyPreferredScaleMac(next.preferredScaleMac, sizeof(next.preferredScaleMac));
  copyPreferredScaleName(next.preferredScaleName,
                         sizeof(next.preferredScaleName));
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
    next.cycleStartedAtMs = session.circuitClosedAtMs != 0U
                                ? session.circuitClosedAtMs
                                : session.startedAtMs;
    next.cycleElapsedMs = cycleShotElapsedMs();
    next.cycleExtractionExtended =
        session.extractionExtended && fastExtractionGuardSession();
    next.cycleSlowExtractionExtended =
        session.slowExtractionExtended && slowExtractionGuardSession();
    next.cycleTargetReachedEarly = session.targetReachedEarly;
    if (next.cycleExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.maxRecoveryWeightG;
      const uint32_t elapsed = next.cycleElapsedMs;
      next.cycleMinBbwBrewTimeRemainingMs =
          elapsed >= session.config.minBbwBrewTimeMs
              ? 0U
              : session.config.minBbwBrewTimeMs - elapsed;
    } else if (next.cycleSlowExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.minRecoveryWeightG;
      next.cycleMinBbwBrewTimeRemainingMs = 0;
    } else {
      next.cycleActiveStopWeightG =
          static_cast<float>(session.config.goalWeightG);
      next.cycleMinBbwBrewTimeRemainingMs = 0;
    }
    next.cycleAutoToManualGuardArmed = session.autoToManualGuardArmed;
    next.cycleAutoToManualGuardEnforced = session.autoToManualGuardEnforced;
    next.cycleAccidentalTouchHolding = session.accidentalTouchHolding;
    next.cycleAccidentalTouchPhase =
        static_cast<uint8_t>(session.accidentalTouchPhase);
    next.cycleAccidentalTouchClass =
        static_cast<uint8_t>(session.accidentalTouchClass);
    next.cycleAccidentalTouchPendingCount = session.accidentalTouchPendingCount;
    next.cycleCupRemovedPending = session.cupRemovedPending;
    next.cycleBbwProtectionEnabled = session.bbwProtectionEnabled;
    next.cycleBbwProtectionEnded = session.bbwProtectionEnded;
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
  next.noScaleShotGuardEnabled = noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
  next.noScaleShotGuardArmed = noScaleShotGuardArmed;
  next.noScaleShotGuardHold = noScaleShotGuardHold;
  next.noScaleShotGuardScaleWasAvailable = noScaleShotGuardScaleWasAvailable;
  next.noScaleShotGuardCooldownRemainingMs = 0;
  if (noScaleBbwEnabled(runtimeConfig.noScaleBbwMode) &&
      !noScaleShotGuardArmed && noScaleShotGuardActivityAtMs != 0) {
    const uint32_t elapsed = elapsedMs(noScaleShotGuardActivityAtMs);
    next.noScaleShotGuardCooldownRemainingMs =
        elapsed >= runtimeConfig.lastShotCooldownMs
            ? 0U
            : runtimeConfig.lastShotCooldownMs - elapsed;
  }
  next.cupStartGuardHold = cupStartGuardHold;
  next.machineRunState = machineRunState();
  next.cupPresenceState = cupPresenceState();
  next.cupPresent = cupPresenceState() == CupPresenceState::PRESENT;
  next.configPersistPending = liveConfigPersistPending();
  next.configPersistFailed = runtimePersistFailed;
  next.bootComplete = firmwareInitializationComplete;
  next.bootDegraded = bootDegraded;
  next.scaleWorkerReady =
      scaleWorkerTaskHandle != nullptr && bleStackReady;
  next.usbConsoleIo4Closed = usbConsoleJumperPresent();
  next.usbSerialEnableSource = usbSerialEnableSource;
  next.bleCompanionResultDropped = bleCompanionResultDropped;
  {
    const BleCompanionStatusSnapshot ble = copyBleCompanionStatus();
    next.bleCompanionEnabled = ble.configuredEnabled;
    next.bleCompanionActive = ble.enabled;
    next.bleCompanionRestartRequired = ble.restartRequired;
    next.bleCompanionStackReady = ble.stackReady;
    next.bleCompanionAdvertising = ble.advertising;
    next.bleCompanionConnected = ble.connected;
    next.bleCompanionProtocolVersion = ble.protocolVersion;
    next.bleCompanionAcceptedWrites = ble.acceptedWrites;
    next.bleCompanionRejectedWrites = ble.rejectedWrites;
    next.bleCompanionLastReject = static_cast<uint8_t>(ble.lastReject);
    next.bleCompanionScanIntensity =
        static_cast<uint8_t>(liveBleScanIntensity());
  }
  portENTER_CRITICAL(&debugLogMux);
  next.debugEventsDropped = debugLog.overwritten();
  portEXIT_CRITICAL(&debugLogMux);
  __atomic_fetch_add(&controlStatusSeq, 1U, __ATOMIC_RELEASE);
  publishControlGate();
}

void refreshControlStatus() {
#if defined(SHOT_STOPPER_HOST_TEST)
  return;
#else
  // Waits until loop commits the status seqlock, not merely until the request
  // is noticed. Timeout copies the last snapshot and leaves the flag set.
  __atomic_store_n(&controlStatusPublishRequested, true, __ATOMIC_RELEASE);
  const uint32_t startedAtMs = millis();
  while (__atomic_load_n(&controlStatusPublishRequested, __ATOMIC_ACQUIRE)) {
    if (static_cast<uint32_t>(millis() - startedAtMs) >=
        CONTROL_STATUS_REFRESH_WAIT_MS) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
#endif
}

void serviceControlStatusPublish() {
  publishControlGate();
  serviceWeightStreamTelemetry();
  const RelaySafetySnapshot relaySnap = getRelaySafetySnapshot();
  const bool requested =
      __atomic_load_n(&controlStatusPublishRequested, __ATOMIC_ACQUIRE);
  const bool forcePublish =
      publishedControlStatus.state != stopperState ||
      publishedControlStatus.activeCycle != session.active ||
      publishedControlStatus.relayClosed != relaySnap.closed ||
      publishedControlStatus.machineRunning != machineIsRunning() ||
      publishedControlStatus.safetyGeneration != relaySnap.generation ||
      publishedControlStatus.safetyFault != relaySnap.fault ||
      publishedControlStatus.configPersistPending !=
          liveConfigPersistPending() ||
      publishedControlStatus.configPersistFailed != runtimePersistFailed;
  if (requested || forcePublish) {
    publishControlStatus();
  }
  // Clear after the seqlock commit so httpd's wait cannot copy the previous
  // blob. A GET that arrives mid-publish still sees the snapshot that just
  // finished.
  if (requested) {
    __atomic_store_n(&controlStatusPublishRequested, false, __ATOMIC_RELEASE);
  }
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
      "ERR not ready: paddle OFF, machine circuit open, Ready, no active cycle");
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

void serialCliWarnIfCycleActive() {
  if (session.active) {
    serialCliReply("WARN cycle active; proceeding");
  }
}

void serialCliQueueNetworkAction(WebCommandType type, SerialCliVerb verb) {
  serialCliWarnIfCycleActive();
  WebCommand command;
  command.type = type;
  command.requestId = millis();
  if (!forwardAcceptedNetworkCommand(command)) {
    serialCliReply("ERR network queue full");
    return;
  }
  char line[48] = {};
  snprintf(line, sizeof(line), "OK queued %s", serialCliVerbName(verb));
  serialCliReply(line);
}

void serialCliFillNetworkDump(SerialCliNetworkDump &dump) {
#ifndef SHOT_STOPPER_HOST_TEST
  const NetworkStatusSnapshot snap = networkManager.snapshot();
  dump.networkActive = snap.networkActive;
  dump.apActive = snap.apActive;
  dump.httpActive = snap.httpActive;
  dump.wifiConfigured = snap.wifiConfigured;
  dump.staOpen = snap.staOpen;
  dump.staLinkMetricsValid = snap.staLinkMetricsValid;
  dump.staReconnectHeld = snap.staReconnectHeld;
  dump.apStartHeld = snap.apStartHeld;
  dump.httpStartHeld = snap.httpStartHeld;
  dump.devicePasswordFactory = snap.devicePasswordFactory;
  dump.ntpMayArm = snap.ntpMayArm;
  dump.apClients = snap.apClients;
  dump.staState = static_cast<uint8_t>(snap.staState);
  dump.staIpMode = snap.staIpMode;
  dump.staConfigState = snap.staConfigState;
  dump.wifiMode = snap.wifiMode;
  dump.channel = snap.channel;
  dump.scanState = snap.scanState;
  dump.ntpState = snap.ntpState;
  dump.staRssi = snap.staRssi;
  dump.staSignalQualityPct = snap.staSignalQualityPct;
  dump.wifiStatus = snap.wifiStatus;
  dump.confirmRemainingMs = snap.confirmRemainingMs;
  dump.taskAgeMs = snap.taskAgeMs;
  dump.taskStackMinWords = snap.taskStackMinWords;
  dump.startupFailures = snap.startupFailures;
  dump.lastCommandRequestId = snap.lastCommandRequestId;
  dump.staConnectAgeMs = snap.staConnectAgeMs;
  dump.staReconnectAgeMs = snap.staReconnectAgeMs;
  dump.lastCommandState = snap.lastCommandState;
  copyCString(dump.apIp, sizeof(dump.apIp), snap.apIp);
  copyCString(dump.staIp, sizeof(dump.staIp), snap.staIp);
  copyCString(dump.staSsid, sizeof(dump.staSsid), snap.staSsid);
  copyCString(dump.configuredIp, sizeof(dump.configuredIp), snap.configuredIp);
  copyCString(dump.configuredNetmask, sizeof(dump.configuredNetmask),
              snap.configuredNetmask);
  copyCString(dump.configuredGateway, sizeof(dump.configuredGateway),
              snap.configuredGateway);
  copyCString(dump.configuredDns1, sizeof(dump.configuredDns1),
              snap.configuredDns1);
  copyCString(dump.configuredDns2, sizeof(dump.configuredDns2),
              snap.configuredDns2);
  copyCString(dump.staMac, sizeof(dump.staMac), snap.staMac);
  copyCString(dump.staBssid, sizeof(dump.staBssid), snap.staBssid);
  copyCString(dump.apMac, sizeof(dump.apMac), snap.apMac);
  copyCString(dump.ntpActiveServer, sizeof(dump.ntpActiveServer),
              snap.ntpActiveServer);
#else
  (void)dump;
#endif
}

void serialCliPrintLiveNetworkStatus(SerialCliVerb verb) {
  SerialCliNetworkDump dump;
  serialCliFillNetworkDump(dump);
  if (verb == SerialCliVerb::WIFI_STATUS) {
    serialCliPrintWifiStatus(dump);
  } else if (verb == SerialCliVerb::AP_STATUS) {
    serialCliPrintApStatus(dump);
  } else if (verb == SerialCliVerb::WEBUI_STATUS) {
    serialCliPrintWebuiStatus(dump);
  } else {
    serialCliPrintNetStatus(dump);
  }
}

void serialCliPrintLiveHealth() {
  SerialCliHealthDump dump;
  dump.freeHeapBytes = freeHeapBytes;
  dump.minimumFreeHeapBytes = minimumFreeHeapBytes;
  dump.largestFreeHeapBlockBytes = largestFreeHeapBlockBytes;
  dump.psramSizeBytes = psramSizeBytes;
  dump.psramFreeBytes = psramFreeBytes;
  dump.psramLargestFreeBlockBytes = psramLargestFreeBlockBytes;
  dump.bleHostAllocPsramCount = bleHostAllocPsramCount;
  dump.bleHostAllocFallbackCount = bleHostAllocFallbackCount;
  dump.bleHostHciRxDropped = bleHostHciRxDropped;
  dump.bleHostHciTxDropped = bleHostHciTxDropped;
  dump.workBufExternal = workBufIsExternal();
#ifndef SHOT_STOPPER_HOST_TEST
  dump.jsonArenaExternal = jsonArenaIsExternal();
#else
  dump.jsonArenaExternal = false;
#endif
  dump.allocExternalFallbackCount = allocExternalFallbackCount();
  dump.loopMaxGapMs = loopMaxGapMs;
  dump.healthIntervalMaxGapMs =
      healthIntervalMaxGapMs > loopIntervalGapMs ? healthIntervalMaxGapMs
                                                 : loopIntervalGapMs;
  dump.loopStackMinWords = loopStackMinWords;
  dump.scaleWorkerStackMinWords = scaleWorkerStackMinWords;
#ifndef SHOT_STOPPER_HOST_TEST
  dump.networkStackMinWords = networkManager.snapshot().taskStackMinWords;
#endif
  dump.heapAlertLatched = healthHeapAlertLatched;
  dump.stackAlertLatched = healthStackAlertLatched;
  dump.loopGapAlertLatched = healthLoopGapAlertLatched;
  dump.cpuLoadValid = hwmonSnapshot.cpuLoadValid;
  dump.cpuMhz = hwmonSnapshot.cpuMhz;
  dump.cpuLoad5s = hwmonSnapshot.cpuLoad5s;
  dump.cpuLoad1m = hwmonSnapshot.cpuLoad1m;
  dump.cpuLoad5m = hwmonSnapshot.cpuLoad5m;
  dump.cpu0Busy = hwmonSnapshot.cpu0Busy;
  dump.cpu1Busy = hwmonSnapshot.cpu1Busy;
  dump.tempValid = hwmonSnapshot.tempValid;
  dump.tempC = hwmonSnapshot.tempC;
  dump.tempPeakC = hwmonSnapshot.tempPeakC;
  serialCliPrintHealth(dump);
}

void serialCliPrintLiveScaleStatus() {
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  SerialCliScaleDump dump;
  dump.state = link.state == ScaleLinkState::CONNECTED ? "CONNECTED"
                                                       : "DISCONNECTED";
  dump.protocolName = link.protocolName;
  copyPreferredScaleMac(dump.preferredMac, sizeof(dump.preferredMac));
  copyPreferredScaleName(dump.preferredName, sizeof(dump.preferredName));
  dump.disconnectSequence = link.disconnectSequence;
  dump.connectionGeneration = link.connectionGeneration;
  dump.packetSequence = link.packetSequence;
  dump.packetGaps = link.packetGaps;
  dump.rejectedPackets = link.rejectedPackets;
  dump.reconnects = link.reconnects;
  dump.recoveredStaleCount = scaleRecoveredStaleCount;
  dump.recoveredStaleMs = scaleRecoveredStaleMs;
  dump.lastDisconnectReason = link.lastDisconnectReason;
  dump.rssiValid = link.rssiValid;
  dump.rssi = link.rssi;
  dump.workerAgeMs = elapsedMs(link.workerProgressAtMs);
  dump.timerValid = link.timerValid;
  dump.timerMs = link.timerMs;
  dump.timerAgeMs = link.timerAgeMs;
  dump.weightFresh = currentWeightIsFresh();
  dump.currentWeightG = currentWeight;
  serialCliPrintScaleStatus(dump);
}

void serialCliPrintLiveNtpStatus() {
  const TimeStatusSnapshot time = g_wallClock.snapshot(millis());
  SerialCliNtpDump dump;
  dump.state = time.state;
  dump.utcSec = time.utcSec;
  dump.lastSyncAgeMs = time.lastSyncAgeMs;
  dump.nextRetryInMs = time.nextRetryInMs;
  dump.consecutiveFailures = time.consecutiveFailures;
  copyCString(dump.activeServer, sizeof(dump.activeServer), time.activeServer);
  dump.ntpServerPreset = runtimeConfig.ntpServerPreset;
  copyCString(dump.ntpServerCustom, sizeof(dump.ntpServerCustom),
              runtimeConfig.ntpServerCustom);
  dump.timezoneOffsetMinutes = runtimeConfig.timezoneOffsetMinutes;
#ifndef SHOT_STOPPER_HOST_TEST
  dump.staUp = networkManager.snapshot().staState == StaState::CONNECTED;
#else
  dump.staUp = false;
#endif
  serialCliPrintNtpStatus(dump);
}

void serialCliPrintBleCompanionStatus() {
  const BleCompanionStatusSnapshot ble = copyBleCompanionStatus();
  Serial.println("BLE_COMPAT_STATUS");
  Serial.print("configuredEnabled=");
  Serial.println(ble.configuredEnabled ? 1 : 0);
  Serial.print("activeThisBoot=");
  Serial.println(ble.enabled ? 1 : 0);
  Serial.print("restartRequired=");
  Serial.println(ble.restartRequired ? 1 : 0);
  Serial.print("stackReady=");
  Serial.println(ble.stackReady ? 1 : 0);
  Serial.print("advertising=");
  Serial.println(ble.advertising ? 1 : 0);
  Serial.print("connected=");
  Serial.println(ble.connected ? 1 : 0);
  Serial.print("protocol=");
  Serial.println(ble.protocolVersion);
  Serial.print("apActive=");
  Serial.println(ble.apActive ? 1 : 0);
  Serial.print("acceptedWrites=");
  Serial.println(ble.acceptedWrites);
  Serial.print("rejectedWrites=");
  Serial.println(ble.rejectedWrites);
  Serial.print("lastReject=");
  Serial.println(bleCompanionRejectReasonName(ble.lastReject));
  Serial.print("scanIntensity=");
  Serial.println(bleScanIntensityName(liveBleScanIntensity()));
}

void serialCliPrintLiveLogDump() {
  if (session.active || circuitClosed) {
    serialCliReply("ERR LOG dump deferred; circuit/cycle active");
    return;
  }
  size_t count = 0;
  portENTER_CRITICAL(&debugLogMux);
  count = debugLog.countAfter(0);
  portEXIT_CRITICAL(&debugLogMux);
  serialCliPrintLogDumpPreamble(count, ringRetainLogLevel);
  uint32_t after = 0;
  for (;;) {
    DebugEvent event;
    bool have = false;
    portENTER_CRITICAL(&debugLogMux);
    have = debugLog.copyFirstAfter(after, event);
    portEXIT_CRITICAL(&debugLogMux);
    if (!have) {
      break;
    }
    after = event.sequence;
    writeSerialLogLine(event);
  }
}

void serialCliApplyDebugPersist(bool serialOn, LogLevel ringLevel,
                                const char *okMessage) {
  if (!serialOn) {
    serialCliReply(okMessage);
  }
  RuntimeConfig candidate = runtimeConfig;
  candidate.serialDebugOutput = serialOn;
  candidate.ringRetainLogLevel = static_cast<uint8_t>(ringLevel);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
  if (serialOn) {
    serialCliReply(okMessage);
  }
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
      serialCliQueueCommand(command, request.verb);
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
    case SerialCliVerb::RESET_DEVICE_PASSWORD: {
      WebCommand command;
      command.type = WebCommandType::RESET_DEVICE_PASSWORD;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::SET_DEVICE_PASSWORD: {
      WebCommand command;
      command.type = WebCommandType::CHANGE_DEVICE_PASSWORD;
      copyCString(command.password, sizeof(command.password), request.arg1);
      serialCliQueueIfSafe(command, request.verb);
      memset(request.arg1, 0, sizeof(request.arg1));
      return;
    }
    case SerialCliVerb::SET_WIFI: {
      WebCommand command;
      command.type = WebCommandType::SAVE_NETWORK;
      copyCString(command.ssid, sizeof(command.ssid), request.arg1);
      copyCString(command.password, sizeof(command.password), request.arg2);
      command.openNetwork = request.openNetwork;
      command.commitConfirmed = true;
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
    case SerialCliVerb::RESET_NETWORK_AP: {
      WebCommand command;
      command.type = WebCommandType::RESET_NETWORK_AP;
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
    case SerialCliVerb::DEBUG_FULL:
      serialCliApplyDebugPersist(true, LogLevel::DEBUG, "OK debug full");
      return;
    case SerialCliVerb::DEBUG_OFF:
      serialCliApplyDebugPersist(false, LogLevel::NONE, "OK debug off");
      return;
    case SerialCliVerb::DEBUG_STATUS:
      serialCliPrintDebugStatus(runtimeConfig.serialDebugOutput, serialLogLevel,
                                ringRetainLogLevel);
      return;
    case SerialCliVerb::WIFI_CONNECT:
      serialCliQueueNetworkAction(WebCommandType::WIFI_CONNECT, request.verb);
      return;
    case SerialCliVerb::WIFI_DISCONNECT:
      serialCliQueueNetworkAction(WebCommandType::WIFI_DISCONNECT,
                                  request.verb);
      return;
    case SerialCliVerb::WIFI_RESTART:
      serialCliQueueNetworkAction(WebCommandType::WIFI_RESTART, request.verb);
      return;
    case SerialCliVerb::AP_START:
      serialCliQueueNetworkAction(WebCommandType::AP_START, request.verb);
      return;
    case SerialCliVerb::AP_STOP:
      serialCliQueueNetworkAction(WebCommandType::AP_STOP, request.verb);
      return;
    case SerialCliVerb::WEBUI_START:
      serialCliQueueNetworkAction(WebCommandType::WEBUI_START, request.verb);
      return;
    case SerialCliVerb::WEBUI_STOP:
      serialCliQueueNetworkAction(WebCommandType::WEBUI_STOP, request.verb);
      return;
    case SerialCliVerb::WEBUI_RESTART:
      serialCliQueueNetworkAction(WebCommandType::WEBUI_RESTART, request.verb);
      return;
    case SerialCliVerb::WIFI_STATUS:
    case SerialCliVerb::AP_STATUS:
    case SerialCliVerb::WEBUI_STATUS:
    case SerialCliVerb::NET_STATUS:
      serialCliPrintLiveNetworkStatus(request.verb);
      return;
    case SerialCliVerb::LOG_DUMP:
      serialCliPrintLiveLogDump();
      return;
    case SerialCliVerb::HEALTH:
      serialCliPrintLiveHealth();
      return;
    case SerialCliVerb::SCALE_STATUS:
      serialCliPrintLiveScaleStatus();
      return;
    case SerialCliVerb::NTP_STATUS:
      serialCliPrintLiveNtpStatus();
      return;
    case SerialCliVerb::BLE_COMPAT_ENABLE:
    case SerialCliVerb::BLE_COMPAT_DISABLE: {
      WebCommand command;
      command.type = request.verb == SerialCliVerb::BLE_COMPAT_ENABLE
                         ? WebCommandType::BLE_COMPAT_ENABLE
                         : WebCommandType::BLE_COMPAT_DISABLE;
      serialCliQueueCommand(command, request.verb);
      return;
    }
    case SerialCliVerb::BLE_COMPAT_STATUS:
      serialCliPrintBleCompanionStatus();
      return;
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

#ifndef SHOT_STOPPER_HOST_TEST
void serviceBootRecoverySafety() {
  serviceRelaySafety();
  localBuzzer.service(millis());
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
  }
  serviceSafetyHeartbeat(false);
}

void waitForRecoveryBuzzer() {
  while (localBuzzer.busy()) {
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void completeBootRecovery(RecoveryOperation operation);

void abandonFailedBootRecovery(RecoveryOperation operation) {
  (void)playRecoveryCue(BuzzerCue::RECOVERY_ERROR);
  waitForRecoveryBuzzer();
  if (operation == RecoveryOperation::FACTORY_RESET) {
    (void)resetPersistedSettingsToFactory(persistedSettings);
  } else if (operation == RecoveryOperation::NETWORK_ACCESS_RESET) {
    (void)resetPersistedNetworkAccess(persistedSettings);
  }
  (void)abandonRecoveryIntent();
}

void abandonMalformedRecoveryIntent() {
  (void)playRecoveryCue(BuzzerCue::RECOVERY_ERROR);
  waitForRecoveryBuzzer();
  (void)resetPersistedSettingsToFactory(persistedSettings);
  (void)abandonRecoveryIntent();
}

bool applyBootRecoveryOperation(RecoveryOperation operation) {
  if (operation == RecoveryOperation::NETWORK_ACCESS_RESET) {
    return resetPersistedNetworkAccess(persistedSettings);
  }
  if (operation != RecoveryOperation::FACTORY_RESET) {
    return false;
  }

  return resetAllDurableStoresForNetwork(persistedSettings);
}

void completeBootRecovery(RecoveryOperation operation) {
  // Do not rewrite a valid latch: a failed put on full NVS can tear it.
  (void)ensureRecoveryIntent(operation);
  if (!applyBootRecoveryOperation(operation)) {
    abandonFailedBootRecovery(operation);
    return;
  }
  (void)clearRecoveryIntent();

  const BuzzerCue success =
      operation == RecoveryOperation::FACTORY_RESET
          ? BuzzerCue::FACTORY_RESET_OK
          : BuzzerCue::NETWORK_RESET_OK;
  (void)playRecoveryCue(success);
  waitForRecoveryBuzzer();
  // Preserve the final 50 ms pause specified by the recovery cue before the
  // reset cuts power to the buzzer output.
  const uint32_t pauseStartedAtMs = millis();
  while (static_cast<uint32_t>(millis() - pauseStartedAtMs) <
         BUZZER_RECOVERY_PULSE_GAP_MS) {
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  // If the latch could not be dropped, continue this boot instead of
  // restarting into the same pending operation forever.
  if (!bootRecoveryShouldRestartAfterSuccess()) {
    return;
  }
  recordResetUptime(millis(), true);
  ESP.restart();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void resumePendingBootRecovery() {
  RecoveryIntent intent;
  const PendingRecoveryKind kind = inspectPendingRecovery(intent);
  if (kind == PendingRecoveryKind::VALID) {
    completeBootRecovery(static_cast<RecoveryOperation>(intent.operation));
    return;
  }
  if (kind == PendingRecoveryKind::MALFORMED) {
    abandonMalformedRecoveryIntent();
  }
}

void maybeRunBootRecoveryGesture() {
  const bool powerOnReset = currentSafetyResetIsPowerOn();
  const bool switchStablyOn = powerOnReset && machineBootActivatorHeldStably();
  if (!recoveryGestureEntryAllowed(powerOnReset, switchStablyOn)) {
    return;
  }

  RecoveryGestureRecognizer recognizer;
  recognizer.begin(millis());
  (void)playRecoveryCue(BuzzerCue::RECOVERY_START);

  for (;;) {
    machineSampleInput();
    const MachineIntention intent = machinePollIntention();
    const RecoveryGestureResult result = recognizer.update(
        millis(), intent.holdActive, intent.turnedOn, intent.turnedOff);
    if (result == RecoveryGestureResult::NETWORK_ACCESS_RESET) {
      completeBootRecovery(RecoveryOperation::NETWORK_ACCESS_RESET);
      return;
    }
    if (result == RecoveryGestureResult::FACTORY_RESET) {
      completeBootRecovery(RecoveryOperation::FACTORY_RESET);
      return;
    }
    if (result == RecoveryGestureResult::TIMED_OUT) {
      (void)playRecoveryCue(BuzzerCue::RECOVERY_START);
      waitForRecoveryBuzzer();
      return;
    }
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
#endif

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

bool usbConsoleJumperPresent() {
  pinMode(USB_CONSOLE_GPIO, INPUT_PULLUP);
  return digitalRead(USB_CONSOLE_GPIO) == USB_CONSOLE_ACTIVE_LEVEL;
}

void setup() {
  bootStartedAtMs = millis();
  // Safe OPEN before Serial, EEPROM or BLE. Arduino-ESP32 3.x rejects
  // digitalWrite until the pad is a GPIO. After reset the pin is Hi-Z and
  // the output latch is 0, which is OPEN for the active-HIGH relay.
  machineInitialize();
  initializeScaleConnectedLed();
#ifndef SHOT_STOPPER_HOST_TEST
  set_arduino_panic_handler(shotStopperPanicHandler, nullptr);
#endif
  safetyResetStatus = beginSafetyResetGuard();

  localBuzzer.begin(BUZZER_GPIO);

  platformClockReady = setCpuFrequencyMhz(80);
  relaySafetyTimersReady = initializeRelaySafetyTimer();
  taskWatchdogReady =
      configureTaskWatchdog() && subscribeCurrentTaskToWatchdog();
  initializeRelaySafetyStateAfterBoot();

  if (EXTERNAL_SAFETY_HARDWARE_PRESENT && readCircuitFeedbackClosed()) {
    tripRelaySafety(RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
  }

#ifndef SHOT_STOPPER_HOST_TEST
  // A pending destructive operation always resumes before normal storage,
  // BLE, Wi-Fi or HTTP startup. A new gesture is accepted only on cold power.
  resumePendingBootRecovery();
  maybeRunBootRecoveryGesture();
  machineOnActivatorReady();
#endif

  // Default builds keep USB Serial/JTAG off until GPIO4 is jumpered to GND.
  // Serial is HWCDC (native USB), not UART0 — see ShotStopperUsbConsole.h.
  // -DSHOT_STOPPER_ENABLE_JTAG=1 starts CDC at boot (OpenOCD + CLI, no jumper).
  // Latch the enable source for Diagnostic; live IO4 is re-read each status.
  if (SHOT_STOPPER_ENABLE_JTAG == 1) {
    usbSerialEnableSource = UsbSerialEnableSource::COMPILE_FLAG;
    Serial.begin(SERIAL_BAUD);
  } else if (usbConsoleJumperPresent()) {
    usbSerialEnableSource = UsbSerialEnableSource::JUMPER;
    Serial.begin(SERIAL_BAUD);
  }

  persistenceReady = EEPROM.begin(EEPROM_SIZE);
#ifndef SHOT_STOPPER_HOST_TEST
  if (!ensureFlashIoMutex()) {
    addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_PERSISTENCE);
  }
  // RTC captures the prior uptime immediately across warm resets; NVS keeps
  // the history when RTC memory is lost (for example after power loss).
  persistResetHistoryAfterBoot(safetyResetStatus);
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
  if (!loadBleCompanionSettings(bleCompanionPersistedSettings)) {
    bleCompanionPersistedSettings = BleCompanionPersistedSettings{};
    if (!saveBleCompanionSettings(bleCompanionPersistedSettings)) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                    BOOT_SUBSYSTEM_SETTINGS_SAVE);
    }
  }
  const bool bleCompanionConfigured =
      bleCompanionPersistedSettings.enabled != 0;
  applyLiveBleScanIntensity(clampBleScanIntensity(
      bleCompanionPersistedSettings.scanIntensity));
  bleCompanionStatusSnapshot.configuredEnabled = bleCompanionConfigured;
  bleCompanionRuntimeSnapshot.configuredEnabled = bleCompanionConfigured;
  if (bleCompanionConfigured) {
    // Host GATT profile (object + characteristic value buffers via ArduinoBLE
    // BLEHostAlloc) prefers PSRAM; fall back to internal if SPIRAM is full.
    static_assert(sizeof(ShotStopperBleCompanion) >= 64u &&
                      sizeof(ShotStopperBleCompanion) <= 8192u,
                  "ShotStopperBleCompanion size outside expected host budget");
    void *storage = allocExternalOrInternal(sizeof(ShotStopperBleCompanion));
    if (storage != nullptr) {
      bleCompanion = new (storage) ShotStopperBleCompanion();
      bleCompanionRuntimeSnapshot.enabled = true;
    } else {
      bleCompanionStatusSnapshot.lastReject =
          BleCompanionRejectReason::ALLOCATION_FAILED;
      addDebugEvent(DebugCategory::SCALE, DebugCode::INITIALIZATION_FAILED,
                    BOOT_SUBSYSTEM_BLE);
    }
  }
  bleCompanionStatusSnapshot.restartRequired =
      bleCompanionConfigured != bleCompanionRuntimeSnapshot.enabled;
  if (settingsLoaded) {
    runtimeConfig = persistedSettings.runtime;
    presetBank = persistedSettings.presets;
    ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                         runtimeConfig.autoRetare);
    scaleWorkerLoadPreferred(persistedSettings.preferredScaleMac,
                             persistedSettings.preferredScaleName,
                             persistedSettings.scaleHistory);
  }
#else
  runtimeConfig = RuntimeConfig{};
  seedDefaultShotPresetBank(presetBank);
#endif
  serialLogLevel = serialLogLevelFromRuntime(runtimeConfig);
  ringRetainLogLevel =
      static_cast<LogLevel>(runtimeConfig.ringRetainLogLevel);
  publishRecipeState();

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
  if (safetyResetStatus.recoveryRequired) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::SAFETY_LOCKOUT_ACTIVE);
  }

#ifndef SHOT_STOPPER_HOST_TEST
  shotLog.load();
  shotLog.onBoot();
  shotLog.save();
  shotCurves.load();
  lastShotStore.load();
  persistedLastShot = lastShotStore.get();
  if (persistedLastShot.valid) {
    ShotCurveRecord newest = {};
    if (shotCurves.copyNewestFirst(&newest, 1) == 1) {
      lastShotCurve = newest;
    }
  }
#else
  shotLog.load();
  shotLog.onBoot();
  shotCurves.load();
  lastShotStore.load();
  persistedLastShot = lastShotStore.get();
  if (persistedLastShot.valid) {
    ShotCurveRecord newest = {};
    if (shotCurves.copyNewestFirst(&newest, 1) == 1) {
      lastShotCurve = newest;
    }
  }
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

  const bool scaleWorkerOk = initializeScaleWorker();
  if (!scaleWorkerOk) {
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SCALE_WORKER, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SCALE_WORKER, 1);
  }

  webCommandQueue =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  const bool webQueueOk = webCommandQueue != nullptr;
  if (!webQueueOk) {
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
          settingsPersistTask, "settings_persist",
          SETTINGS_PERSIST_TASK_STACK_SIZE, nullptr,
          tskIDLE_PRIORITY, &settingsPersistTaskHandle,
          CONTROL_TASK_CORE) != pdPASS) {
    settingsPersistTaskHandle = nullptr;
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SETTINGS_SAVE, 0);
  }
#endif

  hwmon.begin();
  hwmonSnapshot = hwmon.sample(1);
  publishControlStatus();
  bool networkOk = true;
#ifndef SHOT_STOPPER_HOST_TEST
  // Runs before the network task so the OTA slot pair and this boot's
  // pending-verify state are known the first time the Web UI is served.
  shotstopper::ShotStopperOta::instance().begin();
  if (settingsLoaded && webCommandQueue != nullptr) {
    NetworkBridgeCallbacks callbacks;
    callbacks.copyControlStatus = copyControlStatus;
    callbacks.copyControlGate = copyControlGate;
    callbacks.refreshControlStatus = refreshControlStatus;
    callbacks.enqueueWebCommand = enqueueWebCommand;
    callbacks.copyDebugEvents = copyDebugEvents;
    callbacks.addDebugEvent = addDebugEvent;
    callbacks.reportTaskWatchdogFault = reportTaskWatchdogFault;
    callbacks.requestSafeRestart = requestSafeRestart;
    callbacks.copyShotRecords = copyShotRecords;
    callbacks.copyShotCurves = copyShotCurves;
    callbacks.deleteShotRecord = deleteShotRecord;
    callbacks.rateShotRecord = rateShotRecord;
    callbacks.rateLastShot = rateLastShot;
    callbacks.clearShotLog = clearShotLog;
    callbacks.clearLastShot = clearLastShot;
    callbacks.resetAllDurableStores = resetAllDurableStoresForNetwork;
    callbacks.copyPreferredScaleMac = copyPreferredScaleMac;
    callbacks.copyPreferredScaleName = copyPreferredScaleName;
    callbacks.copyScaleHistory = copyScaleHistory;
    callbacks.copyPresetBank = copyPresetBank;
    callbacks.copyRuntimeConfig = copyRuntimeConfig;
    callbacks.copyDebugExportExtras = copyDebugExportExtras;
    callbacks.copyTaskProfiler = copyTaskProfiler;
    if (!networkManager.begin(persistedSettings, callbacks)) {
      networkOk = false;
      logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
              BOOT_SUBSYSTEM_NETWORK, 0);
    } else {
      logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
              BOOT_SUBSYSTEM_NETWORK, 1);
    }
  } else if (!settingsLoaded) {
    networkOk = false;
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_NETWORK, 0);
  }
  const bool psramOk =
      psramFound() && (!settingsLoaded || workBufIsExternal());
  logEmit(psramOk ? LogLevel::INFO : LogLevel::CRITICAL, DebugCategory::BOOT,
          DebugCode::BOOT_SUBSYSTEM, BOOT_SUBSYSTEM_PSRAM, psramOk ? 1 : 0);
#endif

  bootDegraded = !persistenceReady || !scaleWorkerOk || !webQueueOk ||
                 !networkOk;
  firmwareInitializationComplete = !bootDegraded;
  if (firmwareInitializationComplete) {
    addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_READY);
  } else {
    addDebugEvent(DebugCategory::BOOT, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_SCALE_WORKER, bootDegraded ? 1 : 0);
  }
  publishControlStatus();
  serviceScaleConnectedLed();
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
  if (heapLow) {
    if (!healthHeapAlertLatched) {
      healthHeapAlertLatched = true;
      healthHeapLowSinceMs = millis();
      addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_HEAP_LOW,
                    static_cast<int32_t>(freeHeapBytes),
                    static_cast<int32_t>(largestFreeHeapBlockBytes));
    }
    if (!healthHeapRestartLatched && healthHeapLowSinceMs != 0 &&
        elapsedMs(healthHeapLowSinceMs) >= HEALTH_HEAP_LOW_RESTART_MS &&
        controlAllowsConfigurationNow() && !circuitClosed) {
      healthHeapRestartLatched = true;
      addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_HEAP_RESTART,
                    static_cast<int32_t>(freeHeapBytes),
                    static_cast<int32_t>(largestFreeHeapBlockBytes));
      safeRestartRequested = true;
    }
  } else if (heapClear) {
    healthHeapAlertLatched = false;
    healthHeapRestartLatched = false;
    healthHeapLowSinceMs = 0;
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
  recordResetUptime(loopStartedAtMs);
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
  // Relay and paddle control never wait for BLE. The worker owns every scale,
  // heartbeat, packet, timer and connection operation. Restart before heap
  // walks: a failed Wi-Fi stop can leave TLSF unwalkable.
  serviceRelaySafety();
  if (taskWatchdogRestoreFailed) {
    taskWatchdogRestoreFailed = false;
    reportTaskWatchdogFault();
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
    safeRestartRequested = true;
  }
  if (safeRestartRequested) {
    const bool faultRestart =
        criticalTaskWatchdogFault || taskWatchdogRestoreFailed;
    if (faultRestart ||
        (!session.active && !getRelaySafetySnapshot().closed)) {
      machineRequestStop();
      serviceSafetyHeartbeat(false);
#ifndef SHOT_STOPPER_HOST_TEST
      recordResetUptime(millis(), true);
      ESP.restart();
#else
      safeRestartRequested = false;
#endif
      return;
    }
    // Planned restart waits for the shot. Fault paths above still open K1
    // and reset immediately so a hung firmware cannot keep the group on.
  }
  if (elapsedMs(healthTelemetryAtMs) >= HEALTH_TELEMETRY_INTERVAL_MS) {
    const uint32_t intervalMs = elapsedMs(healthTelemetryAtMs);
    healthTelemetryAtMs = loopStartedAtMs;
    loopStackMinWords =
        static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
#ifndef SHOT_STOPPER_HOST_TEST
    const HeapCapSnapshot heap = sampleHeapCaps();
    freeHeapBytes = heap.internalFree;
    minimumFreeHeapBytes = heap.internalMinimum;
    largestFreeHeapBlockBytes = heap.internalLargest;
    psramSizeBytes = heap.psramTotal;
    psramFreeBytes = heap.psramFree;
    psramLargestFreeBlockBytes = heap.psramLargest;
    bleHostAllocPsramCount = BLEHostAllocPsramCount();
    bleHostAllocFallbackCount = BLEHostAllocFallbackCount();
    bleHostHciRxDropped = BLEHostHciRxDropped();
    bleHostHciTxDropped = BLEHostHciTxDropped();
    hwmonSnapshot = hwmon.sample(intervalMs > 0U ? intervalMs
                                                 : HEALTH_TELEMETRY_INTERVAL_MS,
                                 &heap);
#else
    hwmonSnapshot = hwmon.sample(intervalMs > 0U ? intervalMs
                                                 : HEALTH_TELEMETRY_INTERVAL_MS);
#endif
    serviceHealthThresholdAlerts(healthIntervalMaxGapMs);
    loopIntervalGapMs = healthIntervalMaxGapMs;
    healthIntervalMaxGapMs = 0;
  }
  pushActivatorDrivePermission();
  machineSampleInput();
  processScaleWorkerEvents();
  observeMachineSenseFromSession();
  serviceMachine();
  captureLoopGuards();
  serviceNoScaleShotGuard(loopGuardInputs);
  serviceCupStartGuard(loopGuardInputs);
  stateMachineTask();
  machineOnBrewOutcome(session.active);
  serviceExtendedPulseAlert();
  localBuzzer.service(millis());
  servicePendingBrewRfRestore();
  serviceScaleCompletionBeep();
  servicePendingScaleTimerStop();
  serviceRemoteTimerStopRetry();
  pendingShotFinalizeTask();
  serviceSerialCli();
  serviceMaintenanceCancellation();
  processBleCompanionRequests();
  processWebCommands();
  taskProfiler.service(millis());
  serviceControlCommandResult();
  serviceRuntimePersistence();
  serviceShotStorePersistence();
  servicePreferredScaleMacPersistence();
  serviceMaintenanceLease();
  servicePendingPlannedRestart();
  serviceControlStatusPublish();
  publishBleCompanionRuntimeSnapshot();
  serviceScaleConnectedLed();
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
    safeRestartRequested = true;
    serviceSafetyHeartbeat(false);
    return;
  }
  serviceSafetyHeartbeat(true);
  processScaleWorkerEvents();
  vTaskDelay(pdMS_TO_TICKS(controlLoopTickDelayMs()));
}
