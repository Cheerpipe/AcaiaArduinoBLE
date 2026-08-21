#pragma once

#include "ShotStopperScaleTypes.h"
#include "ShotStopperShotLogTypes.h"

// Scale sense: trajectory and first-drop detector.
// Cup presence lives in ShotStopperCupPresence.h. Predicts against a
// brew-injected target. No GPIO. No heap; trajectory is BSS.

void resetShotTrajectory(uint32_t startedAtMs) {
  shot.startMs = startedAtMs;
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
  shot.automaticBrew = false;
}

void calculateExpectedEndTime() {
  shot.expectedEndS = predictedWeightStopTimeS(
      shot.timeS, shot.weight, shot.datapoints, activeWeightCutTargetG(),
      session.config.operationalWallMs / 1000.0f);
}

void rejectScaleSample(DebugCode code, float weightG, float referenceG = 0.0f) {
  addDebugEvent(DebugCategory::SCALE, code, weightToCentigrams(weightG),
                weightToCentigrams(referenceG));
}

void armPostTareBaselineWindow() {
  session.awaitingPostTareBaseline = true;
  session.postTareBaselineDeadlineMs =
      millis() + session.config.postTareBaselineGraceMs;
  session.hasWeightAnchor = false;
  session.recoveryConfirmations = 0;
  session.recoveryLastAtMs = 0;
  session.recoveryLastPacketSequence = 0;
  resetWeightTrend();
  notifyCupPresenceTare();
  holdCupPresenceTransitions(true);
  if (session.weightControlState == WeightControlState::VALIDATING) {
    setWeightControlState(WeightControlState::ACTIVE);
  }
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
  holdCupPresenceTransitions(false);
  addDebugEvent(DebugCategory::SCALE,
                DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT,
                static_cast<int32_t>(session.id),
                static_cast<int32_t>(session.config.postTareBaselineGraceMs));
}

void maybeLatchFirstFlowFromAcceptedWeight(float weight,
                                           uint32_t receivedAtMs) {
  if (!session.active || !session.startedWithScale || session.firstDropMs != 0 ||
      !session.scaleBaselineReady) {
    return;
  }
  if (session.accidentalTouchHolding ||
      session.firstFlow.phase == FirstFlowPhase::TOUCH) {
    session.firstFlowAcceptedConfirmations = 0;
    return;
  }
  const float delta = weight - session.scaleBaselineG;
  if (delta < FIRST_DROP_THRESHOLD_G ||
      firstFlowIsCupMass(delta, runtimeConfig.minimumCupWeightG)) {
    session.firstFlowAcceptedConfirmations = 0;
    return;
  }
  ++session.firstFlowAcceptedConfirmations;
  if (session.firstFlowAcceptedConfirmations >=
      FIRST_DROP_CONFIRMATION_SAMPLES) {
    onFirstDropsDetected(receivedAtMs);
  }
}

bool acceptWeightIntoTrajectory(float weight, uint32_t receivedAtMs,
                                uint32_t packetSequence) {
  size_t index;
  if (shot.datapoints < MAX_SHOT_DATAPOINTS) {
    index = shot.datapoints++;
  } else {
    memmove(&shot.timeS[0], &shot.timeS[1],
            (MAX_SHOT_DATAPOINTS - 1U) * sizeof(float));
    memmove(&shot.weight[0], &shot.weight[1],
            (MAX_SHOT_DATAPOINTS - 1U) * sizeof(float));
    index = MAX_SHOT_DATAPOINTS - 1U;
  }
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
  maybeLatchFirstFlowFromAcceptedWeight(weight, receivedAtMs);
  return true;
}

void considerScaleFlowMarkers(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence) {
  if (!session.active || !session.startedWithScale || session.firstDropMs != 0) {
    return;
  }
  if (!session.scaleBaselineReady) {
    if (fabsf(weight) > FIRST_DROP_BASELINE_SETTLE_G) {
      return;
    }
    session.scaleBaselineG = weight;
    session.scaleBaselineReady = true;
    return;
  }
  if (session.firstFlow.phase == FirstFlowPhase::SEEKING &&
      session.firstFlow.confirmations == 0 &&
      fabsf(weight) <= FIRST_DROP_BASELINE_LOCK_G &&
      fabsf(weight) <= fabsf(session.scaleBaselineG)) {
    session.scaleBaselineG = weight;
  }

  const FirstFlowClass classified =
      stepFirstFlow(session.firstFlow, weight, receivedAtMs, packetSequence,
                    session.scaleBaselineG, runtimeConfig.minimumCupWeightG);
  if (classified == FirstFlowClass::FIRE) {
    const uint32_t detectedAtMs = session.firstFlow.candidateMs != 0
                                      ? session.firstFlow.candidateMs
                                      : receivedAtMs;
    onFirstDropsDetected(detectedAtMs);
  }
}
