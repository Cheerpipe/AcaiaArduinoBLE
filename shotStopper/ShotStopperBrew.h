#pragma once

#include "ShotStopperAlert.h"
#include "ShotStopperBrewTypes.h"

// Brew-by-weight policy and extraction guards.
// Included from shotStopper.cpp after Machine and session BSS. No heap.

bool fastExtractionGuardSession();
bool slowExtractionGuardSession();
float effectiveStopThreshold();
float effectiveMaxStopThreshold();
float effectiveMinStopThreshold();
float activeWeightCutTargetG() {
  if (session.extractionExtended && fastExtractionGuardSession()) {
    return effectiveMaxStopThreshold();
  }
  if (session.slowExtractionExtended && slowExtractionGuardSession()) {
    return effectiveMinStopThreshold();
  }
  return effectiveStopThreshold();
}
bool shouldTrackWeight() {
  return session.active && !session.config.timerOnly &&
         session.weightControlState != WeightControlState::INACTIVE &&
         session.weightControlState != WeightControlState::FAULT_STOPPED &&
         stopperState == StopperState::BREW;
}

float effectiveStopThreshold() {
  return static_cast<float>(session.config.goalWeightG) -
         session.config.weightOffsetG;
}

float effectiveMaxStopThreshold() {
  return session.config.maxRecoveryWeightG - session.config.weightOffsetG;
}

float effectiveMinStopThreshold() {
  return session.config.minRecoveryWeightG - session.config.weightOffsetG;
}

bool fastExtractionGuardSession() {
  return session.active && session.config.fastExtractionGuardEnabled &&
         !session.config.timerOnly && session.startedWithScale;
}

bool slowExtractionGuardSession() {
  return session.active && session.config.slowExtractionGuardEnabled &&
         !session.config.timerOnly && session.startedWithScale;
}

bool minBrewTimeReached() {
  uint32_t elapsedMsValue = 0U;
  return machineRunningElapsed(elapsedMsValue) &&
         elapsedMsValue >= session.config.minBrewTimeMs;
}

bool maxBrewTimeReached() {
  uint32_t elapsedMsValue = 0U;
  return machineRunningElapsed(elapsedMsValue) &&
         elapsedMsValue >= session.config.maxBrewTimeMs;
}

bool targetWeightReached(float weight) {
  return weight >= effectiveStopThreshold();
}

bool minRecoveryWeightReached(float weight) {
  return weight >= effectiveMinStopThreshold();
}

void requestScaleBrewBeep(uint32_t cycleId);
void enterBrewOrManualFromStart();
void demoteActiveCycleToRinseOrEnd();
void calculateExpectedEndTime(float cutTargetG);

void resetFirstFlowDetector() {
  resetFirstFlowState(session.firstFlow);
  session.firstFlowAcceptedConfirmations = 0;
}

void markTareZeroReady() {
  session.scaleBaselineG = 0.0f;
  session.scaleBaselineReady = true;
  resetFirstFlowDetector();
}

void recordFirstDropTimestamp(uint32_t receivedAtMs) {
  if (session.firstDropMs == 0) {
    session.firstDropMs = receivedAtMs;
  }
}

void requestFirstDropBeep() {
  if (!session.firstDropsBeepSent && session.config.firstDropBeep) {
    emitAlert(AlertEvent::FIRST_DROP, session.id);
    session.firstDropsBeepSent = true;
  }
}
bool retareWindowOpen();

void notifyRetareFlowDetected(uint32_t receivedAtMs) {
  if (!retareWindowOpen()) {
    return;
  }
  if (session.retareFlowFirstDetectedAtMs == 0) {
    session.retareFlowFirstDetectedAtMs = receivedAtMs;
    addDebugEvent(DebugCategory::SCALE, DebugCode::FIRST_DROP_DURING_RETARE,
                  static_cast<int32_t>(session.id),
                  static_cast<int32_t>(elapsedMs(session.startedAtMs)));
  }
  session.flowDuringRetare = true;
}

