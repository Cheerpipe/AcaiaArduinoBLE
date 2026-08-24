#pragma once

// =============================================================================
// SPECIALIZATION: Momentary-only — run view (state)
// =============================================================================
// WHAT: MachineRunState without reed. Never canonical: boot is OFF, a missing
//       scale is UNKNOWN (no auto-cut), quiet pan reinforces OFF, and
//       espresso-like rising flow (g/s) is the only path to CONFIRMED_ON.
//       CONFIRMED_ON expires without recent flow so auto-cut cannot pulse a
//       stopped group. Consumes MachineSense pushed by the stopper.
//
// BOUNDARY: This file alone determines momentary-only run state. Do not put
// reed GPIO here (that is MomentaryReedState). Do not put paddle latch logic
// here. Brew/guards must not re-derive this — they use machineRunState().

constexpr float MOMENTARY_FLOW_ON_G_S = 0.60f;
constexpr float MOMENTARY_FLOW_OFF_G_S = 0.20f;
constexpr float MOMENTARY_TARE_RESET_G = 1.0f;
constexpr float MOMENTARY_FINGER_JUMP_G = 15.0f;
constexpr float MOMENTARY_BASELINE_BAND_G = 0.5f;
constexpr float MOMENTARY_CONFIRM_DELTA_G = 1.0f;
constexpr uint32_t MOMENTARY_FLOW_HOLD_MS = 500;
constexpr uint32_t MOMENTARY_FLOW_GAP_SKIP_MS = 500;
constexpr uint32_t MOMENTARY_QUIET_MS = 1000;
constexpr uint32_t MOMENTARY_STOP_ACK_MS = 1500;
constexpr uint32_t MOMENTARY_STOP_RETRY_AFTER_MS = 600;
constexpr uint32_t MOMENTARY_PREINFUSION_MS = 8000;
constexpr uint32_t MOMENTARY_START_NACK_MS = 12000;
constexpr uint32_t MOMENTARY_CONFIRM_MIN_RUN_MS = 800;
constexpr uint32_t MOMENTARY_STALE_UNKNOWN_MS = 1500;

MachineRunState momentaryInferredState = MachineRunState::CONFIRMED_OFF;
bool momentarySawScale = false;
bool momentaryEspressoConfirmed = false;
float momentaryShotBaselineG = 0.0f;
float momentaryQuietBaselineG = 0.0f;
uint32_t momentaryQuietSinceMs = 0;
float momentaryLastWeightG = 0.0f;
uint32_t momentaryLastWeightAtMs = 0;
uint32_t momentaryRisingSinceMs = 0;
uint32_t momentaryLowFlowSinceMs = 0;
bool momentaryStopAwaitingAck = false;
uint32_t momentaryStopAwaitingSinceMs = 0;
bool momentaryStopRetryPending = false;
bool momentaryStartAwaitingAck = false;
uint32_t momentaryStartBaselineSinceMs = 0;
bool momentaryOrphanRun = false;
uint32_t momentaryStaleSinceMs = 0;
bool momentaryFirmwareCutPending = false;
bool momentarySettledWeightCutArmed = false;

bool reedIsOn() { return false; }

inline float momentaryLiveWeightG() { return machineSense.weightG; }

void noteMomentaryLogicalStart() {
  momentaryInferredState = MachineRunState::ASSUMED_ON;
  momentaryEspressoConfirmed = false;
  momentaryRisingSinceMs = 0;
  momentaryLowFlowSinceMs = 0;
  momentaryStopAwaitingAck = false;
  momentaryStopRetryPending = false;
  momentaryQuietSinceMs = 0;
  momentaryStartAwaitingAck = true;
  momentaryStartBaselineSinceMs = 0;
  momentaryOrphanRun = false;
  momentaryStaleSinceMs = 0;
  momentaryFirmwareCutPending = false;
  momentarySettledWeightCutArmed = false;
  if (machineSense.weightFresh) {
    momentarySawScale = true;
    const float weight = momentaryLiveWeightG();
    momentaryShotBaselineG = weight;
    momentaryQuietBaselineG = weight;
    momentaryLastWeightG = weight;
    momentaryLastWeightAtMs = millis();
  } else {
    momentaryShotBaselineG = 0.0f;
  }
}

