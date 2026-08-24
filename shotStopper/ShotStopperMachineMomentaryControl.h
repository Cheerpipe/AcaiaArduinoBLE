#pragma once

#include "ShotStopperMachineMomentaryConfig.h"

// Momentary actuator: K1 mirrors the physical switch 1:1. The only synthetic
// close is a firmware stop pulse (weight cut / walls), aborted if the user
// presses. Logical run walls reuse tripRelaySafety so brew sees existing flags.

void abortFirmwarePulseIfActive() {
  if (pulseOutputActive) {
    pulseOutputActive = false;
    if (!momentaryPhysicalOn && getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(false);
    }
  }
}

void applyMomentaryRelayDrive() {
  if (momentaryPhysicalOn) {
    abortFirmwarePulseIfActive();
    if (!getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS);
    }
    return;
  }
  if (pulseOutputActive) {
    if (static_cast<int32_t>(millis() - pulseOutputEndsAtMs) >= 0) {
      (void)setMachineCircuitClosed(false);
      pulseOutputActive = false;
    } else if (!getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS);
    }
    return;
  }
  if (getRelaySafetySnapshot().closed) {
    (void)setMachineCircuitClosed(false);
  }
}

bool emitFirmwareStopPulse() {
  if (momentaryPhysicalOn) {
    return true;
  }
  if (pulseOutputActive) {
    return true;
  }
  uint32_t durationMs = runtimeStopPulseMs(runtimeConfig);
  if (durationMs < 50U) {
    durationMs = 50U;
  }
  if (!setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS)) {
    return false;
  }
  pulseOutputActive = true;
  pulseOutputIsStart = false;
  pulseOutputEndsAtMs = millis() + durationMs;
  return true;
}

void maybeEmitFirmwareStopPulse() {
  if (momentaryPhysicalOn) {
    abortFirmwarePulseIfActive();
    return;
  }
  if (!machineAllowsFirmwareStopPulse()) {
    abortFirmwarePulseIfActive();
    return;
  }
  (void)emitFirmwareStopPulse();
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
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  return relay.state != RelaySafetyState::LOCKOUT &&
         relay.state != RelaySafetyState::TRIPPED;
}

inline bool machineRequestStop() {
  momentaryLogicalRunActive = false;
  const bool skipPulse =
      momentaryPhysicalOn || momentarySkipFirmwareStopPulse;
  momentarySkipFirmwareStopPulse = false;
  if (skipPulse) {
    abortFirmwarePulseIfActive();
    noteMomentaryLogicalStop();
    applyMomentaryRelayDrive();
    return true;
  }
  if (pulseOutputActive) {
    noteMomentaryLogicalStop();
    return true;
  }
  if (!machineAllowsFirmwareStopPulse()) {
    noteMomentaryLogicalStop();
    applyMomentaryRelayDrive();
    return true;
  }
  noteMomentaryLogicalStop();
  return emitFirmwareStopPulse();
}

inline void machineApplyWorkflowConfig(RuntimeConfig &dst,
                                       const RuntimeConfig &src) {
  dst.stopPulseTenMs = src.stopPulseTenMs;
  dst.maxSinglePressHundredMs = src.maxSinglePressHundredMs;
  dst.momentaryStartOnPress = src.momentaryStartOnPress;
  dst.reedConfirmTimeoutHundredMs = src.reedConfirmTimeoutHundredMs;
}

inline void serviceMachine() {
  serviceMomentaryRunSensors();
  applyMomentaryRelayDrive();
  serviceLogicalRunWalls();
  if (momentaryStopRetryPending) {
    momentaryStopRetryPending = false;
    maybeEmitFirmwareStopPulse();
  }
}
