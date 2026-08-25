#pragma once

#include <stdint.h>

// =============================================================================
// LAYER: Rinse (generic duration)
// =============================================================================
// WHAT: Timed rinse clock. Starts when the stopper *accepts* a rinse and
//       reports when the supplied duration has elapsed. Product rules (beeps,
//       no-scale BBW, history, locks) stay in the stopper.
//
// BOUNDARY: Duration only. Does not detect gestures, read GPIO, know paddle vs
// switch, call machine, or write CycleSession. The stopper copies
// rinseClockStartedAtMs into the cycle session if tests need a mirror.

uint32_t rinseClockDurationMs = 0;
uint32_t rinseClockStartedAtMs = 0;
bool rinseTimerActive = false;

uint32_t rinseBegin(uint32_t durationMs) {
  rinseClockDurationMs = durationMs;
  rinseClockStartedAtMs = millis();
  rinseTimerActive = true;
  return rinseClockStartedAtMs;
}

bool rinseDeadlineReached() {
  return rinseTimerActive &&
         elapsedMs(rinseClockStartedAtMs) >= rinseClockDurationMs;
}

void rinseClear() {
  rinseTimerActive = false;
  rinseClockDurationMs = 0;
  rinseClockStartedAtMs = 0;
}