void noteMomentaryLogicalStop() {
  if (!momentaryLogicalRunActive &&
      momentaryInferredState == MachineRunState::CONFIRMED_OFF &&
      !momentaryStartAwaitingAck) {
    return;
  }
  momentaryEspressoConfirmed = false;
  momentaryRisingSinceMs = 0;
  momentaryLowFlowSinceMs = 0;
  momentaryStopRetryPending = false;
  momentaryStartAwaitingAck = false;
  momentaryStartBaselineSinceMs = 0;
  if (!machineSense.weightFresh) {
    momentaryStopAwaitingAck = false;
    momentaryInferredState = momentarySawScale ? MachineRunState::UNKNOWN
                                       : MachineRunState::CONFIRMED_OFF;
    return;
  }
  momentaryStopAwaitingAck = true;
  momentaryStopAwaitingSinceMs = millis();
  if (momentaryInferredState == MachineRunState::CONFIRMED_ON) {
    momentaryInferredState = MachineRunState::ASSUMED_ON;
  }
}

void noteMomentaryLogicalStartCanceled() {
  momentaryEspressoConfirmed = false;
  momentaryRisingSinceMs = 0;
  momentaryLowFlowSinceMs = 0;
  momentaryStopAwaitingAck = false;
  momentaryStopRetryPending = false;
  momentaryStartAwaitingAck = false;
  momentaryStartBaselineSinceMs = 0;
  momentaryOrphanRun = false;
  momentaryInferredState = MachineRunState::CONFIRMED_OFF;
}

void noteMomentaryDisqualifiedPress(bool restoreRunning) {
  if (!restoreRunning) {
    noteMomentaryLogicalStartCanceled();
    return;
  }
  momentaryStopAwaitingAck = false;
  momentaryStopRetryPending = false;
  momentaryStartAwaitingAck = false;
  momentaryStartBaselineSinceMs = 0;
  momentaryOrphanRun = false;
  momentaryInferredState = momentaryGesturePreState;
  if (momentaryInferredState == MachineRunState::CONFIRMED_OFF) {
    momentaryInferredState = MachineRunState::ASSUMED_ON;
  }
}

void settleMomentaryInferredOff() {
  momentaryEspressoConfirmed = false;
  momentaryRisingSinceMs = 0;
  momentaryLowFlowSinceMs = 0;
  momentaryStopAwaitingAck = false;
  momentaryStopRetryPending = false;
  momentaryStartAwaitingAck = false;
  momentaryStartBaselineSinceMs = 0;
  momentaryOrphanRun = false;
  momentaryLogicalRunActive = false;
  momentaryFirmwareCutPending = false;
  momentarySettledWeightCutArmed = false;
  momentaryInferredState = MachineRunState::CONFIRMED_OFF;
}

void machineOverrideInferredOff() { settleMomentaryInferredOff(); }

void machineOverrideInferredOn() {
  momentaryStopAwaitingAck = false;
  momentaryStopRetryPending = false;
  momentaryStartAwaitingAck = false;
  momentaryStartBaselineSinceMs = 0;
  momentaryOrphanRun = false;
  momentaryFirmwareCutPending = false;
  momentarySettledWeightCutArmed = false;
  momentaryEspressoConfirmed = true;
  momentaryInferredState = MachineRunState::CONFIRMED_ON;
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = millis();
  momentaryLogicalOperationalLimitMs = runtimeConfig.operationalWallMs;
}

void machineArmSettledWeightCutOff() { momentarySettledWeightCutArmed = true; }

void machineCancelSettledWeightCutOff() {
  momentarySettledWeightCutArmed = false;
}

void machineNoteSettledWeightCutOff() {
  if (!momentarySettledWeightCutArmed) {
    return;
  }
  settleMomentaryInferredOff();
}

