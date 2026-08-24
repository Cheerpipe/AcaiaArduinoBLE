#pragma once

#include <stdint.h>

namespace shotstopper {

// =============================================================================
// LAYER: Machine types (shared contracts)
// =============================================================================
// WHAT: Enums and snapshots shared by the machine façade and specializations
//       (UserIntent, MachineRunState, MachineSense).
//
// BOUNDARY: These types are the ONLY machine vocabulary brew/stopper/guards
// may use. They must never branch on paddle vs switch, paddle-mode settings,
// or compiled machine identity. UserIntent is the product of the activator:
// the specialization reads ACTIVATOR_GPIO and translates that signal here.
// MachineSense is a one-way push from the stopper — specializations must not
// reach into session or live scale globals.

// Electrical / machine timing. Brew walls must never exceed this hard cap.
constexpr uint32_t HARD_MAX_CIRCUIT_CLOSED_MS = 60000;
constexpr uint32_t DEFAULT_OPERATIONAL_WALL_MS = 50000;

enum class MachineRunState : uint8_t {
  CONFIRMED_OFF = 0,
  ASSUMED_ON = 1,
  CONFIRMED_ON = 2,
  UNKNOWN = 3,
  ASSUMED_OFF = 4
};

// Weight/brew snapshot the stopper pushes into machine each loop. Momentary
// inference must not read session or the live scale globals.
struct MachineSense {
  float weightG = 0.0f;
  bool weightFresh = false;
  bool accidentalHold = false;
  bool brewCycleActive = false;
  // BLE packet generation for this reading. Momentary flow uses this to ignore
  // control-loop resamples of the same packet; paddle does not read it.
  uint32_t weightSequence = 0;
  // Brew has latched first drops this cycle. Momentary uses it to pulse a
  // weight cut even if inferred state already fell back to ASSUMED_ON.
  bool firstDropSeen = false;
  // One control-loop tick: scale just became CONNECTED. Set by the stopper
  // after consuming a BLE-worker pending flag — never mutated on the BLE task.
  bool scaleConnectedEdge = false;
};

inline bool machinePreferBleAirtime = false;

// Translated activator intention. Not a GPIO level: paddle, switch, or another
// compatible mechanism interprets the pin and publishes one of these values.
enum class UserIntent : uint8_t {
  NONE = 0,
  REQUEST_START = 1,
  REQUEST_STOP = 2,
  HOLD_ACTIVE = 3,
  STABLE_IDLE = 4
};

inline const char *machineRunStateName(MachineRunState state) {
  switch (state) {
    case MachineRunState::CONFIRMED_OFF: return "CONFIRMED_OFF";
    case MachineRunState::ASSUMED_ON: return "ASSUMED_ON";
    case MachineRunState::CONFIRMED_ON: return "CONFIRMED_ON";
    case MachineRunState::UNKNOWN: return "UNKNOWN";
    case MachineRunState::ASSUMED_OFF: return "ASSUMED_OFF";
  }
  return "UNKNOWN";
}

}  // namespace shotstopper