void resetDirectStopConfirmation() {
  session.thresholdConfirmations = 0;
  session.lastThresholdAtMs = 0;
  session.lastThresholdPacketSequence = 0;
  session.lastThresholdConnectionGeneration = 0;
  session.directStopPending = false;
}

bool accidentalTouchSessionActive() {
  return shouldTrackWeight() && session.config.avoidAccidentalTouchEnabled &&
         session.startedWithScale &&
         session.weightControlState == WeightControlState::ACTIVE;
}

void noteAccidentalTouchClass(AccidentalTouchClass classified, float weight) {
  const bool wasHolding = session.accidentalTouchHolding;
  session.accidentalTouchClass = classified;
  if (classified == AccidentalTouchClass::TOUCH) {
    session.accidentalTouchHolding = true;
    if (session.accidentalTouchPendingCount <
        ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES) {
      session.accidentalTouchPendingG[session.accidentalTouchPendingCount++] =
          weight;
    } else {
      memmove(&session.accidentalTouchPendingG[0],
              &session.accidentalTouchPendingG[1],
              (ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES - 1U) * sizeof(float));
      session.accidentalTouchPendingG[ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES - 1U] =
          weight;
    }
    if (!wasHolding) {
      addDebugEvent(DebugCategory::SCALE, DebugCode::ACCIDENTAL_TOUCH_HOLD,
                    weightToCentigrams(weight));
    }
    return;
  }

  session.accidentalTouchHolding = false;
  session.accidentalTouchPendingCount = 0;
  if (classified == AccidentalTouchClass::SUSTAINED) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::ACCIDENTAL_TOUCH_SUSTAINED,
                  weightToCentigrams(weight));
  } else if (wasHolding) {
    addDebugEvent(DebugCategory::SCALE, DebugCode::ACCIDENTAL_TOUCH_RELEASE,
                  weightToCentigrams(weight));
  }
}

void maybeHandoffAccidentalTouchPhase() {
  if (session.accidentalTouchPhase != AccidentalTouchPhase::STARTUP) {
    return;
  }
  if (!accidentalTouchTrendReady(shot.timeS, shot.weight, shot.datapoints)) {
    return;
  }
  session.accidentalTouchPhase = AccidentalTouchPhase::TREND;
  addDebugEvent(DebugCategory::SCALE, DebugCode::ACCIDENTAL_TOUCH_HANDOFF,
                static_cast<int32_t>(shot.datapoints));
}

AccidentalTouchClass evaluateAccidentalTouchSample(float weight,
                                                   uint32_t receivedAtMs) {
  maybeHandoffAccidentalTouchPhase();
  const float nowS =
      static_cast<int32_t>(receivedAtMs - shot.startMs) / 1000.0f;
  const float lastS =
      static_cast<int32_t>(session.lastAcceptedWeightAtMs - shot.startMs) /
      1000.0f;
  const AccidentalTouchClass classified = classifyAccidentalTouch(
      session.accidentalTouchPhase, shot.timeS, shot.weight, shot.datapoints,
      weight, nowS, session.hasWeightAnchor, session.lastAcceptedWeightG, lastS,
      session.accidentalTouchPendingG, session.accidentalTouchPendingCount);
  noteAccidentalTouchClass(classified, weight);
  return classified;
}

bool bbwAutomaticScaleSession() {
  return session.active && session.bbwProtectionEnabled;
}

bool retareWindowOpen() {
  if (!bbwAutomaticScaleSession() || !session.config.autoRetare) {
    return false;
  }
  if (session.retareEnded || session.retarePerformed) {
    return false;
  }
  return elapsedMs(session.startedAtMs) < session.config.retareWindowMs;
}

bool bbwProtectionActive() {
  if (!bbwAutomaticScaleSession()) {
    return false;
  }
  return !session.bbwProtectionEnded;
}

void markRetareEnded(uint32_t endedAtMs) {
  (void)endedAtMs;
  if (session.retareEnded) {
    return;
  }
  session.retareEnded = true;
  session.retareDisabled = true;
}

