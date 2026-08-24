#pragma once

// =============================================================================
// SPECIALIZATION: Paddle / latch-switch — GPIO input
// =============================================================================
// WHAT: Raw sample, debounce, and stable-off for the latch paddle.
//       Included from ShotStopperMachine.h in the shotStopper.cpp TU.
//
// BOUNDARY: Paddle GPIO only. Writes shared activator sample fields
// (ShotStopperMachineActivatorSample.h). Stopper/brew/cup/scale must not read
// these — they consume UserIntent from the machine façade. No momentary code.

bool readRawActivatorOn() {
  return digitalRead(ACTIVATOR_GPIO) == ACTIVATOR_ACTIVE_LEVEL;
}

void initializeActivatorInput() {
  pinMode(ACTIVATOR_GPIO, INPUT_PULLUP);
  rawActivatorOn = readRawActivatorOn();
  activatorOn = rawActivatorOn;
  rawActivatorChangedAtMs = millis();
}

void updateActivatorInput() {
  activatorTurnedOn = false;
  activatorTurnedOff = false;

  const bool sampledOn = readRawActivatorOn();
  if (sampledOn != rawActivatorOn) {
    rawActivatorOn = sampledOn;
    rawActivatorChangedAtMs = millis();
  }

  if (activatorOn != rawActivatorOn &&
      elapsedMs(rawActivatorChangedAtMs) >= ACTIVATOR_DEBOUNCE_MS) {
    const bool previous = activatorOn;
    activatorOn = rawActivatorOn;
    activatorTurnedOn = !previous && activatorOn;
    activatorTurnedOff = previous && !activatorOn;

    addDebugEvent(DebugCategory::ACTIVATOR,
                  activatorOn ? DebugCode::ACTIVATOR_ON : DebugCode::ACTIVATOR_OFF);
  }
}

bool activatorIsStablyOff() {
  return !activatorOn && !rawActivatorOn &&
         elapsedMs(rawActivatorChangedAtMs) >= ACTIVATOR_DEBOUNCE_MS;
}
