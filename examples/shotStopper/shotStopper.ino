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
#include <esp_timer.h>
#include <math.h>
#include "ShotStopperNetwork.h"
#include "ShotStopperPersistence.h"
#endif

#include "ShotStopperDomain.h"

using namespace shotstopper;

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

constexpr uint32_t PADDLE_DEBOUNCE_MS = 30;
constexpr uint32_t DRIP_DELAY_MS = 3000;
constexpr uint32_t SCALE_CONNECT_RETRY_MS = 1000;
constexpr uint32_t SCALE_CONNECT_LOG_MS = 10000;
constexpr uint32_t SCALE_WORKER_STALE_MS = 2000;
constexpr uint32_t PADDLE_RETURN_REMINDER_BEEP_INTERVAL_MS = 15000;
constexpr size_t SCALE_COMMAND_QUEUE_LENGTH = 12;
constexpr size_t SCALE_EVENT_QUEUE_LENGTH = 64;

constexpr bool DEBUG = false;

// ---------------------------------------------------------------------------
// Board hardware
// ---------------------------------------------------------------------------

#if defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t LED_RED_PIN = 46;
constexpr uint8_t LED_BLUE_PIN = 45;
constexpr uint8_t LED_GREEN_PIN = 47;
constexpr uint8_t PADDLE_GPIO = 21;
constexpr uint8_t RELAY_GPIO = 38;
#elif defined(ARDUINO_ESP32C3_DEV)
constexpr uint8_t LED_RED_PIN = 21;
constexpr uint8_t LED_BLUE_PIN = 10;
constexpr uint8_t LED_GREEN_PIN = 20;
constexpr uint8_t PADDLE_GPIO = 8;
constexpr uint8_t RELAY_GPIO = 6;
#elif defined(ARDUINO_NANO_ESP32)
// Arduino Nano ESP32. RGB LED pins are framework-defined.
constexpr uint8_t LED_RED_PIN = LED_RED;
constexpr uint8_t LED_BLUE_PIN = LED_BLUE;
constexpr uint8_t LED_GREEN_PIN = LED_GREEN;
constexpr uint8_t PADDLE_GPIO = 10;
constexpr uint8_t RELAY_GPIO = 11;
#elif defined(ARDUINO_ESP32_DEV)
// ESP32 DevKit v4 / ESP32 Dev Module. An external RGB LED is optional.
// GPIO 27 supports INPUT_PULLUP for the paddle; GPIO 26 drives the relay.
constexpr uint8_t LED_RED_PIN = 25;
constexpr uint8_t LED_BLUE_PIN = 32;
constexpr uint8_t LED_GREEN_PIN = 33;
constexpr uint8_t PADDLE_GPIO = 27;
constexpr uint8_t RELAY_GPIO = 26;
#else
#error "Unsupported board: configure explicit GPIO and LED pins"
#endif

constexpr uint8_t PADDLE_ACTIVE_LEVEL = LOW;
constexpr uint8_t RELAY_CLOSED_LEVEL = HIGH;
constexpr uint8_t RELAY_OPEN_LEVEL = LOW;

static_assert(PADDLE_GPIO != RELAY_GPIO,
              "Paddle and relay must use different GPIOs");
static_assert(PADDLE_ACTIVE_LEVEL == LOW,
              "Micra paddle wiring requires INPUT_PULLUP and active LOW");
static_assert(RELAY_CLOSED_LEVEL != RELAY_OPEN_LEVEL,
              "Relay open and closed levels must differ");
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
  TimerStopResult timerStopResult = TimerStopResult::NOT_REQUIRED;
  uint32_t id = 0;
  uint32_t scaleDisconnectSequenceAtStart = 0;
  uint32_t weightSequenceAtStart = 0;
  uint32_t startedAtMs = 0;
  uint32_t rinseStartedAtMs = 0;
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
  float weightG = 0.0f;
  bool commandAttempted = false;
  bool writeSucceeded = false;
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
uint32_t nextCycleId = 1;
TaskHandle_t scaleWorkerTaskHandle = nullptr;
QueueHandle_t scaleCommandQueue = nullptr;
QueueHandle_t scaleEventQueue = nullptr;
QueueHandle_t webCommandQueue = nullptr;
portMUX_TYPE scaleLinkMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE scaleBeepMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE webStatusMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE debugLogMux = portMUX_INITIALIZER_UNLOCKED;
ScaleLinkState scaleLinkState = ScaleLinkState::DISCONNECTED;
uint32_t scaleDisconnectSequence = 0;
uint32_t scaleWorkerProgressAtMs = 0;
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