bool bbwWeightStopInhibited() {
  if (!bbwAutomaticScaleSession()) {
    return false;
  }
  return bbwProtectionActive();
}

bool withinRinseGestureWindow() {
  return elapsedMs(session.startedAtMs) <= session.config.rinseGestureMs;
}

void onFirstDropsDetected(uint32_t receivedAtMs) {
  recordFirstDropTimestamp(receivedAtMs);
  if (session.hasWeightAnchor && isfinite(session.lastAcceptedWeightG)) {
    shotCurveSampler.latchFirstDrop(receivedAtMs, session.lastAcceptedWeightG);
  }
  requestFirstDropBeep();
  notifyRetareFlowDetected(receivedAtMs);
}

void performAutomaticRetare() {
  if (session.retarePerformed || session.retareDisabled || !retareWindowOpen()) {
    return;
  }
  if (!requestRemoteRetare()) {
    return;
  }
  session.retarePerformed = true;
  emitImmediateCommandAlertIfBuzzer(AlertEvent::TARE);
  markRetareEnded(millis());
}
void initializeBbwProtection() {
  session.bbwProtectionEnabled = false;
  session.retareEnded = false;
  session.bbwProtectionEnded = false;
  session.flowDuringRetare = false;
  session.retareFlowFirstDetectedAtMs = 0;
  session.retarePerformed = false;
  session.retareDisabled = false;
  session.firstDropsBeepSent = false;
  resetFirstFlowDetector();

  if (!session.startedWithScale || session.config.timerOnly ||
      !session.automaticEnabled) {
    session.retareEnded = true;
    session.bbwProtectionEnded = true;
    return;
  }
  session.bbwProtectionEnabled = true;
  if (!session.config.autoRetare) {
    session.retareEnded = true;
  }
}

void serviceBbwProtectionPhases() {
  if (!session.active || !session.bbwProtectionEnabled) {
    return;
  }
  const uint32_t nowMs = millis();
  if (!session.retareEnded && session.config.autoRetare &&
      !session.retarePerformed &&
      elapsedMs(session.startedAtMs) >= session.config.retareWindowMs) {
    markRetareEnded(nowMs);
  }
  if (!session.bbwProtectionEnded &&
      elapsedMs(session.startedAtMs) >= session.config.bbwProtectionMs) {
    session.bbwProtectionEnded = true;
    resetDirectStopConfirmation();
  }
}
void enterFastExtractionExtended(float weightG, uint32_t atMs) {
  if (session.extractionExtended) {
    return;
  }
  session.extractionExtended = true;
  session.targetReachedEarly = true;
  session.targetReachedAtMs = atMs;
  shotCurveSampler.latchExtended(atMs, weightG);
  addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_ENTERED,
                static_cast<int32_t>(weightG * 100.0f),
                static_cast<int32_t>(elapsedMs(session.startedAtMs)));
  if (buzzerPatternForExtendedPulseRate(
          runtimeConfig.buzzerExtendedPulseRate) != BuzzerPattern::NONE) {
    emitAlert(AlertEvent::EXTENDED_PULSE, session.id);
  }
  calculateExpectedEndTime(activeWeightCutTargetG());
}

void enterSlowExtractionExtended(float weightG, uint32_t atMs) {
  if (session.slowExtractionExtended || session.extractionExtended) {
    return;
  }
  session.slowExtractionExtended = true;
  shotCurveSampler.latchExtended(atMs, weightG);
  addDebugEvent(DebugCategory::SCALE, DebugCode::SLOW_EXTRACTION_ENTERED,
                static_cast<int32_t>(weightG * 100.0f),
                static_cast<int32_t>(elapsedMs(session.startedAtMs)));
  if (buzzerPatternForExtendedPulseRate(
          runtimeConfig.buzzerSlowExtendedPulseRate) != BuzzerPattern::NONE) {
    emitAlert(AlertEvent::EXTENDED_PULSE, session.id);
  }
  calculateExpectedEndTime(activeWeightCutTargetG());
}

