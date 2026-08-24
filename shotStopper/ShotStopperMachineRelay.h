#pragma once

#include "ShotStopperHardware.h"
#include "ShotStopperMachineTypes.h"
#include "ShotStopperSafety.h"

// K1 electrical driver and independent deadline/feedback safety.
// Included from ShotStopperMachine.h in the shotStopper.cpp translation unit.
// State is file-scope BSS — no heap.

bool readCircuitFeedbackClosed() {
  return EXTERNAL_SAFETY_HARDWARE_PRESENT &&
         digitalRead(CIRCUIT_FEEDBACK_GPIO) == CIRCUIT_FEEDBACK_CLOSED_LEVEL;
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
  circuitClosed = false;
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
      static_cast<uint32_t>(callbackAtMs - circuitClosedAtMs) >=
          HARD_MAX_CIRCUIT_CLOSED_MS) {
    tripRelaySafetyLocked(RelaySafetyFault::HARD_LIMIT, true, false, false);
  }
  portEXIT_CRITICAL(&relayMux);
}

void operationalLimitTimerCallback(void *) {
  const uint32_t callbackAtMs = millis();
  portENTER_CRITICAL(&relayMux);
  if ((relaySafetyState == RelaySafetyState::ARMING ||
       relaySafetyState == RelaySafetyState::CLOSED) &&
      operationalLimitAtArmMs < HARD_MAX_CIRCUIT_CLOSED_MS &&
      static_cast<uint32_t>(callbackAtMs - circuitClosedAtMs) >=
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
// machine circuit first (IRAM GPIO path), then clear the RTC close marker. Avoid heap or
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
        operationalLimitAtArmMs < HARD_MAX_CIRCUIT_CLOSED_MS;
    // Keep this ISR minimal and IRAM-safe. The control task performs timer
    // cleanup, RTC OPEN publication and logging after observing the latched
    // trip flags. If reset wins that race, the retained CLOSE marker causes a
    // conservative boot lockout.
    openRelayElectricalFromIsr();
    circuitClosed = false;
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
  hardArgs.name = "circuit_hard_limit";
  if (esp_timer_create(&hardArgs, &relaySafetyTimer) != ESP_OK) {
    return false;
  }

  esp_timer_create_args_t operationalArgs = {};
  operationalArgs.callback = &operationalLimitTimerCallback;
  operationalArgs.arg = nullptr;
  operationalArgs.dispatch_method = ESP_TIMER_TASK;
  operationalArgs.name = "circuit_oper_limit";
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
  snapshot.closed = circuitClosed;
  snapshot.commandedClosed = relaySafetyState == RelaySafetyState::ARMING ||
                             relaySafetyState == RelaySafetyState::CLOSED;
  snapshot.feedbackAvailable = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  snapshot.externalSafetyPresent = EXTERNAL_SAFETY_HARDWARE_PRESENT;
  snapshot.watchdogReady = taskWatchdogReady;
  snapshot.timersReady = relaySafetyTimersReady;
  snapshot.tripped = relaySafetyTripped;
  snapshot.operationalTripped = operationalLimitTripped;
  snapshot.generation = relaySafetyGeneration;
  snapshot.closedAtMs = circuitClosedAtMs;
  snapshot.operationalLimitMs = operationalLimitAtArmMs;
  snapshot.resetReasonCode = safetyResetStatus.reasonCode;
  snapshot.unsafeResetCount = safetyResetStatus.unsafeResetCount;
  snapshot.resetRecoveryRequired = safetyResetStatus.recoveryRequired;
  snapshot.bootLoopDetected = safetyResetStatus.bootLoopDetected;
  portEXIT_CRITICAL(&relayMux);
  snapshot.feedbackClosed = readCircuitFeedbackClosed();
  return snapshot;
}

void applyBrewRfPreference(bool preferBluetooth) {
#if defined(SHOT_STOPPER_HAS_COEX)
  // Prefer BLE airtime while an automatic brew needs a fresh weight stream.
  // Restore balance as soon as machine circuit opens so the Web UI stays responsive.
  (void)esp_coex_preference_set(preferBluetooth ? ESP_COEX_PREFER_BT
                                                : ESP_COEX_PREFER_BALANCE);
#else
  (void)preferBluetooth;
#endif
#if !defined(SHOT_STOPPER_HOST_TEST)
  // Pause on the machine circuit-close path (stopAdvertise is fast). Resume is owned by
  // the BLE worker via syncCompanionAdvertisingForScaleLink so opening machine circuit
  // never blocks the control task in BLE.advertise() before the stop beep.
  if (preferBluetooth && bleCompanion != nullptr) {
    bleCompanion->setAdvertisingPaused(true);
  }
#else
  (void)preferBluetooth;
#endif
}

bool setMachineCircuitClosed(bool closed,
                  uint32_t operationalLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS) {
  if (closed) {
    const RelaySafetySnapshot before = getRelaySafetySnapshot();
    if (before.closed) {
      return true;
    }

    if (operationalLimitMs < 1 ||
        operationalLimitMs > HARD_MAX_CIRCUIT_CLOSED_MS) {
      tripRelaySafety(RelaySafetyFault::INVALID_LIMIT);
      addDebugEvent(DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
                    static_cast<int32_t>(CircuitArmFailReason::INVALID_LIMIT));
      return false;
    }
    if (before.state == RelaySafetyState::LOCKOUT) {
      addDebugEvent(DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
                    static_cast<int32_t>(CircuitArmFailReason::SAFETY_LOCKOUT));
      return false;
    }
    if (!platformClockReady || !relaySafetyTimersReady || !taskWatchdogReady ||
        relaySafetyTimer == nullptr || operationalLimitTimer == nullptr ||
        !independentSafetyTimer.ready() || criticalTaskWatchdogFault) {
      tripRelaySafety(!taskWatchdogReady || criticalTaskWatchdogFault
                          ? RelaySafetyFault::WATCHDOG_UNAVAILABLE
                          : RelaySafetyFault::INITIALIZATION_FAILED);
      addDebugEvent(
          DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
          static_cast<int32_t>(CircuitArmFailReason::SUPERVISOR_UNAVAILABLE));
      return false;
    }
    if (EXTERNAL_SAFETY_HARDWARE_PRESENT && readCircuitFeedbackClosed()) {
      tripRelaySafety(RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
      addDebugEvent(
          DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
          static_cast<int32_t>(CircuitArmFailReason::FEEDBACK_STUCK_CLOSED));
      return false;
    }

    stopRelayDeadlineTimers();
    const uint32_t closingAtMs = millis();
    uint32_t generation;
    // A momentary stop pulse may re-close K1 after tripRelaySafety opened it.
    // Keep the trip flags so stateMachineTask still finalizes the brew cycle.
    const bool preserveTripFlags =
        before.tripped || before.operationalTripped;
    portENTER_CRITICAL(&relayMux);
    generation = ++relaySafetyGeneration;
    circuitClosedAtMs = closingAtMs;
    operationalLimitAtArmMs = operationalLimitMs;
    if (!preserveTripFlags) {
      relaySafetyTripped = false;
      operationalLimitTripped = false;
      relaySafetyFault = RelaySafetyFault::NONE;
    }
    relaySafetyState = RelaySafetyState::ARMING;
    circuitClosed = false;
    portEXIT_CRITICAL(&relayMux);

    bool armed = independentSafetyTimer.arm(
        operationalLimitMs < HARD_MAX_CIRCUIT_CLOSED_MS
            ? operationalLimitMs
            : HARD_MAX_CIRCUIT_CLOSED_MS);
    if (esp_timer_start_once(
            relaySafetyTimer,
            static_cast<uint64_t>(HARD_MAX_CIRCUIT_CLOSED_MS) * 1000ULL) !=
        ESP_OK) {
      armed = false;
    }
    if (operationalLimitMs < HARD_MAX_CIRCUIT_CLOSED_MS &&
        esp_timer_start_once(
            operationalLimitTimer,
            static_cast<uint64_t>(operationalLimitMs) * 1000ULL) != ESP_OK) {
      armed = false;
    }

    if (!armed) {
      tripRelaySafety(RelaySafetyFault::TIMER_ARM_FAILED);
      addDebugEvent(DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
                    static_cast<int32_t>(CircuitArmFailReason::TIMER_ARM_FAILED));
      return false;
    }

#ifdef SHOT_STOPPER_HOST_TEST
    if (hostCircuitArmBeforeCommitHook != nullptr) {
      hostCircuitArmBeforeCommitHook();
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
      circuitClosed = true;
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
      addDebugEvent(DebugCategory::RELAY, DebugCode::CIRCUIT_ARM_FAILED,
                    static_cast<int32_t>(CircuitArmFailReason::ARM_CANCELED));
      return false;
    }
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_CLOSED,
                  static_cast<int32_t>(operationalLimitMs));
    if (machinePreferBleAirtime) {
      pendingBrewRfRestore = false;
      applyBrewRfPreference(true);
    }
    return true;
  }

  portENTER_CRITICAL(&relayMux);
  const bool wasClosed = circuitClosed ||
                         relaySafetyState == RelaySafetyState::ARMING;
  // Open before stopping either timer. A preemption after this write is safe.
  const bool alreadyOpenedBySafety =
      !circuitClosed && (relaySafetyState == RelaySafetyState::TRIPPED ||
                     relaySafetyState == RelaySafetyState::LOCKOUT);
  if (!alreadyOpenedBySafety) {
    digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  }
  recordRelayCommandedClosed(false);
  circuitClosed = false;
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
  // Coex restore runs after the machine circuit-open beep (servicePendingBrewRfRestore).
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
        relay.operationalLimitMs < HARD_MAX_CIRCUIT_CLOSED_MS;
    tripRelaySafety(operational ? RelaySafetyFault::OPERATIONAL_LIMIT
                               : RelaySafetyFault::HARD_LIMIT,
                    !operational, operational, false);
    return;
  }

  if (!EXTERNAL_SAFETY_HARDWARE_PRESENT) {
    return;
  }

  const bool feedbackClosed = readCircuitFeedbackClosed();
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
    if (elapsedMs(settleStartedAtMs) < CIRCUIT_FEEDBACK_SETTLE_MS) {
      return;
    }
    portENTER_CRITICAL(&relayMux);
    const bool stillPending = feedbackTransitionPending &&
        elapsedMs(feedbackTransitionStartedAtMs) >= CIRCUIT_FEEDBACK_SETTLE_MS;
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