void serviceMomentaryRunSensors() {
  if (machineSense.scaleConnectedEdge &&
      runtimeConfig.assumeIdleWhenScaleConnects &&
      !machineSense.brewCycleActive) {
    settleMomentaryInferredOff();
  }
  if (momentaryLogicalRunActive &&
      momentaryInferredState == MachineRunState::CONFIRMED_OFF &&
      !momentaryStopAwaitingAck && momentaryStartAwaitingAck) {
    momentaryInferredState = MachineRunState::ASSUMED_ON;
  }
  if (!machineSense.weightFresh) {
    if (momentaryStaleSinceMs == 0) {
      momentaryStaleSinceMs = millis();
    }
    if (momentaryInferredState == MachineRunState::CONFIRMED_ON) {
      momentaryEspressoConfirmed = false;
      momentaryInferredState = MachineRunState::ASSUMED_ON;
    }
    if ((momentaryLogicalRunActive || momentarySawScale) &&
        elapsedMs(momentaryStaleSinceMs) >= MOMENTARY_STALE_UNKNOWN_MS) {
      momentaryInferredState = MachineRunState::UNKNOWN;
    }
    momentaryQuietSinceMs = 0;
    momentaryRisingSinceMs = 0;
    momentaryLowFlowSinceMs = 0;
    momentaryStartBaselineSinceMs = 0;
    if (momentaryStopAwaitingAck &&
        elapsedMs(momentaryStopAwaitingSinceMs) >= MOMENTARY_STOP_ACK_MS) {
      momentaryStopAwaitingAck = false;
      if (momentarySawScale) {
        momentaryInferredState = MachineRunState::UNKNOWN;
      }
    }
    return;
  }
  momentaryStaleSinceMs = 0;
  momentarySawScale = true;
  const float weight = momentaryLiveWeightG();
  const uint32_t now = millis();
  const uint32_t dtMs =
      momentaryLastWeightAtMs == 0 ? 0U : elapsedMs(momentaryLastWeightAtMs);
  const float jump = fabsf(weight - momentaryLastWeightG);
  const bool finger = jump > MOMENTARY_FINGER_JUMP_G;
  const bool accidental = machineSense.accidentalHold;
  const bool skipFlow = finger || accidental || dtMs < 40U ||
                        dtMs >= MOMENTARY_FLOW_GAP_SKIP_MS;
  float flowGps = 0.0f;
  if (!skipFlow) {
    flowGps = (weight - momentaryLastWeightG) * 1000.0f / static_cast<float>(dtMs);
  }

  if (momentaryLogicalRunActive && !finger && !accidental &&
      momentaryEspressoConfirmed && weight < MOMENTARY_TARE_RESET_G &&
      momentaryLastWeightG > 2.0f) {
    momentaryShotBaselineG = weight;
    momentaryQuietBaselineG = weight;
    momentaryEspressoConfirmed = false;
    momentaryRisingSinceMs = 0;
    momentaryLowFlowSinceMs = 0;
    if (momentaryInferredState == MachineRunState::CONFIRMED_ON) {
      momentaryInferredState = MachineRunState::ASSUMED_ON;
    }
  }

  if (fabsf(weight - momentaryQuietBaselineG) <= MOMENTARY_BASELINE_BAND_G) {
    if (momentaryQuietSinceMs == 0) {
      momentaryQuietSinceMs = now;
    }
  } else {
    momentaryQuietSinceMs = 0;
    momentaryQuietBaselineG = weight;
  }

  const bool quietPan =
      momentaryQuietSinceMs != 0 && elapsedMs(momentaryQuietSinceMs) >= MOMENTARY_QUIET_MS;
  if (quietPan && !momentaryStartAwaitingAck &&
      (momentaryStopAwaitingAck || momentaryOrphanRun ||
       (!momentaryLogicalRunActive &&
        momentaryInferredState != MachineRunState::CONFIRMED_ON &&
        momentaryInferredState != MachineRunState::ASSUMED_ON &&
        momentaryInferredState != MachineRunState::ASSUMED_OFF))) {
    momentaryStopAwaitingAck = false;
    momentaryOrphanRun = false;
    momentaryEspressoConfirmed = false;
    momentaryInferredState = MachineRunState::CONFIRMED_OFF;
  }

  if (momentaryStopAwaitingAck) {
    if (quietPan) {
      momentaryStopAwaitingAck = false;
      momentaryOrphanRun = false;
      momentaryInferredState = MachineRunState::CONFIRMED_OFF;
    } else if (flowGps >= MOMENTARY_FLOW_ON_G_S &&
               elapsedMs(momentaryStopAwaitingSinceMs) >= MOMENTARY_STOP_RETRY_AFTER_MS) {
      if (momentaryFirmwareCutPending) {
        momentaryStopRetryPending = true;
        momentaryStopAwaitingSinceMs = now;
      } else {
        momentaryStopAwaitingAck = false;
        momentaryEspressoConfirmed = true;
        momentaryInferredState = MachineRunState::CONFIRMED_ON;
        momentaryLogicalRunActive = true;
        if (momentaryLogicalRunStartedAtMs == 0) {
          momentaryLogicalRunStartedAtMs = now;
        }
        momentaryLogicalOperationalLimitMs = runtimeConfig.operationalWallMs;
      }
    } else if (elapsedMs(momentaryStopAwaitingSinceMs) >= MOMENTARY_STOP_ACK_MS) {
      momentaryStopAwaitingAck = false;
    }
  }

  if (momentaryStartAwaitingAck && !momentaryStopAwaitingAck) {
    if (fabsf(weight - momentaryShotBaselineG) <= MOMENTARY_BASELINE_BAND_G) {
      if (momentaryStartBaselineSinceMs == 0) {
        momentaryStartBaselineSinceMs = now;
      }
      if (elapsedMs(momentaryStartBaselineSinceMs) >=
          runtimeShotReactTimeoutMs(runtimeConfig)) {
        momentaryStartAwaitingAck = false;
        momentaryStartBaselineSinceMs = 0;
        momentaryLogicalRunActive = false;
        momentaryEspressoConfirmed = false;
        momentaryOrphanRun = false;
        momentaryInferredState = MachineRunState::ASSUMED_OFF;
      }
    } else {
      momentaryStartBaselineSinceMs = 0;
    }
  }

  if (momentaryInferredState == MachineRunState::CONFIRMED_ON &&
      !momentaryStopAwaitingAck && !skipFlow) {
    if (flowGps < MOMENTARY_FLOW_OFF_G_S) {
      if (momentaryLowFlowSinceMs == 0) {
        momentaryLowFlowSinceMs = now;
      }
      if (elapsedMs(momentaryLowFlowSinceMs) >= MOMENTARY_FLOW_HOLD_MS) {
        momentaryEspressoConfirmed = false;
        momentaryLowFlowSinceMs = 0;
        momentaryInferredState = MachineRunState::ASSUMED_ON;
      }
    } else {
      momentaryLowFlowSinceMs = 0;
    }
  } else if (momentaryInferredState != MachineRunState::CONFIRMED_ON) {
    momentaryLowFlowSinceMs = 0;
  }

  const float delta = weight - momentaryShotBaselineG;
  const bool inShotBand =
      delta >= MOMENTARY_CONFIRM_DELTA_G &&
      delta <= runtimeConfig.maxRecoveryWeightG;
  const bool canConfirmOn =
      (momentaryLogicalRunActive ||
       momentaryInferredState == MachineRunState::ASSUMED_ON ||
       momentaryInferredState == MachineRunState::ASSUMED_OFF) &&
      !finger && !accidental && !skipFlow && !momentaryStopAwaitingAck &&
      momentaryInferredState != MachineRunState::CONFIRMED_OFF && inShotBand &&
      flowGps >= MOMENTARY_FLOW_ON_G_S && flowGps <= 8.0f;
  if (canConfirmOn) {
    if (momentaryRisingSinceMs == 0) {
      momentaryRisingSinceMs = now;
    }
    if (elapsedMs(momentaryRisingSinceMs) >= MOMENTARY_FLOW_HOLD_MS &&
        elapsedMs(momentaryLogicalRunStartedAtMs) >= MOMENTARY_CONFIRM_MIN_RUN_MS) {
      const bool recoverFromAssumedOff =
          momentaryInferredState == MachineRunState::ASSUMED_OFF;
      momentaryEspressoConfirmed = true;
      momentaryStartAwaitingAck = false;
      momentaryStartBaselineSinceMs = 0;
      momentaryInferredState = MachineRunState::CONFIRMED_ON;
      if (recoverFromAssumedOff) {
        momentaryLogicalRunActive = true;
        if (momentaryLogicalRunStartedAtMs == 0) {
          momentaryLogicalRunStartedAtMs = now;
        }
        momentaryLogicalOperationalLimitMs = runtimeConfig.operationalWallMs;
      }
    }
  } else if (skipFlow || flowGps < MOMENTARY_FLOW_OFF_G_S) {
    momentaryRisingSinceMs = 0;
  }

  momentaryLastWeightG = weight;
  momentaryLastWeightAtMs = now;
}

