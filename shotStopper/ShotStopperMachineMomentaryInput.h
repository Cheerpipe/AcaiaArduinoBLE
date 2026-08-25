#pragma once

// =============================================================================
// SPECIALIZATION: Momentary switch — GPIO input
// =============================================================================
// WHAT: Debounce and live 1:1 relay mirror when the façade allows activator
//       drive. Start/stop for brew follows momentaryStartOnPress: on press
//       (default) or on release of a hold no longer than Single-press limit.
//       A longer hold is still mirrored (machine-native rinse) when firmware
//       rinse is off. When rinse is on, a long-press from idle publishes
//       REQUEST_RINSE instead of a start/stop edge.
//
// BOUNDARY: Momentary-only. Writes shared activator sample + momentary runtime.
// Stopper/brew/cup/scale must not read these fields or know press vs release —
// they consume UserIntent from the façade. No paddle latch / PaddleMode here.

bool momentaryPhysicalOn = false;
bool momentaryRawOn = false;
uint32_t momentaryRawChangedAtMs = 0;
uint32_t momentaryPressStartedAtMs = 0;
bool momentaryPressHeld = false;
bool momentaryUserStopThisCycle = false;
bool momentarySkipFirmwareStopPulse = false;
bool momentaryStartEdgeThisCycle = false;
bool momentaryLogicalRunActive = false;
uint32_t momentaryLogicalRunStartedAtMs = 0;
uint32_t momentaryLogicalOperationalLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS;
bool momentaryElapsedLatched = false;
uint32_t momentaryLatchedElapsedMs = 0;
bool momentaryGesturePending = false;
bool momentaryGestureRestoreRunning = false;
bool momentaryRinseRequested = false;
bool momentaryRinseHoldFromIdle = false;
bool momentaryRinseEmittedThisHold = false;
MachineRunState momentaryGesturePreState = MachineRunState::CONFIRMED_OFF;
uint32_t momentaryGesturePreStartedAtMs = 0;
uint32_t momentaryGesturePreLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS;
bool pulseOutputActive = false;
bool pulseOutputIsStart = false;
uint32_t pulseOutputEndsAtMs = 0;

bool readRawActivatorOn() {
  return digitalRead(ACTIVATOR_GPIO) == ACTIVATOR_ACTIVE_LEVEL;
}

void applyMomentaryRelayDrive();
bool machineIsRunning();
void noteMomentaryLogicalStart();
void noteMomentaryLogicalStop();
void noteMomentaryLogicalStartCanceled();
void noteMomentaryDisqualifiedPress(bool restoreRunning);
MachineRunState machineRunState();
#if SHOT_STOPPER_MACHINE_TYPE == 2
void resetReedRuntime();
#endif

void clearMomentaryElapsedLatch() {
  momentaryElapsedLatched = false;
  momentaryLatchedElapsedMs = 0;
}

void latchMomentaryElapsed() {
  if (momentaryElapsedLatched || momentaryLogicalRunStartedAtMs == 0) {
    return;
  }
  momentaryLatchedElapsedMs = elapsedMs(momentaryLogicalRunStartedAtMs);
  momentaryElapsedLatched = true;
}

// Logical start/stop for brew and the reed assume window. Same edge as
// momentaryStartOnPress (press, or a short-press release). Not the 1:1 relay.
void applyMomentaryStartStopEdge() {
  if (machineIsRunning()) {
    activatorOn = false;
    activatorTurnedOff = true;
    momentaryUserStopThisCycle = true;
    momentarySkipFirmwareStopPulse = true;
    latchMomentaryElapsed();
    momentaryLogicalRunActive = false;
    noteMomentaryLogicalStop();
    return;
  }
  activatorOn = false;
  activatorTurnedOn = true;
  momentaryStartEdgeThisCycle = true;
  clearMomentaryElapsedLatch();
  noteMomentaryLogicalStart();
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = millis();
  momentaryLogicalOperationalLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS;
}

void snapshotMomentaryGesture() {
  momentaryGestureRestoreRunning = machineIsRunning();
  momentaryGesturePreState = machineRunState();
  momentaryGesturePreStartedAtMs = momentaryLogicalRunStartedAtMs;
  momentaryGesturePreLimitMs = momentaryLogicalOperationalLimitMs;
}

void revertDisqualifiedSinglePress() {
  activatorOn = false;
  momentaryStartEdgeThisCycle = false;
  if (!momentaryGestureRestoreRunning) {
    activatorTurnedOff = true;
    momentaryUserStopThisCycle = true;
    momentarySkipFirmwareStopPulse = true;
    latchMomentaryElapsed();
    momentaryLogicalRunActive = false;
    noteMomentaryDisqualifiedPress(false);
    return;
  }
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = momentaryGesturePreStartedAtMs;
  momentaryLogicalOperationalLimitMs = momentaryGesturePreLimitMs;
  noteMomentaryDisqualifiedPress(true);
}

