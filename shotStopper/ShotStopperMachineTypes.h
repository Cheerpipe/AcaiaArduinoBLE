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

enum class UserIntent : uint8_t {
  NONE = 0,
  REQUEST_START = 1,
  REQUEST_STOP = 2,
  HOLD_ACTIVE = 3,
  STABLE_IDLE = 4
};

inline const char *momentaryStartEdgeId(bool startOnPress) {
  return startOnPress ? "press" : "release";
}

inline bool parseMomentaryStartEdge(const char *text, bool &startOnPress) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "press") == 0) {
    startOnPress = true;
    return true;
  }
  if (strcmp(text, "release") == 0) {
    startOnPress = false;
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
