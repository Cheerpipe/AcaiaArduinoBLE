#pragma once

// Momentary-switch GPIO: debounce, short/long classification, synthetic latch.
// Recovery copies physical edges into paddleOn / paddleTurnedOn / Off.
// The main loop classifies on release by default, or on press when
// runtimeConfig.momentaryStartOnPress is set. Long-press still starts when the
// threshold is crossed while idle. Machine-type identifiers are MOMENTARY/REED.
// Relay drive still uses pulse for the short K1 close (emitPulse,
// COMPILED_STOP_PULSE_MS).

bool brewSeesPhysicalSwitchEdges = true;
bool momentaryPhysicalOn = false;
bool momentaryRawOn = false;
uint32_t momentaryRawChangedAtMs = 0;
uint32_t momentaryPressStartedAtMs = 0;
bool momentaryPressHeld = false;
bool momentaryLongPressActive = false;
uint32_t momentaryCapturedPressMs = 0;
bool momentaryUserStopThisCycle = false;
bool momentaryStartEdgeThisCycle = false;
bool momentaryDeferredStopPending = false;
uint32_t momentarySyntheticOnAtMs = 0;
bool momentaryLogicalRunActive = false;
uint32_t momentaryLogicalRunStartedAtMs = 0;
uint32_t momentaryLogicalOperationalLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS;
bool pulseOutputActive = false;
bool pulseOutputIsStart = false;
uint32_t pulseOutputEndsAtMs = 0;
bool momentaryIgnoreMatchingRelease = false;

bool readRawPaddleOn() {
  return digitalRead(PADDLE_GPIO) == PADDLE_ACTIVE_LEVEL;
}

void initializePaddleInput() {
  pinMode(PADDLE_GPIO, INPUT_PULLUP);
#if SHOT_STOPPER_MACHINE_TYPE == 2
  pinMode(REED_GPIO, INPUT_PULLUP);
#endif
  momentaryRawOn = readRawPaddleOn();
  momentaryPhysicalOn = momentaryRawOn;
  momentaryRawChangedAtMs = millis();
  rawPaddleOn = momentaryRawOn;
  paddleOn = momentaryRawOn;
  brewSeesPhysicalSwitchEdges = true;
  momentaryPressHeld = false;
  momentaryLongPressActive = false;
  momentaryCapturedPressMs = 0;
  momentaryDeferredStopPending = false;
  momentaryLogicalRunActive = false;
  pulseOutputActive = false;
  momentaryIgnoreMatchingRelease = false;
}

void machineReleasePhysicalSwitchToBrew() {
  brewSeesPhysicalSwitchEdges = false;
  if (!momentaryPhysicalOn) {
    paddleOn = false;
    rawPaddleOn = false;
  }
}

void machineReconcileBrewOutcome(bool brewActive) {
  if (momentaryStartEdgeThisCycle && !brewActive) {
    paddleOn = false;
    momentaryDeferredStopPending = false;
  }
}

bool machineIsRunning();

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

  if (brewSeesPhysicalSwitchEdges) {
    if (physicalTurnedOn || physicalTurnedOff) {
      const bool previous = paddleOn;
      paddleOn = momentaryPhysicalOn;
      paddleTurnedOn = !previous && paddleOn;
      paddleTurnedOff = previous && !paddleOn;
      addDebugEvent(DebugCategory::PADDLE,
                    paddleOn ? DebugCode::PADDLE_ON : DebugCode::PADDLE_OFF);
    }
    return;
  }

  if (momentaryDeferredStopPending && paddleOn &&
      elapsedMs(momentarySyntheticOnAtMs) > runtimeConfig.rinseGestureMs) {
    paddleOn = false;
    paddleTurnedOff = true;
    momentaryUserStopThisCycle = true;
    momentaryDeferredStopPending = false;
    addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_OFF);
  }

  if (physicalTurnedOn) {
    momentaryPressHeld = true;
    momentaryPressStartedAtMs = millis();
    momentaryLongPressActive = false;
    if (runtimeConfig.momentaryStartOnPress && !paddleOn &&
        !machineIsRunning()) {
      momentaryCapturedPressMs = COMPILED_STOP_PULSE_MS;
      momentaryIgnoreMatchingRelease = true;
      paddleOn = true;
      paddleTurnedOn = true;
      momentaryStartEdgeThisCycle = true;
      momentarySyntheticOnAtMs = millis();
      addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_ON);
    }
  }

  if (momentaryPressHeld && momentaryPhysicalOn && !momentaryLongPressActive &&
      !paddleOn && !momentaryLogicalRunActive &&
      elapsedMs(momentaryPressStartedAtMs) >= COMPILED_MAX_SINGLE_PRESS_MS) {
    momentaryLongPressActive = true;
    momentaryPressHeld = false;
  }

  if (physicalTurnedOff && (momentaryPressHeld || momentaryLongPressActive)) {
    const uint32_t heldMs = elapsedMs(momentaryPressStartedAtMs);
    const bool wasLong = momentaryLongPressActive;
    momentaryPressHeld = false;
    momentaryLongPressActive = false;
    if (wasLong) {
      momentaryIgnoreMatchingRelease = false;
      return;
    }
    if (momentaryIgnoreMatchingRelease) {
      momentaryIgnoreMatchingRelease = false;
      return;
    }
    momentaryCapturedPressMs = heldMs < 50U ? 50U : heldMs;
    if (!paddleOn && !machineIsRunning()) {
      paddleOn = true;
      paddleTurnedOn = true;
      momentaryStartEdgeThisCycle = true;
      momentarySyntheticOnAtMs = millis();
      addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_ON);
    } else if (paddleOn && elapsedMs(momentarySyntheticOnAtMs) <=
               runtimeConfig.rinseGestureMs) {
      momentaryDeferredStopPending = true;
    } else {
      paddleOn = false;
      paddleTurnedOff = true;
      momentaryUserStopThisCycle = true;
      addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_OFF);
    }
  }
}