void initializeActivatorInput() {
  pinMode(ACTIVATOR_GPIO, INPUT_PULLUP);
#if SHOT_STOPPER_MACHINE_TYPE == 2
  pinMode(REED_GPIO, INPUT_PULLUP);
  resetReedRuntime();
#endif
  momentaryRawOn = readRawActivatorOn();
  momentaryPhysicalOn = momentaryRawOn;
  momentaryRawChangedAtMs = millis();
  rawActivatorOn = momentaryRawOn;
  activatorOn = false;
  momentaryPressHeld = false;
  momentaryUserStopThisCycle = false;
  momentarySkipFirmwareStopPulse = false;
  momentaryLogicalRunActive = false;
  clearMomentaryElapsedLatch();
  momentaryGesturePending = false;
  momentaryGestureRestoreRunning = false;
  pulseOutputActive = false;
  momentaryRinseRequested = false;
  momentaryRinseHoldFromIdle = false;
  momentaryRinseEmittedThisHold = false;
}

void machineOnActivatorReady() {
  if (!momentaryPhysicalOn) {
    activatorOn = false;
    rawActivatorOn = false;
  }
}

void machineOnBrewOutcome(bool brewActive) {
  if (momentaryStartEdgeThisCycle && !brewActive) {
    activatorOn = false;
    momentaryLogicalRunActive = false;
    momentaryGesturePending = false;
    clearMomentaryElapsedLatch();
    noteMomentaryLogicalStartCanceled();
  }
}

bool activatorIsStablyOff() {
  return !activatorOn && !rawActivatorOn && !momentaryPhysicalOn &&
         elapsedMs(momentaryRawChangedAtMs) >= ACTIVATOR_DEBOUNCE_MS;
}

void updateActivatorInput() {
  activatorTurnedOn = false;
  activatorTurnedOff = false;
  momentaryUserStopThisCycle = false;
  momentaryStartEdgeThisCycle = false;
  momentaryRinseRequested = false;

  const bool sampledOn = readRawActivatorOn();
  if (sampledOn != momentaryRawOn) {
    momentaryRawOn = sampledOn;
    momentaryRawChangedAtMs = millis();
  }
  rawActivatorOn = momentaryRawOn;

  bool physicalTurnedOn = false;
  bool physicalTurnedOff = false;
  if (momentaryPhysicalOn != momentaryRawOn &&
      elapsedMs(momentaryRawChangedAtMs) >= ACTIVATOR_DEBOUNCE_MS) {
    const bool previous = momentaryPhysicalOn;
    momentaryPhysicalOn = momentaryRawOn;
    physicalTurnedOn = !previous && momentaryPhysicalOn;
    physicalTurnedOff = previous && !momentaryPhysicalOn;
  }

  applyMomentaryRelayDrive();

  const bool startOnPress = runtimeConfig.momentaryStartOnPress;

  if (physicalTurnedOn) {
    momentaryPressHeld = true;
    momentaryPressStartedAtMs = millis();
    momentaryRinseEmittedThisHold = false;
    momentaryRinseHoldFromIdle = !machineIsRunning();
    addDebugEvent(DebugCategory::ACTIVATOR, DebugCode::ACTIVATOR_ON);
    if (startOnPress) {
      snapshotMomentaryGesture();
      applyMomentaryStartStopEdge();
      momentaryGesturePending = true;
    }
  }

  const bool idleRinseHold =
      runtimeConfig.rinseEnabled && momentaryRinseHoldFromIdle;

  if (idleRinseHold && momentaryPressHeld && !momentaryRinseEmittedThisHold &&
      elapsedMs(momentaryPressStartedAtMs) >= runtimeConfig.rinseGestureMs) {
    momentaryRinseRequested = true;
    momentaryRinseEmittedThisHold = true;
    momentaryGesturePending = false;
  }

  if (startOnPress && momentaryGesturePending && momentaryPressHeld &&
      !idleRinseHold &&
      elapsedMs(momentaryPressStartedAtMs) >
          runtimeMaxSinglePressMs(runtimeConfig)) {
    revertDisqualifiedSinglePress();
    momentaryGesturePending = false;
  }

  if (physicalTurnedOff && momentaryPressHeld) {
    const uint32_t heldMs = elapsedMs(momentaryPressStartedAtMs);
    momentaryPressHeld = false;
    momentaryGesturePending = false;
    addDebugEvent(DebugCategory::ACTIVATOR, DebugCode::ACTIVATOR_OFF);
    if (startOnPress || momentaryRinseEmittedThisHold) {
      return;
    }
    // Idle rinse skip of maxSinglePress is only for the in-hold press-mode
    // revert so the gesture can demote. On release, a sub-gesture long hold
    // stays native 1:1 (same as rinse off).
    if (heldMs > runtimeMaxSinglePressMs(runtimeConfig)) {
      return;
    }
    applyMomentaryStartStopEdge();
  }
}