void considerDirectStopSample(float weight, uint32_t receivedAtMs,
                              uint32_t packetSequence,
                              uint32_t connectionGeneration) {
  if (!shouldTrackWeight() || packetSequence == 0 || !isfinite(weight) ||
      bbwWeightStopInhibited()) {
    return;
  }

  const bool overload = fabsf(weight) > MAX_AUTOMATION_WEIGHT_G;
  const bool active =
      session.weightControlState == WeightControlState::ACTIVE;
  const bool validating =
      session.weightControlState == WeightControlState::VALIDATING;
  if (!overload && !active) {
    return;
  }
  if (overload && !active && !validating) {
    return;
  }
  const bool overMax =
      fastExtractionGuardSession() && session.extractionExtended &&
      weight >= effectiveMaxStopThreshold();
  const bool overMin =
      slowExtractionGuardSession() && session.slowExtractionExtended &&
      weight >= effectiveMinStopThreshold();
  const bool overThreshold = weight >= effectiveStopThreshold();
  if (!overThreshold && !overload && !overMax && !overMin) {
    resetDirectStopConfirmation();
    return;
  }

  const bool consecutive = session.thresholdConfirmations > 0 &&
      connectionGeneration == session.lastThresholdConnectionGeneration &&
      packetSequence == session.lastThresholdPacketSequence + 1U &&
      static_cast<int32_t>(receivedAtMs - session.lastThresholdAtMs) >= 0 &&
      static_cast<uint32_t>(receivedAtMs - session.lastThresholdAtMs) <=
          DIRECT_STOP_CONFIRMATION_WINDOW_MS;
  session.thresholdConfirmations = consecutive
      ? static_cast<uint8_t>(session.thresholdConfirmations + 1U)
      : 1U;
  session.lastThresholdAtMs = receivedAtMs;
  session.lastThresholdPacketSequence = packetSequence;
  session.lastThresholdConnectionGeneration = connectionGeneration;

  if (session.thresholdConfirmations < DIRECT_STOP_CONFIRMATION_SAMPLES) {
    return;
  }

  if (overload) {
    session.directStopPending = true;
    session.directStopReason = EndReason::WEIGHT_ANOMALY;
    weightStreamState = WeightStreamState::OVERLOAD;
    session.calibrationEligible = false;
    setWeightControlState(WeightControlState::FAULT_STOPPED);
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_OVERLOAD_CONFIRMED,
                  static_cast<int32_t>(weight));
    return;
  }

  if (overMax) {
    session.directStopPending = true;
    session.directStopReason = EndReason::FAST_EXTRACTION_MAX_WEIGHT;
    addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_STOP_MAX,
                  static_cast<int32_t>(weight * 100.0f));
    return;
  }

  if (overMin) {
    session.directStopPending = true;
    session.directStopReason = EndReason::SLOW_EXTRACTION_MIN_WEIGHT;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SLOW_EXTRACTION_STOP_MIN_WEIGHT,
                  static_cast<int32_t>(weight * 100.0f));
    return;
  }

  if (overThreshold && fastExtractionGuardSession() &&
      (!minBrewTimeReached() || session.extractionExtended)) {
    enterFastExtractionExtended(weight, receivedAtMs);
    resetDirectStopConfirmation();
    return;
  }

  session.directStopPending = true;
  session.directStopReason = EndReason::SCALE_THRESHOLD;
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_THRESHOLD_CONFIRMED,
                static_cast<int32_t>(weight * 100.0f));
}
void armNoScaleShotGuard() {
  if (noScaleShotGuardArmed) {
    return;
  }
  noScaleShotGuardArmed = true;
  addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_ARMED);
}

void consumeNoScaleShotGuard() {
  noScaleShotGuardArmed = false;
  noScaleShotGuardActivityAtMs = millis();
  addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED);
}

