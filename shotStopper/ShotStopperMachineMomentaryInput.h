#pragma once

// Momentary-switch GPIO: debounce, live 1:1 relay mirror. Start/stop for
// brew follows momentaryStartOnPress: on press (default) or on release of a
// hold no longer than Single-press limit. A longer hold is still mirrored
// (machine-native rinse) and is not a start/stop: in press mode the tentative
// edge is undone; in release mode it is never applied.

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
bool momentaryGesturePending = false;
bool momentaryGestureRestoreRunning = false;
MachineRunState momentaryGesturePreState = MachineRunState::CONFIRMED_OFF;
uint32_t momentaryGesturePreStartedAtMs = 0;
uint32_t momentaryGesturePreLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS;
bool pulseOutputActive = false;
bool pulseOutputIsStart = false;
uint32_t pulseOutputEndsAtMs = 0;

bool readRawPaddleOn() {
  return digitalRead(PADDLE_GPIO) == PADDLE_ACTIVE_LEVEL;
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

// Logical start/stop for brew and the reed assume window. Same edge as
// momentaryStartOnPress (press, or a short-press release). Not the 1:1 relay.
void applyMomentaryStartStopEdge() {
  if (machineIsRunning()) {
    paddleOn = false;
    paddleTurnedOff = true;
    momentaryUserStopThisCycle = true;
    momentarySkipFirmwareStopPulse = true;
    momentaryLogicalRunActive = false;
    noteMomentaryLogicalStop();
    return;
  }
  paddleOn = false;
  paddleTurnedOn = true;
  momentaryStartEdgeThisCycle = true;
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
  paddleOn = false;
  momentaryStartEdgeThisCycle = false;
  if (!momentaryGestureRestoreRunning) {
    paddleTurnedOff = true;
    momentaryUserStopThisCycle = true;
    momentarySkipFirmwareStopPulse = true;
    momentaryLogicalRunActive = false;
    noteMomentaryDisqualifiedPress(false);
    return;
  }
  momentaryLogicalRunActive = true;
  momentaryLogicalRunStartedAtMs = momentaryGesturePreStartedAtMs;
  momentaryLogicalOperationalLimitMs = momentaryGesturePreLimitMs;
  noteMomentaryDisqualifiedPress(true);
}

void initializePaddleInput() {
  pinMode(PADDLE_GPIO, INPUT_PULLUP);
#if SHOT_STOPPER_MACHINE_TYPE == 2
  pinMode(REED_GPIO, INPUT_PULLUP);
  resetReedRuntime();
#endif
  momentaryRawOn = readRawPaddleOn();
  momentaryPhysicalOn = momentaryRawOn;
  momentaryRawChangedAtMs = millis();
  rawPaddleOn = momentaryRawOn;
  paddleOn = false;
  momentaryPressHeld = false;
  momentaryUserStopThisCycle = false;
  momentarySkipFirmwareStopPulse = false;
  momentaryLogicalRunActive = false;
  momentaryGesturePending = false;
  momentaryGestureRestoreRunning = false;
  pulseOutputActive = false;
}

void machineReleasePhysicalSwitchToBrew() {
  if (!momentaryPhysicalOn) {
    paddleOn = false;
    rawPaddleOn = false;
  }
}

void machineReconcileBrewOutcome(bool brewActive) {
  if (momentaryStartEdgeThisCycle && !brewActive) {
    paddleOn = false;
    momentaryLogicalRunActive = false;
    momentaryGesturePending = false;
    noteMomentaryLogicalStartCanceled();
  }
}

bool paddleIsStablyOff() {
  return !paddleOn && !rawPaddleOn && !momentaryPhysicalOn &&
         elapsedMs(momentaryRawChangedAtMs) >= PADDLE_DEBOUNCE_MS;
}

void updatePaddleInput() {
  paddleTurnedOn = false;
  paddleTurnedOff = false;
  momentaryUserStopThisCycle = false;
  momentaryStartEdgeThisCycle = false;

  const bool sampledOn = readRawPaddleOn();
  if (sampledOn != momentaryRawOn) {
    momentaryRawOn = sampledOn;
    momentaryRawChangedAtMs = millis();
  }
  rawPaddleOn = momentaryRawOn;

  bool physicalTurnedOn = false;
  bool physicalTurnedOff = false;
  if (momentaryPhysicalOn != momentaryRawOn &&
      elapsedMs(momentaryRawChangedAtMs) >= PADDLE_DEBOUNCE_MS) {
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
    addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_ON);
    if (startOnPress) {
      snapshotMomentaryGesture();
      applyMomentaryStartStopEdge();
      momentaryGesturePending = true;
    }
  }

  if (startOnPress && momentaryGesturePending && momentaryPressHeld &&
      elapsedMs(momentaryPressStartedAtMs) >
          runtimeMaxSinglePressMs(runtimeConfig)) {
    revertDisqualifiedSinglePress();
    momentaryGesturePending = false;
  }

  if (physicalTurnedOff && momentaryPressHeld) {
    const uint32_t heldMs = elapsedMs(momentaryPressStartedAtMs);
    momentaryPressHeld = false;
    momentaryGesturePending = false;
    addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_OFF);
    if (startOnPress) {
      return;
    }
    if (heldMs > runtimeMaxSinglePressMs(runtimeConfig)) {
      return;
    }
    applyMomentaryStartStopEdge();
  }
}
