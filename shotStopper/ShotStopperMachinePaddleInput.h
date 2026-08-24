#pragma once

// =============================================================================
// SPECIALIZATION: Paddle / latch-switch — GPIO input
// =============================================================================
// WHAT: Raw sample, debounce, and stable-off for the latch paddle.
//       Included from ShotStopperMachine.h in the shotStopper.cpp TU.
//
// BOUNDARY: Paddle GPIO only. Writes shared switch sample fields
// (ShotStopperMachineSwitchSample.h). Stopper/brew/cup/scale must not read
// these — they consume UserIntent from the machine façade. No momentary code.

bool readRawPaddleOn() {
  return digitalRead(PADDLE_GPIO) == PADDLE_ACTIVE_LEVEL;
}

void initializePaddleInput() {
  pinMode(PADDLE_GPIO, INPUT_PULLUP);
  rawPaddleOn = readRawPaddleOn();
  paddleOn = rawPaddleOn;
  rawPaddleChangedAtMs = millis();
}

void updatePaddleInput() {
  paddleTurnedOn = false;
  paddleTurnedOff = false;

  const bool sampledOn = readRawPaddleOn();
  if (sampledOn != rawPaddleOn) {
    rawPaddleOn = sampledOn;
    rawPaddleChangedAtMs = millis();
  }

  if (paddleOn != rawPaddleOn &&
      elapsedMs(rawPaddleChangedAtMs) >= PADDLE_DEBOUNCE_MS) {
    const bool previous = paddleOn;
    paddleOn = rawPaddleOn;
    paddleTurnedOn = !previous && paddleOn;
    paddleTurnedOff = previous && !paddleOn;

    addDebugEvent(DebugCategory::PADDLE,
                  paddleOn ? DebugCode::PADDLE_ON : DebugCode::PADDLE_OFF);
  }
}

bool paddleIsStablyOff() {
  return !paddleOn && !rawPaddleOn &&
         elapsedMs(rawPaddleChangedAtMs) >= PADDLE_DEBOUNCE_MS;
}
