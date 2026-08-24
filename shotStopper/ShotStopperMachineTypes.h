#pragma once

#include <stdint.h>
#include <string.h>

namespace shotstopper {

// Electrical / machine timing. Brew walls must never exceed this hard cap.
constexpr uint32_t HARD_MAX_CIRCUIT_CLOSED_MS = 60000;
constexpr uint32_t DEFAULT_OPERATIONAL_WALL_MS = 50000;
constexpr uint32_t DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS = 10000;
constexpr uint32_t MIN_PADDLE_RETURN_REMINDER_INTERVAL_MS = 5000;
constexpr uint32_t MAX_PADDLE_RETURN_REMINDER_INTERVAL_MS = 60000;
constexpr uint32_t DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS =
    15UL * 60UL * 1000UL;
constexpr uint32_t MIN_PADDLE_RETURN_REMINDER_MAX_DURATION_MS = 60000UL;
constexpr uint32_t MAX_PADDLE_RETURN_REMINDER_MAX_DURATION_MS =
    60UL * 60UL * 1000UL;

enum class MachineType : uint8_t {
  PADDLE = 0,
  MOMENTARY = 1,
  MOMENTARY_REED = 2
};

enum class MachineRunState : uint8_t {
  CONFIRMED_OFF = 0,
  ASSUMED_ON = 1,
  CONFIRMED_ON = 2,
  UNKNOWN = 3
};

// Weight/brew snapshot the stopper pushes into machine each loop. Momentary
// inference must not read session or the live scale globals.
struct MachineSense {
  float weightG = 0.0f;
  bool weightFresh = false;
  bool accidentalHold = false;
  bool brewCycleActive = false;
};

inline bool machinePreferBleAirtime = false;

enum class UserIntent : uint8_t {
  NONE = 0,
  REQUEST_START = 1,
  REQUEST_STOP = 2,
  HOLD_ACTIVE = 3,
  STABLE_IDLE = 4
};

// Latch-switch brew feel. Snapshot by the machine at cycle start; brew/stopper
// never branch on this enum. Natural = OFF ends the shot. Original = legacy
// start gesture (BBW+scale: OFF after rinse keeps the circuit closed until
// weight stop; ON again promotes to Natural). Auto = BBW finish regardless of
// ON/OFF (no promote, no hold-off of automation).
enum class PaddleMode : uint8_t {
  NATURAL = 0,
  ORIGINAL = 1,
  AUTO = 2
};

inline bool validPaddleMode(uint8_t mode) {
  return mode == static_cast<uint8_t>(PaddleMode::NATURAL) ||
         mode == static_cast<uint8_t>(PaddleMode::ORIGINAL) ||
         mode == static_cast<uint8_t>(PaddleMode::AUTO);
}

inline const char *paddleModeId(uint8_t mode) {
  switch (static_cast<PaddleMode>(mode)) {
    case PaddleMode::ORIGINAL:
      return "original";
    case PaddleMode::AUTO:
      return "auto";
    case PaddleMode::NATURAL:
    default:
      return "natural";
  }
}

inline bool parsePaddleMode(const char *text, uint8_t &mode) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "natural") == 0) {
    mode = static_cast<uint8_t>(PaddleMode::NATURAL);
    return true;
  }
  if (strcmp(text, "original") == 0) {
    mode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
    return true;
  }
  if (strcmp(text, "auto") == 0) {
    mode = static_cast<uint8_t>(PaddleMode::AUTO);
    return true;
  }
  return false;
}

inline const char *machineRunStateName(MachineRunState state) {
  switch (state) {
    case MachineRunState::CONFIRMED_OFF: return "CONFIRMED_OFF";
    case MachineRunState::ASSUMED_ON: return "ASSUMED_ON";
    case MachineRunState::CONFIRMED_ON: return "CONFIRMED_ON";
    case MachineRunState::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

}  // namespace shotstopper