bool noScaleShotGuardWouldBlock(const GuardInputs &inputs) {
  const RuntimeConfig effective = effectiveRuntimeConfig();
  return runtimeConfig.avoidBbwShotWithoutScale && !effective.timerOnly &&
         !inputs.scaleUsable && noScaleShotGuardArmed;
}

void maybeEmitManualNoScaleBeep(const GuardInputs &inputs) {
  if (!runtimeConfig.buzzerManualNoScaleBeep) {
    return;
  }
  const RuntimeConfig effective = effectiveRuntimeConfig();
  if (effective.timerOnly || inputs.scaleUsable) {
    return;
  }
  emitAlert(AlertEvent::MANUAL_NO_SCALE);
}

void blockNoScaleShotGuard() {
  noScaleShotGuardHold = false;
  consumeNoScaleShotGuard();
  addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED);
}

void serviceNoScaleShotGuard(const GuardInputs &inputs) {
  if (inputs.scaleAvailable && !noScaleShotGuardScaleWasAvailable) {
    armNoScaleShotGuard();
  }
  noScaleShotGuardScaleWasAvailable = inputs.scaleAvailable;
  if (noScaleShotGuardHold) {
    if (!noScaleShotGuardWouldBlock(inputs)) {
      noScaleShotGuardHold = false;
    } else if (inputs.holdActive &&
               elapsedMs(noScaleShotGuardHoldAtMs) >
                   runtimeConfig.rinseGestureMs) {
      blockNoScaleShotGuard();
    }
  }
  if (!noScaleShotGuardArmed && !session.active &&
      noScaleShotGuardActivityAtMs != 0 &&
      elapsedMs(noScaleShotGuardActivityAtMs) >=
          runtimeConfig.lastShotCooldownMs) {
    armNoScaleShotGuard();
  }
}

bool cupStartGuardWouldBlock(const GuardInputs &inputs) {
  const RuntimeConfig effective = effectiveRuntimeConfig();
  return effective.cupProtectionEnabled && effective.requireCupToStart &&
         !effective.timerOnly && inputs.scaleUsable &&
         inputs.cup != CupPresenceState::PRESENT;
}

void serviceCupStartGuard(const GuardInputs &inputs) {
  if (!cupStartGuardHold) {
    return;
  }
  const RuntimeConfig effective = effectiveRuntimeConfig();
  if (!effective.cupProtectionEnabled || !effective.requireCupToStart ||
      effective.timerOnly) {
    cupStartGuardHold = false;
    return;
  }
  if (inputs.scaleUsable && inputs.cup == CupPresenceState::PRESENT) {
    cupStartGuardHold = false;
    return;
  }
  if (inputs.holdActive &&
      elapsedMs(cupStartGuardHoldAtMs) > runtimeConfig.rinseGestureMs) {
    cupStartGuardHold = false;
    addDebugEvent(DebugCategory::STATE, DebugCode::CUP_START_GUARD_BLOCKED,
                  weightToCentigrams(inputs.currentWeightG));
    emitAlert(AlertEvent::CUP_START_BLOCKED);
  }
}
void armAutoToManualGuardForAutomaticBrew() {
  if (!session.config.autoToManualGuardEnabled ||
      session.config.timerOnly || !session.startedWithScale ||
      session.weightControlState == WeightControlState::INACTIVE) {
    return;
  }
  const uint32_t limitMs = autoToManualGuardLimitMs(
      true,
      static_cast<AutoToManualGuardLimitMode>(
          session.config.autoToManualGuardLimitMode),
      session.config.autoToManualGuardManualLimitMs,
      session.config.autoToManualGuardSamplesDs,
      session.config.operationalWallMs);
  session.autoToManualGuardArmed = true;
  session.autoToManualGuardDeadlineAtMs = session.startedAtMs + limitMs;
  addDebugEvent(DebugCategory::SCALE, DebugCode::AUTO_TO_MANUAL_GUARD_ARMED,
                static_cast<int32_t>(limitMs));
  if (session.weightControlState == WeightControlState::SUSPENDED &&
      !session.autoToManualGuardEnforced) {
    session.autoToManualGuardEnforced = true;
    latchAtmCurveFromSession(millis());
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::AUTO_TO_MANUAL_GUARD_ENFORCED,
                  static_cast<int32_t>(elapsedMs(session.startedAtMs)),
                  static_cast<int32_t>(limitMs));
  }
}

