#pragma once

// Momentary actuator: K1 is closed only for a replayed press or a firmware stop
// pulse. Long-press after the single-press threshold mirrors the remainder.
// Logical run walls reuse tripRelaySafety so brew sees the existing flags.

void abortStartPulseIfActive() {
  if (pulseOutputActive && pulseOutputIsStart) {
    (void)setMachineCircuitClosed(false);
    pulseOutputActive = false;
  }
}

bool emitPulse(uint32_t durationMs, bool isStart) {
  if (momentaryLongPressActive) {
    return true;
  }
  if (pulseOutputActive) {
    if (!isStart && !pulseOutputIsStart) {
      return true;
    }
    (void)setMachineCircuitClosed(false);
    pulseOutputActive = false;
  }
  if (durationMs < 50U) {
    durationMs = 50U;
  }
  if (!setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS)) {
    return false;
  }
  pulseOutputActive = true;
  pulseOutputIsStart = isStart;
  pulseOutputEndsAtMs = millis() + durationMs;
  return true;
}

void finishPulseOutputIfDue() {
  if (momentaryLongPressActive) {
    if (!getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS);
    }
    return;
  }
  if (pulseOutputActive) {
    if (static_cast<int32_t>(millis() - pulseOutputEndsAtMs) >= 0) {
      (void)setMachineCircuitClosed(false);
      pulseOutputActive = false;
    }
    return;
  }
  if (getRelaySafetySnapshot().closed) {
    (void)setMachineCircuitClosed(false);
  }
}

void maybeEmitFirmwareStopPulse() {
  if (!machineAllowsFirmwareStopPulse()) {
    abortStartPulseIfActive();
    return;
  }
  if (pulseOutputActive && pulseOutputIsStart) {
    abortStartPulseIfActive();
    return;
  }
  (void)emitPulse(COMPILED_STOP_PULSE_MS, false);
}

void serviceLogicalRunWalls() {
  if (!momentaryLogicalRunActive) {
    return;
  }
  const uint32_t elapsed = elapsedMs(momentaryLogicalRunStartedAtMs);
  if (elapsed >= HARD_MAX_CIRCUIT_CLOSED_MS) {
    tripRelaySafety(RelaySafetyFault::HARD_LIMIT, true, false, false);
#if SHOT_STOPPER_MACHINE_TYPE == 1
    const bool cut = machineAllowsFirmwareStopPulse();
    momentaryLogicalRunActive = false;
    if (cut) {
      maybeEmitFirmwareStopPulse();
      noteMomentaryLogicalStop();
    } else if (momentaryInferredState != MachineRunState::CONFIRMED_OFF) {
      momentaryOrphanRun = true;
    }
#else
    momentaryLogicalRunActive = false;
    if (machineAllowsFirmwareStopPulse()) {
      maybeEmitFirmwareStopPulse();
      noteMomentaryLogicalStop();
    }
#endif
    return;
  }
  if (momentaryLogicalOperationalLimitMs < HARD_MAX_CIRCUIT_CLOSED_MS &&
      elapsed >= momentaryLogicalOperationalLimitMs) {
    tripRelaySafety(RelaySafetyFault::OPERATIONAL_LIMIT, false, true, false);
#if SHOT_STOPPER_MACHINE_TYPE == 1
    const bool cut = machineAllowsFirmwareStopPulse();
    momentaryLogicalRunActive = false;
    if (cut) {
      maybeEmitFirmwareStopPulse();
      noteMomentaryLogicalStop();
    } else if (momentaryInferredState != MachineRunState::CONFIRMED_OFF) {
      momentaryOrphanRun = true;
    }
#else
    momentaryLogicalRunActive = false;
    if (machineAllowsFirmwareStopPulse()) {
      maybeEmitFirmwareStopPulse();
      noteMomentaryLogicalStop();
    }
#endif
  }
}

inline bool machineRequestStart(uint32_t operationalLimitMs) {
  noteMomentaryLogicalStart();
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = millis();
  momentaryLogicalOperationalLimitMs = operationalLimitMs;
  const uint32_t durationMs =
      momentaryCapturedPressMs > 0U ? momentaryCapturedPressMs : COMPILED_STOP_PULSE_MS;
  momentaryCapturedPressMs = 0U;
  return emitPulse(durationMs, true);
}

inline bool machineRequestStop() {
  const bool allow = machineAllowsFirmwareStopPulse();
  momentaryLogicalRunActive = false;
  if (!allow) {
    abortStartPulseIfActive();
    return true;
  }
  noteMomentaryLogicalStop();
  if (pulseOutputActive && pulseOutputIsStart) {
    abortStartPulseIfActive();
    return true;
  }
  return emitPulse(COMPILED_STOP_PULSE_MS, false);
}

inline void serviceMachine() {
  serviceMomentaryRunSensors();
  finishPulseOutputIfDue();
  serviceLogicalRunWalls();
  if (momentaryStopRetryPending) {
    momentaryStopRetryPending = false;
    maybeEmitFirmwareStopPulse();
  }
  if (momentaryUserStopThisCycle && !session.active) {
    (void)machineRequestStop();
  }
}
