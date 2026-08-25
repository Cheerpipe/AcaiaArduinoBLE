#pragma once

// =============================================================================
// SPECIALIZATION: Paddle / latch-switch — policy (intent translator)
// =============================================================================
// WHAT: GPIO edges + snapshotted PaddleMode → UserIntent / cycle policy.
//       Included from ShotStopperMachine.h when SHOT_STOPPER_MACHINE_TYPE == 0.
//
// BOUNDARY: Paddle-only. Owns Natural/Original/Auto feel. Stopper/brew never
// read paddleMode or GPIO; they consume last MachineIntention. Do not put
// momentary press/release or reed logic here. If brew/guards need a paddle
// quirk, encode it as generic intent/flags on the façade — never leak
// PaddleMode into brew.

bool paddleReturnReminderActive = false;
uint32_t paddleReturnReminderLastAtMs = 0;
uint32_t paddleReturnReminderStartedAtMs = 0;

bool machineCycleActive = false;
uint8_t machineCyclePaddleMode = static_cast<uint8_t>(PaddleMode::NATURAL);
bool machineCycleAutomaticBbw = false;
bool machineCyclePromotedToNatural = false;
bool machineCycleHardMaxArmed = false;
bool machineCycleSawRelease = false;
uint32_t machineCycleStartedAtMs = 0;
uint32_t machineCycleRinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
uint32_t paddleHoldStartedAtMs = 0;

void machineEndCycle() {
  machineCycleActive = false;
  machineCycleAutomaticBbw = false;
  machineCyclePromotedToNatural = false;
  machineCycleHardMaxArmed = false;
  machineCycleSawRelease = false;
  rinseActuationActive = false;
}

void machineBeginCycle(bool automaticBbw) {
  machineCycleActive = true;
  machineCyclePaddleMode = runtimeConfig.paddleMode;
  machineCycleAutomaticBbw = automaticBbw;
  machineCyclePromotedToNatural = false;
  machineCycleSawRelease = false;
  machineCycleHardMaxArmed =
      automaticBbw &&
      machineCyclePaddleMode == static_cast<uint8_t>(PaddleMode::ORIGINAL);
  machineCycleStartedAtMs = millis();
  machineCycleRinseGestureMs = runtimeConfig.rinseGestureMs;
}

bool machineHidesPhysicalStop() {
  if (!machineCycleActive || !machineCycleAutomaticBbw) {
    return false;
  }
  if (machineCyclePaddleMode == static_cast<uint8_t>(PaddleMode::ORIGINAL)) {
    return !machineCyclePromotedToNatural;
  }
  return machineCyclePaddleMode == static_cast<uint8_t>(PaddleMode::AUTO);
}

bool machineAllowsAutomationStop() {
  return !(machineHidesPhysicalStop() &&
           machineCyclePaddleMode ==
               static_cast<uint8_t>(PaddleMode::ORIGINAL) &&
           (activatorOn || rawActivatorOn));
}

uint32_t machineCloseLimitMs(uint32_t operationalWallMs) {
  return machineCycleHardMaxArmed ? HARD_MAX_CIRCUIT_CLOSED_MS
                                  : operationalWallMs;
}

bool machineCycleHardMaxArmedForTest() { return machineCycleHardMaxArmed; }

bool machineCyclePromotedToNaturalForTest() {
  return machineCyclePromotedToNatural;
}

uint32_t machineLastActivatorEdgeMs() { return rawActivatorChangedAtMs; }

// Early paddle OFF still reports stop (or rinse) so Original/Auto walk-away
// does not hide the gesture. After that window, Original/Auto may hide stop.
bool machineReportsStopOnRelease() {
  return !machineHidesPhysicalStop() ||
         (machineCycleActive &&
          elapsedMs(machineCycleStartedAtMs) <= machineCycleRinseGestureMs);
}

bool paddleHoldIsRinseGesture() {
  if (!runtimeConfig.rinseEnabled) {
    return false;
  }
  // Active cycle: demote only while still inside the cycle-start window.
  // A later short ON→OFF (Original promote, Auto walk-away) is stop, not rinse.
  if (machineCycleActive) {
    return elapsedMs(machineCycleStartedAtMs) <= machineCycleRinseGestureMs;
  }
  return paddleHoldStartedAtMs != 0 &&
         elapsedMs(paddleHoldStartedAtMs) <= runtimeConfig.rinseGestureMs;
}