#ifndef SHOT_STOPPER_HOST_TEST
PersistedSettings persistedSettings;
ShotStopperNetwork networkManager;
#endif

// The esp_timer callback independently opens CN9 at the hard limit even if the
// normal control loop is delayed or unavailable.
esp_timer_handle_t relaySafetyTimer = nullptr;
esp_timer_handle_t operationalLimitTimer = nullptr;
portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;
bool cn9Closed = false;
bool relaySafetyTripped = false;
bool operationalLimitTripped = false;
uint32_t cn9ClosedAtMs = 0;
uint32_t operationalLimitAtArmMs = HARD_MAX_CN9_CLOSED_MS;

struct RelaySafetySnapshot {
  bool closed;
  bool tripped;
  bool operationalTripped;
  uint32_t closedAtMs;
  uint32_t operationalLimitMs;
};

struct ScaleLinkSnapshot {
  ScaleLinkState state;
  uint32_t disconnectSequence;
  uint32_t workerProgressAtMs;
};

const uint8_t COLOR_RED[3] = {255, 0, 0};
const uint8_t COLOR_GREEN[3] = {0, 255, 0};
const uint8_t COLOR_BLUE[3] = {0, 0, 255};
const uint8_t COLOR_MAGENTA[3] = {255, 0, 255};
const uint8_t COLOR_CYAN[3] = {0, 255, 255};
const uint8_t COLOR_YELLOW[3] = {255, 255, 0};
const uint8_t COLOR_OFF[3] = {0, 0, 0};
uint8_t currentColor[3] = {255, 255, 255};

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

bool enqueueWebCommand(const WebCommand &command) {
  return webCommandQueue != nullptr &&
         xQueueSend(webCommandQueue, &command, 0) == pdTRUE;
}

