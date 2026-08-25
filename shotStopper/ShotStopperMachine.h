#pragma once

#include "ShotStopperHardware.h"
#include "ShotStopperMachineTypes.h"
#include "ShotStopperSafety.h"

// =============================================================================
// LAYER: Machine (façade)
// =============================================================================
// WHAT: Generic machine API. The activator reads user intention on
//       ACTIVATOR_GPIO (paddle latch, momentary switch, or another compatible
//       mechanism) and translates that signal into UserIntent /
//       MachineIntention. Compile-time selects the specialization behind one
//       surface: start/stop, run state, cycle policy, MachineSense.
//
// BOUNDARY (hard rules — do not cross):
// - The activator owns GPIO sampling, debounce, edge interpretation, and
//   translation to UserIntent. Shot stopper / brew / cup / scale / guards
//   talk ONLY to this abstract machine. They must NOT include paddle or
//   momentary headers, branch on MachineType, read paddleMode / reed /
//   momentary GPIO, or know how a specialization drives K1.
// - Paddle code lives only under ShotStopperMachinePaddle*. Momentary under
//   ShotStopperMachineMomentary*. Never mix paddle logic into momentary files
//   or the reverse. Run state for each type is owned solely by that type's
//   *State* header (PaddleState vs MomentaryOnlyState / MomentaryReedState).
// - Rinse *detection* is machine-owned (gesture → REQUEST_RINSE). Rinse
//   *duration* lives in ShotStopperRinse. The stopper orchestrates: it does
//   not classify ON→OFF windows or long-press. machineBeginRinse/EndRinse
//   latch rinse actuation (not a second rinse FSM). Brew/scale/cup never
//   call actuators here.
// - Activator→K1 forwarding permission is a one-way stopper push
//   (machineSetActivatorDriveAllowed), like preferBleAirtime. Guards never
//   write it. machineRequestStart/Stop and the relay driver must not consult
//   it (rinse, web, firmware stop pulse still close K1).

#include "ShotStopperMachineRelay.h"
#include "ShotStopperMachineActivatorSample.h"

struct MachineIntention {
  UserIntent intent = UserIntent::NONE;
  bool holdActive = false;
  bool turnedOn = false;
  bool turnedOff = false;
  bool stablyOff = false;
};

MachineIntention lastPolledIntention;
MachineSense machineSense;

inline MachineIntention machineCaptureIntention(const MachineIntention &intention) {
  lastPolledIntention = intention;
  return intention;
}

inline MachineIntention machineLastIntention() { return lastPolledIntention; }

inline void machineObserveSense(const MachineSense &sense) {
  machineSense = sense;
}

inline void machineSetPreferBleAirtime(bool prefer) {
  const bool was = machinePreferBleAirtime;
  machinePreferBleAirtime = prefer;
  // Apply BT preference before K1 pull-in so coex/GATT work does not race
  // the coil. Clearing the flag restores immediately if close never happened;
  // a successful close still restores via pendingBrewRfRestore after open.
  if (prefer && !was) {
    applyBrewRfPreference(true);
  } else if (!prefer && was && !getRelaySafetySnapshot().closed) {
    applyBrewRfPreference(false);
  }
}

// Dumb drive bit + hold-continuity latch. Specializations must not know why
// the stopper cleared the bit (no-scale BBW, cup-start, or a future guard).
bool machineActivatorDriveAllowed = true;
bool machineActivatorDriveSuppressedThisHold = false;
bool rinseActuationActive = false;

inline void machineSetActivatorDriveAllowed(bool allowed) {
  machineActivatorDriveAllowed = allowed;
}

inline bool machineMayForwardActivatorOn() {
  if (!machineActivatorDriveAllowed) {
    machineActivatorDriveSuppressedThisHold = true;
    return false;
  }
  if (machineActivatorDriveSuppressedThisHold) {
    return false;
  }
  return true;
}

inline void machineNoteActivatorReleased() {
  machineActivatorDriveSuppressedThisHold = false;
}

#if SHOT_STOPPER_MACHINE_TYPE == 0
#include "ShotStopperMachinePaddleInput.h"
#include "ShotStopperMachinePaddleControl.h"
#include "ShotStopperMachinePaddleState.h"
#include "ShotStopperMachinePaddlePolicy.h"