MachineIntention machinePollIntention() {
  if (activatorTurnedOn) {
    paddleHoldStartedAtMs = millis();
  }
  if (machineCycleActive && activatorTurnedOff) {
    machineCycleSawRelease = true;
  }
  if (machineHidesPhysicalStop() && activatorTurnedOn && machineCycleSawRelease &&
      machineCyclePaddleMode == static_cast<uint8_t>(PaddleMode::ORIGINAL)) {
    machineCyclePromotedToNatural = true;
  }

  MachineIntention out;
  out.turnedOn = activatorTurnedOn;
  out.turnedOff = activatorTurnedOff;
  out.holdActive = activatorOn || rawActivatorOn;
  out.stablyOff = activatorIsStablyOff();

  const bool swallowStop = activatorTurnedOff && !machineReportsStopOnRelease();
  if (activatorTurnedOn && !machineCycleActive) {
    out.intent = UserIntent::REQUEST_START;
  } else if (activatorTurnedOn && machineCycleActive) {
    out.intent = UserIntent::HOLD_ACTIVE;
  } else if (activatorTurnedOff && paddleHoldIsRinseGesture()) {
    out.intent = UserIntent::REQUEST_RINSE;
  } else if (activatorTurnedOff && !swallowStop) {
    out.intent = UserIntent::REQUEST_STOP;
  } else if (out.holdActive) {
    out.intent = UserIntent::HOLD_ACTIVE;
  } else if (out.stablyOff) {
    out.intent = UserIntent::STABLE_IDLE;
  }
  return machineCaptureIntention(out);
}

void machineServiceReminders() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  // Read the GPIO here rather than a debounced state: this reminder describes
  // the physical paddle circuit as it is wired at this instant.
  const bool paddleOnCircuitOff = readRawActivatorOn() && !relay.closed;
  const AlertOutputChannel channel = currentAlertOutputChannel();
  const bool localBuzzerUsable =
      BUZZER_SUPPORT_ENABLED && localBuzzer.ready;
  const bool scaleUsable = scaleAvailable();
  bool outputUsable = false;
  switch (channel) {
    case AlertOutputChannel::SCALE_ONLY:
      outputUsable = scaleUsable;
      break;
    case AlertOutputChannel::BUZZER_ONLY:
      outputUsable = localBuzzerUsable;
      break;
    case AlertOutputChannel::SCALE_PRIORITY:
      outputUsable = scaleUsable || localBuzzerUsable;
      break;
  }
  const bool shouldRemind =
      soundAlertsEnabled() && runtimeConfig.paddleReturnReminderBeep &&
      paddleOnCircuitOff && outputUsable;
  if (!shouldRemind) {
    paddleReturnReminderActive = false;
    paddleReturnReminderLastAtMs = 0;
    paddleReturnReminderStartedAtMs = 0;
    cancelScalePaddleReturnReminderBeep();
    return;
  }

  const uint32_t now = millis();
  if (!paddleReturnReminderActive) {
    paddleReturnReminderActive = true;
    paddleReturnReminderLastAtMs = now;
    paddleReturnReminderStartedAtMs = now;
    return;
  }
  if (elapsedMs(paddleReturnReminderStartedAtMs) >=
      runtimeConfig.paddleReturnReminderMaxDurationMs) {
    paddleReturnReminderActive = false;
    paddleReturnReminderLastAtMs = 0;
    paddleReturnReminderStartedAtMs = 0;
    cancelScalePaddleReturnReminderBeep();
    return;
  }
  if (elapsedMs(paddleReturnReminderLastAtMs) >=
      runtimeConfig.paddleReturnReminderIntervalMs) {
    if (emitAlert(AlertEvent::PADDLE_REMINDER)) {
      paddleReturnReminderLastAtMs = now;
    }
  }
}

inline void machineApplyWorkflowConfig(RuntimeConfig &dst,
                                       const RuntimeConfig &src) {
  dst.paddleMode = src.paddleMode;
  dst.paddleReturnReminderBeep = src.paddleReturnReminderBeep;
  dst.paddleReturnReminderIntervalMs = src.paddleReturnReminderIntervalMs;
  dst.paddleReturnReminderMaxDurationMs = src.paddleReturnReminderMaxDurationMs;
  dst.rinseGestureMs = src.rinseGestureMs;
}
