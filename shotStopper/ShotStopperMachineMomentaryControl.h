#pragma once

#include "ShotStopperMachineMomentaryConfig.h"

// =============================================================================
// SPECIALIZATION: Momentary switch — actuator (control)
// =============================================================================
// WHAT: K1 mirrors the physical switch 1:1 when the façade allows activator
//       drive. Firmware uses synthetic pulses for a remote start and for a
//       stop (weight cut / walls), aborted if the user presses. Logical run walls reuse
//       tripRelaySafety so brew sees existing flags.
//
// BOUNDARY: Momentary-only. No paddle latch/PaddleMode policy. Stopper/brew
// call machineRequestStart/Stop on the façade and must not know about pulses
// or 1:1 mirroring. Run state lives in MomentaryOnlyState / MomentaryReedState.

void abortFirmwarePulseIfActive() {
  if (pulseOutputActive) {
    pulseOutputActive = false;
    if (!momentaryPhysicalOn && getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(false);
    }
  }
}

void serviceFirmwarePulseDrive() {
  if (!pulseOutputActive) {
    if (getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(false);
    }
    return;
  }
  if (static_cast<int32_t>(millis() - pulseOutputEndsAtMs) >= 0) {
    (void)setMachineCircuitClosed(false);
    pulseOutputActive = false;
  } else if (!getRelaySafetySnapshot().closed) {
    (void)setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS);
  }
}

void applyMomentaryRelayDrive() {
  if (rinseActuationActive) {
    serviceFirmwarePulseDrive();
    return;
  }
  if (momentaryPhysicalOn) {
    if (!machineActivatorDriveSuppressedThisHold) {
      abortFirmwarePulseIfActive();
    } else if (pulseOutputActive) {
      serviceFirmwarePulseDrive();
      return;
    }
    if (!machineMayForwardActivatorOn()) {
      if (getRelaySafetySnapshot().closed) {
        (void)setMachineCircuitClosed(false);
      }
      return;
    }
    if (!getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS);
    }
    return;
  }
  machineNoteActivatorReleased();
  if (pulseOutputActive) {
    serviceFirmwarePulseDrive();
    return;
  }
  if (getRelaySafetySnapshot().closed) {
    (void)setMachineCircuitClosed(false);
  }
}

bool emitFirmwarePulse(bool isStart) {
  if (!rinseActuationActive && momentaryPhysicalOn) {
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
  pulseOutputIsStart = isStart;
  pulseOutputEndsAtMs = millis() + durationMs;
#if SHOT_STOPPER_MACHINE_TYPE == 1
  if (!isStart) {
    momentaryFirmwareCutPending = true;
  }
#endif
  return true;
}

bool emitFirmwareStopPulse() { return emitFirmwarePulse(false); }

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

#if SHOT_STOPPER_MACHINE_TYPE == 1
void endMomentaryLogicalRunForWall() {
  const bool cut = machineAllowsFirmwareStopPulse();
  latchMomentaryElapsed();
  momentaryLogicalRunActive = false;
  if (cut) {
    maybeEmitFirmwareStopPulse();
    noteMomentaryLogicalStop();
    return;
  }
  if (momentaryInferredState != MachineRunState::CONFIRMED_OFF) {
    momentaryOrphanRun = true;
  }
}
#endif

void serviceLogicalRunWalls() {
  if (!momentaryLogicalRunActive) {
    return;
  }
  const uint32_t elapsed = elapsedMs(momentaryLogicalRunStartedAtMs);
  if (elapsed >= HARD_MAX_CIRCUIT_CLOSED_MS) {
#if SHOT_STOPPER_MACHINE_TYPE == 1
    if (machineAllowsFirmwareStopPulse()) {
      tripRelaySafety(RelaySafetyFault::HARD_LIMIT, true, false, false);
      endMomentaryLogicalRunForWall();
    } else if (!momentarySawScale && !machineSense.weightFresh) {
      latchMomentaryElapsed();
      settleMomentaryInferredOff();
      momentaryNoFlowIdlePending = true;
    } else {
      tripRelaySafety(RelaySafetyFault::HARD_LIMIT, true, false, false);
      endMomentaryLogicalRunForWall();
    }
#else
    tripRelaySafety(RelaySafetyFault::HARD_LIMIT, true, false, false);
    latchMomentaryElapsed();
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
    endMomentaryLogicalRunForWall();
#else
    latchMomentaryElapsed();
    momentaryLogicalRunActive = false;
    if (machineAllowsFirmwareStopPulse()) {
      maybeEmitFirmwareStopPulse();
      noteMomentaryLogicalStop();
    }
#endif
  }
}

inline bool machineRequestStart(uint32_t operationalLimitMs,
                                bool remoteActuation) {
  clearMomentaryElapsedLatch();
  noteMomentaryLogicalStart();
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = millis();
  momentaryLogicalOperationalLimitMs = operationalLimitMs;
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (relay.state == RelaySafetyState::LOCKOUT ||
      relay.state == RelaySafetyState::TRIPPED) {
    return false;
  }
  // A physical press already toggles the machine circuit. In particular, a
  // release-edge start must not add a second pulse, which would toggle it back
  // off. A Web start has no physical edge, so it must synthesize that pulse.
  return !remoteActuation || emitFirmwarePulse(true);
}

inline bool machineRequestStop() {
  latchMomentaryElapsed();
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

inline bool machineBeginRinse(uint32_t operationalLimitMs) {
  if (rinseActuationActive) {
    return true;
  }
  rinseActuationActive = true;
  machineActivatorDriveSuppressedThisHold = true;
  abortFirmwarePulseIfActive();
  if (getRelaySafetySnapshot().closed) {
    (void)setMachineCircuitClosed(false);
  }
  clearMomentaryElapsedLatch();
  noteMomentaryLogicalStart();
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = millis();
  momentaryLogicalOperationalLimitMs = operationalLimitMs;
  return emitFirmwarePulse(true);
}

inline bool machineEndRinse() {
  latchMomentaryElapsed();
  momentaryLogicalRunActive = false;
  machineActivatorDriveSuppressedThisHold = true;
  noteMomentaryLogicalStop();
  abortFirmwarePulseIfActive();
  const bool ok = emitFirmwarePulse(false);
  rinseActuationActive = false;
  return ok;
}

inline void machineApplyWorkflowConfig(RuntimeConfig &dst,
                                       const RuntimeConfig &src) {
  dst.stopPulseTenMs = src.stopPulseTenMs;
  dst.maxSinglePressHundredMs = src.maxSinglePressHundredMs;
  dst.momentaryStartOnPress = src.momentaryStartOnPress;
  dst.reedConfirmTimeoutHundredMs = src.reedConfirmTimeoutHundredMs;
  dst.assumeIdleWhenScaleConnects = src.assumeIdleWhenScaleConnects;
  dst.shotReactTimeoutS = src.shotReactTimeoutS;
  dst.rinseGestureMs = src.rinseGestureMs;
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
