#pragma once

#include "ShotStopperHardware.h"
#include "ShotStopperMachineTypes.h"
#include "ShotStopperSafety.h"

// Machine façade. Brew talks to paddleOn edges, machineRequestStart/Stop,
// and machineIsRunning. Included from shotStopper.cpp after logging helpers
// and BSS globals. State is file-scope BSS — no heap.

#include "ShotStopperMachineRelay.h"
#if SHOT_STOPPER_MACHINE_TYPE == 0
#include "ShotStopperMachinePaddleInput.h"
#include "ShotStopperMachinePaddleControl.h"
#include "ShotStopperMachinePaddleState.h"

inline void serviceMachine() {}
inline void machineReleasePhysicalSwitchToBrew() {}
inline void machineReconcileBrewOutcome() {}
inline bool machineSupportsRinse() { return true; }
inline bool reedIsOn() { return false; }
#else
#include "ShotStopperMachineMomentaryInput.h"
#if SHOT_STOPPER_MACHINE_TYPE == 2
#include "ShotStopperMachineMomentaryReedState.h"
#else
#include "ShotStopperMachineMomentaryOnlyState.h"
#endif
#include "ShotStopperMachineMomentaryControl.h"

inline bool machineSupportsRinse() { return false; }
#endif

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
