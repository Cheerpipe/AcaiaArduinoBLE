#pragma once

#include <stdint.h>

namespace shotstopper {

constexpr uint32_t RECOVERY_MODE_WINDOW_MS = 60000;
constexpr uint32_t RECOVERY_GESTURE_WINDOW_MS = 5000;
constexpr uint32_t RECOVERY_CONFIRMATION_MS = 3000;
constexpr uint8_t RECOVERY_NETWORK_CYCLES = 3;
constexpr uint8_t RECOVERY_FACTORY_CYCLES = 5;

enum class RecoveryGestureResult : uint8_t {
  NONE = 0,
  NETWORK_ACCESS_RESET,
  FACTORY_RESET,
  TIMED_OUT,
};

inline bool recoveryGestureEntryAllowed(bool powerOnReset,
                                        bool activatorStablyOn) {
  return powerOnReset && activatorStablyOn;
}

// Boot-local recognizer. Inputs are already debounced by the firmware. A
// gesture begins on the first OFF edge and counts complete OFF->ON cycles.
// Unsigned subtraction deliberately keeps all deadlines safe across millis()
// wraparound.
struct RecoveryGestureRecognizer {
  bool active = false;
  bool attemptActive = false;
  uint8_t completedCycles = 0;
  uint32_t modeStartedAtMs = 0;
  uint32_t attemptStartedAtMs = 0;
  uint32_t lastTransitionAtMs = 0;

  void begin(uint32_t nowMs) {
    active = true;
    attemptActive = false;
    completedCycles = 0;
    modeStartedAtMs = nowMs;
    attemptStartedAtMs = 0;
    lastTransitionAtMs = nowMs;
  }

  void resetAttempt() {
    attemptActive = false;
    completedCycles = 0;
    attemptStartedAtMs = 0;
  }

  RecoveryGestureResult update(uint32_t nowMs, bool activatorOn,
                               bool turnedOn, bool turnedOff) {
    if (!active) {
      return RecoveryGestureResult::NONE;
    }
    if (static_cast<uint32_t>(nowMs - modeStartedAtMs) >=
        RECOVERY_MODE_WINDOW_MS) {
      active = false;
      resetAttempt();
      return RecoveryGestureResult::TIMED_OUT;
    }

    const bool transitioned = turnedOn || turnedOff;
    if (transitioned && !attemptActive) {
      if (!turnedOff) {
        return RecoveryGestureResult::NONE;
      }
      attemptActive = true;
      completedCycles = 0;
      attemptStartedAtMs = nowMs;
      lastTransitionAtMs = nowMs;
      return RecoveryGestureResult::NONE;
    }
    if (!attemptActive) {
      return RecoveryGestureResult::NONE;
    }

    if (transitioned) {
      const bool insideGestureWindow =
          static_cast<uint32_t>(nowMs - attemptStartedAtMs) <=
          RECOVERY_GESTURE_WINDOW_MS;
      if (!insideGestureWindow) {
        resetAttempt();
        // An OFF edge can also be the first edge of a fresh attempt.
        if (turnedOff) {
          attemptActive = true;
          attemptStartedAtMs = nowMs;
          lastTransitionAtMs = nowMs;
        }
        return RecoveryGestureResult::NONE;
      }
      lastTransitionAtMs = nowMs;
      if (turnedOn) {
        ++completedCycles;
        if (completedCycles > RECOVERY_FACTORY_CYCLES) {
          resetAttempt();
        }
      }
      return RecoveryGestureResult::NONE;
    }

    const bool candidate = activatorOn &&
                           (completedCycles == RECOVERY_NETWORK_CYCLES ||
                            completedCycles == RECOVERY_FACTORY_CYCLES);
    if (candidate &&
        static_cast<uint32_t>(nowMs - lastTransitionAtMs) >=
            RECOVERY_CONFIRMATION_MS) {
      const RecoveryGestureResult result =
          completedCycles == RECOVERY_FACTORY_CYCLES
              ? RecoveryGestureResult::FACTORY_RESET
              : RecoveryGestureResult::NETWORK_ACCESS_RESET;
      active = false;
      resetAttempt();
      return result;
    }

    // Non-candidates expire when their five-second movement window closes.
    // Valid 3/5-cycle candidates may finish their separate confirmation wait
    // after the movement window, but still before the 60-second mode deadline.
    if (!candidate &&
        static_cast<uint32_t>(nowMs - attemptStartedAtMs) >
            RECOVERY_GESTURE_WINDOW_MS) {
      resetAttempt();
    }
    return RecoveryGestureResult::NONE;
  }
};

}  // namespace shotstopper