inline void machineOnActivatorReady() {}
inline void machineOnBrewOutcome(bool) {}
inline bool machineSupportsRinse() { return runtimeConfig.rinseEnabled; }
inline void machineNoteFirmwareStop() {
  if (activatorOn || rawActivatorOn) {
    machineActivatorDriveSuppressedThisHold = true;
  }
}
inline void machineSampleInput() {
  updateActivatorInput();
  applyPaddleRelayDrive();
}
inline void serviceMachine() {
  machineServiceReminders();
  applyPaddleRelayDrive();
}
inline void machineOverrideInferredOff() {}
inline void machineOverrideInferredOn() {}
inline void machineArmSettledWeightCutOff() {}
inline void machineNoteSettledWeightCutOff() {}
inline void machineCancelSettledWeightCutOff() {}
#else
#include "ShotStopperMachineMomentaryInput.h"
#if SHOT_STOPPER_MACHINE_TYPE == 2
#include "ShotStopperMachineMomentaryReedState.h"
#else
#include "ShotStopperMachineMomentaryOnlyState.h"
#endif
#include "ShotStopperMachineMomentaryControl.h"

#if SHOT_STOPPER_MACHINE_TYPE == 2
inline void machineOverrideInferredOff() {}
inline void machineOverrideInferredOn() {}
inline void machineArmSettledWeightCutOff() {}
inline void machineNoteSettledWeightCutOff() {}
inline void machineCancelSettledWeightCutOff() {}
#endif

inline bool machineSupportsRinse() { return runtimeConfig.rinseEnabled; }
inline void machineBeginCycle(bool) {}
inline void machineEndCycle() { rinseActuationActive = false; }
inline bool machineHidesPhysicalStop() { return false; }
inline bool machineAllowsAutomationStop() { return true; }
inline uint32_t machineCloseLimitMs(uint32_t operationalWallMs) {
  return operationalWallMs;
}
inline bool machineCycleHardMaxArmedForTest() { return false; }
inline bool machineCyclePromotedToNaturalForTest() { return false; }
inline uint32_t machineLastActivatorEdgeMs() { return momentaryRawChangedAtMs; }
inline void machineNoteFirmwareStop() {
  momentarySkipFirmwareStopPulse = false;
}
inline void machineServiceReminders() {}
inline void machineSampleInput() { updateActivatorInput(); }

inline MachineIntention machinePollIntention() {
  MachineIntention out;
  out.turnedOn = activatorTurnedOn;
  out.turnedOff = activatorTurnedOff;
  out.holdActive = momentaryPhysicalOn || rawActivatorOn;
  out.stablyOff = activatorIsStablyOff();
  if (momentaryRinseRequested) {
    out.intent = UserIntent::REQUEST_RINSE;
  } else if (activatorTurnedOn) {
    out.intent = UserIntent::REQUEST_START;
  } else if (activatorTurnedOff) {
    out.intent = UserIntent::REQUEST_STOP;
  } else if (out.holdActive) {
    out.intent = UserIntent::HOLD_ACTIVE;
  } else if (out.stablyOff) {
    out.intent = UserIntent::STABLE_IDLE;
  }
  return machineCaptureIntention(out);
}
#endif

#if SHOT_STOPPER_MACHINE_TYPE != 1
inline bool machineTakeNoFlowIdle() { return false; }
#endif

inline void machineInitialize() {
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  if (EXTERNAL_SAFETY_HARDWARE_PRESENT) {
    pinMode(SAFETY_HEARTBEAT_GPIO, OUTPUT);
    digitalWrite(SAFETY_HEARTBEAT_GPIO, LOW);
    pinMode(CIRCUIT_FEEDBACK_GPIO, INPUT_PULLUP);
  }
  initializeActivatorInput();
  machineSetActivatorDriveAllowed(true);
  machineNoteActivatorReleased();
}

inline bool machineBootActivatorHeldStably() {
  if (!readRawActivatorOn()) {
    return false;
  }
#ifndef SHOT_STOPPER_HOST_TEST
  const uint32_t startedAtMs = millis();
  while (static_cast<uint32_t>(millis() - startedAtMs) <
         ACTIVATOR_DEBOUNCE_MS) {
    if (!readRawActivatorOn()) {
      return false;
    }
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
#endif
  return readRawActivatorOn();
}

inline void machineFillStatus(ControlStatusSnapshot &status) {
  const bool rawOn = readRawActivatorOn();
  status.rawActivatorOn = rawOn;
  status.physicalActivatorOn = rawOn;
  status.circuitElapsedMs = machineElapsedMs();
  machineFillInferenceStatus(status);
}
