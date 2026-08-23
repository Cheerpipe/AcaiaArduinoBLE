#pragma once

#include "ShotStopperHardware.h"
#include "ShotStopperMachineTypes.h"
#include "ShotStopperSafety.h"

// Machine façade (paddle / latch-switch). Brew talks to this header the same
// way it does today: paddleOn edges, machineRequestStart/Stop, machineIsRunning.
// Internals are split so later machine types can replace input/control/state
// without changing that contract. Included from shotStopper.cpp after logging
// helpers and BSS globals. State is file-scope BSS — no heap.

#include "ShotStopperMachineRelay.h"
#include "ShotStopperMachinePaddleInput.h"
#include "ShotStopperMachinePaddleControl.h"
#include "ShotStopperMachinePaddleState.h"

struct MachineIntention {
  UserIntent intent = UserIntent::NONE;
  bool holdActive = false;
  bool turnedOn = false;
  bool turnedOff = false;
  bool stablyOff = false;
};

inline MachineIntention machinePollIntention() {
  MachineIntention out;
  out.turnedOn = paddleTurnedOn;
  out.turnedOff = paddleTurnedOff;
  out.holdActive = paddleOn;
  out.stablyOff = paddleIsStablyOff();
  if (paddleTurnedOn) {
    out.intent = UserIntent::REQUEST_START;
  } else if (paddleTurnedOff) {
    out.intent = UserIntent::REQUEST_STOP;
  } else if (paddleOn) {
    out.intent = UserIntent::HOLD_ACTIVE;
  } else if (out.stablyOff) {
    out.intent = UserIntent::STABLE_IDLE;
  }
  return out;
}