bool autoToManualGuardDeadlineDue() {
  if (!session.active || !session.autoToManualGuardEnforced) {
    return false;
  }
  return static_cast<int32_t>(millis() -
                              session.autoToManualGuardDeadlineAtMs) >= 0;
}
bool automaticScaleStopDue() {
  if (session.config.timerOnly || stopperState != StopperState::BREW ||
      bbwWeightStopInhibited()) {
    return false;
  }

  const bool holding = session.accidentalTouchHolding;
  const bool directStopFresh = session.directStopPending &&
      session.thresholdConfirmations >= DIRECT_STOP_CONFIRMATION_SAMPLES &&
      static_cast<int32_t>(millis() - session.lastThresholdAtMs) >= 0 &&
      elapsedMs(session.lastThresholdAtMs) <= MAX_AUTOMATION_WEIGHT_AGE_MS;
  const bool directStopHonored =
      directStopFresh &&
      (session.directStopReason == EndReason::WEIGHT_ANOMALY ||
       session.weightControlState == WeightControlState::ACTIVE);
  if (!holding && directStopHonored) {
    return true;
  }

  if (session.extractionExtended && fastExtractionGuardSession()) {
    if (minBrewTimeReached() &&
        targetWeightReached(session.lastAcceptedWeightG)) {
      session.directStopPending = true;
      session.directStopReason = EndReason::FAST_EXTRACTION_MIN_TIME;
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::FAST_EXTRACTION_STOP_MIN_TIME,
                    static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
      return true;
    }
  }

  if (slowExtractionGuardSession() && !session.extractionExtended &&
      !session.slowExtractionExtended && maxBrewTimeReached() &&
      !targetWeightReached(session.lastAcceptedWeightG)) {
    if (minRecoveryWeightReached(session.lastAcceptedWeightG)) {
      session.directStopPending = true;
      session.directStopReason = EndReason::SLOW_EXTRACTION_MAX_TIME;
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::SLOW_EXTRACTION_STOP_MAX_TIME,
                    static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
      return true;
    }
    enterSlowExtractionExtended(session.lastAcceptedWeightG, millis());
    return false;
  }

  if (holding) {
    return false;
  }

  if (!session.receivedFreshWeightInCycle ||
      session.weightControlState != WeightControlState::ACTIVE ||
      currentWeightSequence == session.weightSequenceAtStart ||
      scaleAutomationUnavailableForSession()) {
    return false;
  }
  const float elapsedS = cycleElapsedSeconds();
  if (elapsedS < shot.expectedEndS) {
    return false;
  }

  if (fastExtractionGuardSession() && !minBrewTimeReached() &&
      !session.extractionExtended) {
    enterFastExtractionExtended(session.lastAcceptedWeightG, millis());
    return false;
  }

  if (session.extractionExtended && fastExtractionGuardSession()) {
    session.directStopPending = true;
    session.directStopReason = EndReason::FAST_EXTRACTION_MAX_WEIGHT;
    addDebugEvent(DebugCategory::SCALE, DebugCode::FAST_EXTRACTION_STOP_MAX,
                  static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f));
    return true;
  }

  if (session.slowExtractionExtended && slowExtractionGuardSession()) {
    session.directStopPending = true;
    session.directStopReason = EndReason::SLOW_EXTRACTION_MIN_WEIGHT;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SLOW_EXTRACTION_STOP_MIN_WEIGHT,
                  static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f));
    return true;
  }

  session.directStopPending = true;
  session.directStopReason = EndReason::SCALE_THRESHOLD;
  addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_THRESHOLD_CONFIRMED,
                static_cast<int32_t>(session.lastAcceptedWeightG * 100.0f));
  return true;
}
