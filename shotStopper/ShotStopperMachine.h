#pragma once

#include "ShotStopperHardware.h"
#include "ShotStopperMachineTypes.h"
#include "ShotStopperSafety.h"

// Machine driver (paddle intention + latched CN9 actuator + relay-echo sense).
// Included from shotStopper.cpp after logging helpers and BSS globals so host
// tests keep seeing paddleOn/cn9Closed in the same translation unit.
// State is file-scope BSS — no heap.

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

#ifndef SHOT_STOPPER_HOST_TEST
void IRAM_ATTR shotStopperPanicHandler(arduino_panic_info_t *, void *) {
  // The Arduino core invokes this before its normal panic/reboot path. Keep
  // it allocation-free and flash-independent: only de-energize K1 through
  // the same direct-register path used by the independent safety timer.
  openRelayElectricalFromIsr();
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
#if !defined(SHOT_STOPPER_HOST_TEST)
  // Pause on the CN9-close path (stopAdvertise is fast). Resume is owned by
  // the BLE worker via syncCompanionAdvertisingForScaleLink so opening CN9
  // never blocks the control task in BLE.advertise() before the stop beep.
  if (preferBluetooth && bleCompanion != nullptr) {
    bleCompanion->setAdvertisingPaused(true);
  }
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
      pendingBrewRfRestore = false;
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
  // Coex restore runs after the CN9-open beep (servicePendingBrewRfRestore).
  pendingBrewRfRestore = true;
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

void initializeRelaySafetyStateAfterBoot() {
  portENTER_CRITICAL(&relayMux);
  relaySafetyState = platformClockReady && relaySafetyTimersReady &&
                             taskWatchdogReady
                         ? RelaySafetyState::OPEN
                         : RelaySafetyState::LOCKOUT;
  relaySafetyFault =
      !platformClockReady || !relaySafetyTimersReady
          ? RelaySafetyFault::INITIALIZATION_FAILED
          : (!taskWatchdogReady ? RelaySafetyFault::WATCHDOG_UNAVAILABLE
                                : RelaySafetyFault::NONE);
  portEXIT_CRITICAL(&relayMux);
}

// One snapshot: closed + elapsed. Callers that need both must use this so an
// ISR open between two getRelaySafetySnapshot() calls cannot stamp 0 ms.
inline bool machineRunningElapsed(uint32_t &elapsedOut) {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (!relay.closed) {
    elapsedOut = 0U;
    return false;
  }
  elapsedOut = elapsedMs(relay.closedAtMs);
  return true;
}

inline bool machineIsRunning() {
  return getRelaySafetySnapshot().closed;
}

inline uint32_t machineElapsedMs() {
  uint32_t elapsed = 0U;
  (void)machineRunningElapsed(elapsed);
  return elapsed;
}

inline MachineRunState machineRunState() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (relay.state == RelaySafetyState::LOCKOUT ||
      relay.state == RelaySafetyState::TRIPPED) {
    return relay.closed ? MachineRunState::UNKNOWN : MachineRunState::CONFIRMED_OFF;
  }
  if (relay.state == RelaySafetyState::ARMING) {
    return MachineRunState::ASSUMED_ON;
  }
  if (relay.closed || relay.state == RelaySafetyState::CLOSED) {
    return MachineRunState::CONFIRMED_ON;
  }
  return MachineRunState::CONFIRMED_OFF;
}

inline bool machineRequestStart(uint32_t operationalLimitMs) {
  return setCn9Closed(true, operationalLimitMs);
}

inline bool machineRequestStop() {
  return setCn9Closed(false);
}

struct MachineIntention {
  UserIntent intent = UserIntent::NONE;
  bool holdActive = false;
  bool turnedOn = false;
  bool turnedOff = false;
  bool stablyOff = false;
};

inline MachineIntention machinePollIntention() {
  MachineIntention out;
  out.turnedOn = paddleTurnedOn;
  out.turnedOff = paddleTurnedOff;
  out.holdActive = paddleOn;
  out.stablyOff = paddleIsStablyOff();
  if (paddleTurnedOn) {
    out.intent = UserIntent::REQUEST_START;
  } else if (paddleTurnedOff) {
    out.intent = UserIntent::REQUEST_STOP;
  } else if (paddleOn) {
    out.intent = UserIntent::HOLD_ACTIVE;
  } else if (out.stablyOff) {
    out.intent = UserIntent::STABLE_IDLE;
  }
  return out;
}