ScaleLinkSnapshot getScaleLinkSnapshot() {
  ScaleLinkSnapshot snapshot;
  portENTER_CRITICAL(&scaleLinkMux);
  snapshot.state = scaleLinkState;
  snapshot.disconnectSequence = scaleDisconnectSequence;
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

bool scaleAutomationUnavailableForSession() {
  const ScaleLinkSnapshot snapshot = getScaleLinkSnapshot();
  return !scaleLinkAvailable(snapshot) ||
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

void setColor(const uint8_t rgb[3]) {
  if (currentColor[0] == rgb[0] && currentColor[1] == rgb[1] &&
      currentColor[2] == rgb[2]) {
    return;
  }

  analogWrite(LED_RED_PIN, 255 - rgb[0]);
  analogWrite(LED_GREEN_PIN, 255 - rgb[1]);
  analogWrite(LED_BLUE_PIN, 255 - rgb[2]);
  currentColor[0] = rgb[0];
  currentColor[1] = rgb[1];
  currentColor[2] = rgb[2];
}

// ---------------------------------------------------------------------------
// Relay and independent hard limit
// ---------------------------------------------------------------------------

void relaySafetyTimerCallback(void *) {
  const uint32_t callbackAtMs = millis();
  portENTER_CRITICAL(&relayMux);
  // Ignore a callback left queued by a previous cycle. This also makes a
  // rapid re-arm safe if an old esp_timer callback was already dispatched.
  if (cn9Closed &&
      static_cast<uint32_t>(callbackAtMs - cn9ClosedAtMs) >=
          HARD_MAX_CN9_CLOSED_MS) {
    digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
    cn9Closed = false;
    relaySafetyTripped = true;
  }
  portEXIT_CRITICAL(&relayMux);
}

void operationalLimitTimerCallback(void *) {
  const uint32_t callbackAtMs = millis();
  portENTER_CRITICAL(&relayMux);
  if (cn9Closed &&
      static_cast<uint32_t>(callbackAtMs - cn9ClosedAtMs) >=
          operationalLimitAtArmMs) {
    digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
    cn9Closed = false;
    operationalLimitTripped = true;
  }
  portEXIT_CRITICAL(&relayMux);
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
  return esp_timer_create(&operationalArgs, &operationalLimitTimer) == ESP_OK;
}

RelaySafetySnapshot getRelaySafetySnapshot() {
  RelaySafetySnapshot snapshot;
  portENTER_CRITICAL(&relayMux);
  snapshot.closed = cn9Closed;
  snapshot.tripped = relaySafetyTripped;
  snapshot.operationalTripped = operationalLimitTripped;
  snapshot.closedAtMs = cn9ClosedAtMs;
  snapshot.operationalLimitMs = operationalLimitAtArmMs;
  portEXIT_CRITICAL(&relayMux);
  return snapshot;
}

bool setCn9Closed(bool closed,
                  uint32_t operationalLimitMs = HARD_MAX_CN9_CLOSED_MS) {
  if (closed) {
    if (getRelaySafetySnapshot().closed) {
      return true;
    }

    if (relaySafetyTimer == nullptr || operationalLimitTimer == nullptr ||
        operationalLimitMs < 1 ||
        operationalLimitMs > HARD_MAX_CN9_CLOSED_MS) {
      Serial.println("Cannot close CN9: safety timers/config unavailable");
      return false;
    }

    const uint32_t closingAtMs = millis();
    esp_timer_stop(relaySafetyTimer);
    esp_timer_stop(operationalLimitTimer);
    if (esp_timer_start_once(
            relaySafetyTimer,
            static_cast<uint64_t>(HARD_MAX_CN9_CLOSED_MS) * 1000ULL) !=
        ESP_OK) {
      Serial.println("Cannot close CN9: failed to arm hard-limit timer");
      return false;
    }
    if (operationalLimitMs < HARD_MAX_CN9_CLOSED_MS &&
        esp_timer_start_once(
            operationalLimitTimer,
            static_cast<uint64_t>(operationalLimitMs) * 1000ULL) != ESP_OK) {
      esp_timer_stop(relaySafetyTimer);
      Serial.println("Cannot close CN9: failed to arm operational timer");
      return false;
    }

    portENTER_CRITICAL(&relayMux);
    cn9ClosedAtMs = closingAtMs;
    operationalLimitAtArmMs = operationalLimitMs;
    relaySafetyTripped = false;
    operationalLimitTripped = false;
    digitalWrite(RELAY_GPIO, RELAY_CLOSED_LEVEL);
    cn9Closed = true;
    portEXIT_CRITICAL(&relayMux);
    Serial.println("CN9 closed");
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_CLOSED,
                  static_cast<int32_t>(operationalLimitMs));
    return true;
  }

  if (relaySafetyTimer != nullptr) {
    esp_timer_stop(relaySafetyTimer);
  }
  if (operationalLimitTimer != nullptr) {
    esp_timer_stop(operationalLimitTimer);
  }
  if (!getRelaySafetySnapshot().closed) {
    return true;
  }

  portENTER_CRITICAL(&relayMux);
  const bool mustOpen = cn9Closed;
  if (mustOpen) {
    digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
    cn9Closed = false;
  }
  portEXIT_CRITICAL(&relayMux);
  if (mustOpen) {
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

void requestRemoteTimerStop() {
  if (!session.timerStartCommandQueued || session.stopTimerRequested) {
    return;
  }

  session.stopTimerRequested = true;
  if (!scaleAvailable()) {
    session.timerStopResult = TimerStopResult::NOT_ATTEMPTED;
    Serial.print("Remote timer stop not attempted for cycle ");
    Serial.print(session.id);
    Serial.println(": scale disconnected");
    return;
  }

  ScaleCommand command;
  command.type = ScaleCommandType::STOP_TIMER;
  command.cycleId = session.id;
  if (!enqueueScaleCommand(command)) {
    session.timerStopResult = TimerStopResult::NOT_ATTEMPTED;
    Serial.print("Remote timer stop not attempted for cycle ");
    Serial.print(session.id);
    Serial.println(": command queue unavailable");
  } else {
    session.timerStopResult = TimerStopResult::PENDING;
    Serial.print("Remote timer stop queued for cycle ");
    Serial.println(session.id);
  }
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
  return session.active && session.automaticEnabled &&
         (stopperState == StopperState::QUALIFYING_ON ||
          stopperState == StopperState::BREW);
}

void recordWeightSample(float weight, uint32_t receivedAtMs) {
  if (!shouldTrackWeight()) {
    return;
  }

  if (!isfinite(weight)) {
    Serial.println("Invalid non-finite weight ignored");
    return;
  }

  if (static_cast<int32_t>(receivedAtMs - shot.startMs) < 0) {
    return;
  }

  if (shot.datapoints >= MAX_SHOT_DATAPOINTS) {
    Serial.println("Shot trajectory full; ignoring additional samples");
    return;
  }

  const size_t index = shot.datapoints++;
  shot.timeS[index] =
      static_cast<uint32_t>(receivedAtMs - shot.startMs) / 1000.0f;
  shot.weight[index] = weight;
  calculateExpectedEndTime();

  // Per-sample serial output can fill the UART buffer and delay the next
  // control iteration. Keep it behind the compile-time diagnostic flag; the
  // Web ring deliberately never records individual weights.
  if (DEBUG) {
    Serial.print(weight);
    Serial.print("g, t=");
    Serial.print(shot.timeS[index]);
    Serial.print("s, expected end=");
    Serial.print(shot.expectedEndS);
    Serial.println("s");
  }
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

  runtimeConfig.weightOffsetG = updatedOffset;
  ++runtimeConfig.revision;
  if (runtimeConfig.revision == 0) {
    runtimeConfig.revision = 1;
  }
  WebCommand persist;
  persist.type = WebCommandType::PERSIST_RUNTIME;
  persist.config = runtimeConfig;
#ifndef SHOT_STOPPER_HOST_TEST
  if (!networkManager.enqueueAcceptedCommand(persist)) {
    Serial.println("Offset persistence deferred queue unavailable");
  }
#endif
  Serial.print("New offset: ");
  Serial.println(runtimeConfig.weightOffsetG);
}

// ---------------------------------------------------------------------------
// Scale connection and weight packets
// ---------------------------------------------------------------------------

void publishScaleEvent(const ScaleEvent &event, bool critical) {
  if (scaleEventQueue == nullptr) {
    return;
  }
  xQueueSend(scaleEventQueue, &event, critical ? portMAX_DELAY : 0);
}

void updateWorkerLinkState() {
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
    setScaleLinkState(ScaleLinkState::DISCONNECTED);
    return;
  }

  if (scale.heartbeatRequired()) {
    if (!scale.heartbeat()) {
      setScaleLinkState(ScaleLinkState::DISCONNECTED);
      return;
    }
  }

  const bool weightAvailable = scale.newWeightAvailable();
  if (!scale.isConnected()) {
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
}

void scaleWorkerTask(void *) {
  uint32_t lastConnectAttemptMs = 0;
  uint32_t lastConnectLogMs = 0;
  bool connectAttemptSeriesActive = false;

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
        setScaleLinkState(connected ? ScaleLinkState::CONNECTED
                                    : ScaleLinkState::DISCONNECTED);
      }
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
                  tskIDLE_PRIORITY + 2, &scaleWorkerTaskHandle) != pdPASS) {
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
  if (scaleEventQueue == nullptr) {
    return;
  }

  ScaleEvent event;
  while (xQueueReceive(scaleEventQueue, &event, 0) == pdTRUE) {
    switch (event.type) {
      case ScaleEventType::WEIGHT:
        if (!isfinite(event.weightG)) {
          Serial.println("Invalid scale weight event ignored");
          break;
        }
        currentWeight = event.weightG;
        currentWeightReceivedAtMs = event.receivedAtMs;
        ++currentWeightSequence;
        recordWeightSample(currentWeight, currentWeightReceivedAtMs);
        break;

      case ScaleEventType::TIMER_START_RESULT:
        if (event.cycleId == session.id) {
          session.remoteTimerMayBeRunning = event.commandAttempted;
          session.remoteTimerStarted = event.writeSucceeded;
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

void resetSessionForNewCycle(ControlSource source) {
  session = CycleSession{};
  session.id = nextCycleId++;
  if (nextCycleId == 0) {
    nextCycleId = 1;
  }
  session.active = true;
  session.source = source;
  session.config = snapshotConfig(runtimeConfig);
  session.endReason = EndReason::NONE;
}

void beginCycle(ControlSource source = ControlSource::PHYSICAL) {
  if (pendingAnalysis.pending) {
    pendingAnalysis.pending = false;
    Serial.println("Previous drip analysis cancelled by a new cycle");
  }

  resetSessionForNewCycle(source);
  session.startedAtMs = millis();
  session.weightSequenceAtStart = currentWeightSequence;
  const ScaleLinkSnapshot scaleLinkAtStart = getScaleLinkSnapshot();
  session.startedWithScale = scaleLinkAvailable(scaleLinkAtStart);
  session.scaleDisconnectSequenceAtStart =
      scaleLinkAtStart.disconnectSequence;
  session.automaticEnabled = session.startedWithScale;
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
  const bool analyze = shot.confirmedBrew && !session.config.timerOnly;

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
  if (session.automaticEnabled &&
      !scaleAutomationUnavailableForSession()) {
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
  if (session.config.timerOnly || stopperState != StopperState::BREW ||
      !session.automaticEnabled || scaleAutomationUnavailableForSession()) {
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

      // A BLE loss marks the cycle manual but does not skip gesture
      // classification; a quick release must still become a rinse.
      if (session.automaticEnabled &&
          scaleAutomationUnavailableForSession()) {
        session.automaticEnabled = false;
        session.scaleWasLost = true;
        Serial.println("Scale lost while qualifying; cycle marked manual");
      }

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

      if (scaleAutomationUnavailableForSession()) {
        session.automaticEnabled = false;
        session.scaleWasLost = true;
        Serial.println("Scale lost during brew; continuing as manual cycle");
        transitionTo(StopperState::MANUAL_NO_SCALE);
        return;
      }

      if (automaticScaleStopDue()) {
        finalizeCycle(EndReason::SCALE_PREDICTION,
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
         !getRelaySafetySnapshot().closed && !paddleOn && !rawPaddleOn;
}

void beginWebRinse() {
  if (pendingAnalysis.pending) {
    pendingAnalysis.pending = false;
    Serial.println("Previous drip analysis cancelled by a web rinse");
  }
  resetSessionForNewCycle(ControlSource::WEB);
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

void forwardAcceptedNetworkCommand(const WebCommand &command) {
#ifndef SHOT_STOPPER_HOST_TEST
  if (!networkManager.enqueueAcceptedCommand(command)) {
    addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                  static_cast<int32_t>(command.type));
  }
#else
  (void)command;
#endif
}

void rejectWebCommand(const WebCommand &command) {
  addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                static_cast<int32_t>(command.type));
}

void processWebCommand(const WebCommand &command) {
  switch (command.type) {
    case WebCommandType::STOP:
    case WebCommandType::STOP_HEARTBEAT:
    case WebCommandType::PADDLE_OFF:
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
      return;

    case WebCommandType::PADDLE_ON:
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      virtualPaddleOn = true;
      beginCycle(ControlSource::WEB);
      if (!session.active) {
        virtualPaddleOn = false;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_PADDLE_ON,
                    static_cast<int32_t>(command.type));
      return;

    case WebCommandType::RINSE:
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      beginWebRinse();
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_RINSE,
                    static_cast<int32_t>(command.type));
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
      runtimeConfig = candidate;
      WebCommand persist;
      persist.type = WebCommandType::PERSIST_RUNTIME;
      persist.requestId = command.requestId;
      persist.config = runtimeConfig;
      forwardAcceptedNetworkCommand(persist);
      addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                    static_cast<int32_t>(runtimeConfig.revision));
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
      runtimeConfig.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
      ++runtimeConfig.revision;
      if (runtimeConfig.revision == 0) {
        runtimeConfig.revision = 1;
      }
      WebCommand persist;
      persist.type = WebCommandType::PERSIST_RUNTIME;
      persist.requestId = command.requestId;
      persist.config = runtimeConfig;
      forwardAcceptedNetworkCommand(persist);
      addDebugEvent(DebugCategory::CONFIG, DebugCode::WEIGHT_OFFSET_RESET,
                    static_cast<int32_t>(runtimeConfig.revision));
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
      forwardAcceptedNetworkCommand(command);
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      return;

    case WebCommandType::PERSIST_RUNTIME:
      rejectWebCommand(command);
      return;
  }
}

void processWebCommands() {
  if (webCommandQueue == nullptr) {
    return;
  }
  WebCommand command;
  size_t processed = 0;
  while (processed < WEB_COMMAND_QUEUE_LENGTH &&
         xQueueReceive(webCommandQueue, &command, 0) == pdTRUE) {
    processWebCommand(command);
    command = WebCommand{};
    ++processed;
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
  next.source = session.active ? session.source : ControlSource::NONE;
  next.cycleId = session.active ? session.id : 0;
  next.cn9ElapsedMs = relay.closed ? elapsedMs(relay.closedAtMs) : 0;
  next.scaleAvailable = scaleLinkAvailable(scaleLink);
  next.currentWeightValid = next.scaleAvailable && currentWeightSequence > 0 &&
                            isfinite(currentWeight) &&
                            static_cast<uint32_t>(now -
                                                  currentWeightReceivedAtMs) <=
                                SCALE_WORKER_STALE_MS;
  next.currentWeightG = next.currentWeightValid ? currentWeight : 0.0f;
  next.currentWeightAgeMs =
      next.currentWeightValid
          ? static_cast<uint32_t>(now - currentWeightReceivedAtMs)
          : 0;
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

void updateStatusLed() {
  switch (stopperState) {
    case StopperState::BREW:
      setColor((millis() / 1000U) % 2U ? COLOR_GREEN : COLOR_BLUE);
      break;
    case StopperState::RINSE:
      setColor(COLOR_CYAN);
      break;
    case StopperState::MANUAL_NO_SCALE:
      setColor(COLOR_MAGENTA);
      break;
    case StopperState::QUALIFYING_ON:
      setColor(COLOR_YELLOW);
      break;
    case StopperState::REQUIRES_OFF:
      setColor(scaleAvailable() ? COLOR_YELLOW : COLOR_RED);
      break;
    case StopperState::READY:
      setColor(scaleAvailable() ? COLOR_GREEN : COLOR_RED);
      break;
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  // Establish the relay's safe electrical state before Serial, EEPROM or BLE.
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);

  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  setColor(COLOR_OFF);
  initializePaddleInput();
  const bool safetyTimersReady = initializeRelaySafetyTimer();

  setCpuFrequencyMhz(80);
  Serial.begin(9600);
  Serial.println("Shot Stopper Micra initializing");
  if (!safetyTimersReady) {
    Serial.println("FATAL: relay safety timers unavailable; CN9 will not close");
  }

  EEPROM.begin(EEPROM_SIZE);
#ifndef SHOT_STOPPER_HOST_TEST
  bool settingsReady = true;
  if (!loadPersistedSettings(persistedSettings)) {
    bool legacyMigrated = false;
    settingsReady = initializeDefaultSettings(
        persistedSettings, EEPROM.read(WEIGHT_ADDR), EEPROM.read(OFFSET_ADDR),
        &legacyMigrated);
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
#else
  runtimeConfig = RuntimeConfig{};
#endif

  Serial.print("Goal weight: ");
  Serial.println(runtimeConfig.goalWeightG);
  Serial.print("Weight offset: ");
  Serial.println(runtimeConfig.weightOffsetG);

  BLE.begin();
  Serial.println("BLE scale central active; local configuration removed");

  if (!initializeScaleWorker()) {
    Serial.println("Scale worker unavailable; manual mode only");
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
    if (!networkManager.begin(persistedSettings, callbacks)) {
      Serial.println("Network manager unavailable; stopper remains local");
    }
  } else if (!settingsReady) {
    Serial.println("Network settings unavailable; stopper remains local");
  }
  vTaskPrioritySet(nullptr, tskIDLE_PRIORITY + 3);
#endif

  // The Arduino core feeds this watchdog before every loop() call. A stalled
  // control loop therefore resets the MCU and lets the normally-open relay
  // return CN9 to its safe state.
  enableLoopWDT();
}

void loop() {
  // Relay and paddle control never wait for BLE. The worker owns every scale,
  // heartbeat, packet, timer and connection operation.
  updatePaddleInput();
  stateMachineTask();
  servicePaddleReturnReminder();
  processScaleWorkerEvents();
  shotAnalysisTask();
#ifndef SHOT_STOPPER_HOST_TEST
  serviceSerialRecovery();
#endif
  processWebCommands();
  publishControlStatus();
  updateStatusLed();
}
