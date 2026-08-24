#pragma once

// Momentary-switch GPIO: debounce, live 1:1 relay mirror, short/long
// classification on release. A short press is start/stop for brew. A long
// press is mirrored and ignored by brew (machine-native rinse, etc.).

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

void initializePaddleInput() {
  pinMode(PADDLE_GPIO, INPUT_PULLUP);
#if SHOT_STOPPER_MACHINE_TYPE == 2
  pinMode(REED_GPIO, INPUT_PULLUP);
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

  if (physicalTurnedOn) {
    momentaryPressHeld = true;
    momentaryPressStartedAtMs = millis();
    addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_ON);
  }

  if (physicalTurnedOff && momentaryPressHeld) {
    const uint32_t heldMs = elapsedMs(momentaryPressStartedAtMs);
    momentaryPressHeld = false;
    addDebugEvent(DebugCategory::PADDLE, DebugCode::PADDLE_OFF);
    if (heldMs > runtimeMaxSinglePressMs(runtimeConfig)) {
      return;
    }
    if (machineIsRunning()) {
      paddleOn = false;
      paddleTurnedOff = true;
      momentaryUserStopThisCycle = true;
      momentarySkipFirmwareStopPulse = true;
      if (!machineSense.brewCycleActive) {
        momentaryLogicalRunActive = false;
        noteMomentaryLogicalStop();
      }
    } else {
      paddleOn = false;
      paddleTurnedOn = true;
      momentaryStartEdgeThisCycle = true;
      noteMomentaryLogicalStart();
      momentaryLogicalRunActive = true;
      momentaryLogicalRunStartedAtMs = millis();
      momentaryLogicalOperationalLimitMs = HARD_MAX_CIRCUIT_CLOSED_MS;
    }
  }
}
