#pragma once

#include "ShotStopperScaleTypes.h"
#include "ShotStopperShotLogTypes.h"

// Scale sense: trajectory, first-drop / stable-cup / cup-removed detectors.
// Predicts against a brew-injected target. No GPIO. No heap; trajectory is BSS.

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

void considerRetareCupCandidate(float weight, uint32_t receivedAtMs) {
  if (!retareWindowOpen() || session.retareDisabled || session.retarePerformed) {
    return;
  }
  if (weight < session.config.minimumCupWeightG) {
    resetRetareStabilityStreak();
    return;
  }

  if (session.retareStabilitySamples == 0) {
    session.retareCandidateWeightG = weight;
    session.retareStabilitySamples = 1;
    session.retareStabilityStartedAtMs = receivedAtMs;
    session.retareLastSampleAtMs = receivedAtMs;
    return;
  }

  if (static_cast<uint32_t>(receivedAtMs - session.retareLastSampleAtMs) >
          session.config.retareStabilityMaxGapMs ||
      fabsf(weight - session.retareCandidateWeightG) >
          session.config.retareStabilityToleranceG) {
    session.retareCandidateWeightG = weight;
    session.retareStabilitySamples = 1;
    session.retareStabilityStartedAtMs = receivedAtMs;
    session.retareLastSampleAtMs = receivedAtMs;
    return;
  }

  session.retareCandidateWeightG = weight;
  session.retareLastSampleAtMs = receivedAtMs;
  if (session.retareStabilitySamples < UINT8_MAX) {
    ++session.retareStabilitySamples;
  }
  const uint32_t stableDurationMs =
      static_cast<uint32_t>(receivedAtMs - session.retareStabilityStartedAtMs);
  const bool samplesMet =
      session.retareStabilitySamples >= session.config.retareStabilitySamples;
  const bool durationMet =
      session.config.retareStabilityMinDurationMs == 0U ||
      stableDurationMs >= session.config.retareStabilityMinDurationMs;
  if (samplesMet && durationMet &&
      session.retareCandidateWeightG >= session.config.minimumCupWeightG) {
    performAutomaticRetare();
  }
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
  addDebugEvent(DebugCategory::SCALE,
                DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT,
                static_cast<int32_t>(session.id),
                static_cast<int32_t>(session.config.postTareBaselineGraceMs));
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
  return true;
}

void considerScaleFlowMarkers(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence);

void considerCupRemovedSample(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence) {
  if (!session.active || stopperState != StopperState::BREW ||
      session.config.timerOnly || !session.config.cupProtectionEnabled ||
      !session.config.stopIfCupRemoved || !session.startedWithScale) {
    return;
  }
  if (session.awaitingPostTareBaseline) {
    if (isfinite(weight) && weight > session.config.cupRemovedWeightG) {
      session.cupRemovedArmed = true;
    }
    session.cupRemovedConfirmations = 0;
    session.lastCupRemovedAtMs = 0;
    session.lastCupRemovedPacketSequence = 0;
    return;
  }
  if (!isfinite(weight) || weight > session.config.cupRemovedWeightG) {
    session.cupRemovedArmed = true;
    session.cupRemovedConfirmations = 0;
    session.lastCupRemovedAtMs = 0;
    session.lastCupRemovedPacketSequence = 0;
    return;
  }
  if (!session.cupRemovedArmed) {
    return;
  }

  const bool consecutive = session.cupRemovedConfirmations > 0 &&
      static_cast<int32_t>(receivedAtMs - session.lastCupRemovedAtMs) >= 0 &&
      static_cast<uint32_t>(receivedAtMs - session.lastCupRemovedAtMs) <=
          DIRECT_STOP_CONFIRMATION_WINDOW_MS &&
      (packetSequence == 0 || session.lastCupRemovedPacketSequence == 0 ||
       packetSequence == session.lastCupRemovedPacketSequence + 1U);
  session.cupRemovedConfirmations = consecutive
      ? static_cast<uint8_t>(session.cupRemovedConfirmations + 1U)
      : 1U;
  session.lastCupRemovedAtMs = receivedAtMs;
  session.lastCupRemovedPacketSequence = packetSequence;

  if (session.cupRemovedConfirmations < DIRECT_STOP_CONFIRMATION_SAMPLES) {
    return;
  }
  session.cupRemovedPending = true;
  addDebugEvent(DebugCategory::SCALE, DebugCode::CUP_REMOVED_CONFIRMED,
                weightToCentigrams(weight),
                static_cast<int32_t>(elapsedMs(session.startedAtMs)));
}

void considerScaleFlowMarkers(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence) {
  if (!session.active || !session.startedWithScale || session.firstDropMs != 0) {
    return;
  }
  if (session.awaitingPostTareBaseline) {
    if (fabsf(weight) <= POST_TARE_BASELINE_MAX_ABS_G) {
      session.scaleBaselineG = weight;
      session.scaleBaselineReady = true;
    }
    return;
  }
  if (!session.scaleBaselineReady) {
    if (fabsf(weight) > POST_TARE_BASELINE_MAX_ABS_G) {
      return;
    }
    session.scaleBaselineG = weight;
    session.scaleBaselineReady = true;
    return;
  }
  const float deltaFromBaseline = weight - session.scaleBaselineG;
  if (deltaFromBaseline < FIRST_DROP_THRESHOLD_G) {
    session.firstDropConfirmations = 0;
    return;
  }
  const float firstDropMaxWeightG =
      static_cast<float>(session.config.goalWeightG) * 0.5f;
  if (weight >= firstDropMaxWeightG) {
    session.firstDropConfirmations = 0;
    return;
  }

  const bool consecutive =
      session.firstDropConfirmations > 0 &&
      packetSequence == session.firstDropLastPacketSequence + 1U &&
      static_cast<int32_t>(receivedAtMs - session.firstDropLastAtMs) >= 0 &&
      static_cast<uint32_t>(receivedAtMs - session.firstDropLastAtMs) <=
          DIRECT_STOP_CONFIRMATION_WINDOW_MS;
  session.firstDropConfirmations =
      consecutive ? static_cast<uint8_t>(session.firstDropConfirmations + 1U)
                  : 1U;
  session.firstDropLastAtMs = receivedAtMs;
  session.firstDropLastPacketSequence = packetSequence;

  if (session.firstDropConfirmations >= FIRST_DROP_CONFIRMATION_SAMPLES) {
    onFirstDropsDetected(receivedAtMs);
  }
}
