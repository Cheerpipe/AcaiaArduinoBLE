#pragma once

#include "ShotStopperMachineMomentaryConfig.h"

// =============================================================================
// SPECIALIZATION: Momentary switch — actuator (control)
// =============================================================================
// WHAT: K1 mirrors the physical switch 1:1 when the façade allows activator
//       drive. The only synthetic close is a firmware stop pulse (weight cut /
//       walls), aborted if the user presses. Logical run walls reuse
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

void applyMomentaryRelayDrive() {
  if (momentaryPhysicalOn) {
    abortFirmwarePulseIfActive();
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
#if SHOT_STOPPER_MACHINE_TYPE == 1
  momentaryFirmwareCutPending = true;
#endif
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

inline bool machineRequestStart(uint32_t operationalLimitMs) {
  clearMomentaryElapsedLatch();
  noteMomentaryLogicalStart();
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = millis();
  momentaryLogicalOperationalLimitMs = operationalLimitMs;
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  return relay.state != RelaySafetyState::LOCKOUT &&
         relay.state != RelaySafetyState::TRIPPED;
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

inline void machineApplyWorkflowConfig(RuntimeConfig &dst,
                                       const RuntimeConfig &src) {
  dst.stopPulseTenMs = src.stopPulseTenMs;
  dst.maxSinglePressHundredMs = src.maxSinglePressHundredMs;
  dst.momentaryStartOnPress = src.momentaryStartOnPress;
  dst.reedConfirmTimeoutHundredMs = src.reedConfirmTimeoutHundredMs;
  dst.assumeIdleWhenScaleConnects = src.assumeIdleWhenScaleConnects;
  dst.shotReactTimeoutS = src.shotReactTimeoutS;
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