bool machineAllowsFirmwareStopPulse() {
  return momentaryInferredState == MachineRunState::CONFIRMED_ON &&
         !momentaryPhysicalOn;
}

inline bool machineRunningElapsed(uint32_t &elapsedOut) {
  if (!(momentaryLogicalRunActive || momentaryStopAwaitingAck ||
        momentaryOrphanRun ||
        momentaryInferredState == MachineRunState::CONFIRMED_ON ||
        momentaryInferredState == MachineRunState::ASSUMED_ON)) {
    elapsedOut = 0U;
    return false;
  }
  elapsedOut = elapsedMs(momentaryLogicalRunStartedAtMs);
  return true;
}

inline bool machineIsRunning() {
  return momentaryLogicalRunActive || momentaryStopAwaitingAck ||
         momentaryOrphanRun ||
         momentaryInferredState == MachineRunState::CONFIRMED_ON ||
         momentaryInferredState == MachineRunState::ASSUMED_ON;
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
    return momentaryLogicalRunActive ? MachineRunState::UNKNOWN
                                 : MachineRunState::CONFIRMED_OFF;
  }
  return momentaryInferredState;
}

inline void machineFillInferenceStatus(ControlStatusSnapshot &status) {
  status.machineStartAckPending = momentaryStartAwaitingAck;
  status.machineStopAckPending = momentaryStopAwaitingAck;
  status.machineOrphanRun = momentaryOrphanRun;
}
