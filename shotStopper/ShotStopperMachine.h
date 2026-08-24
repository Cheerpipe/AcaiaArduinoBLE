#pragma once

#include "ShotStopperHardware.h"
#include "ShotStopperMachineTypes.h"
#include "ShotStopperSafety.h"

// Machine façade. Stopper talks only to UserIntent, machineRequestStart/Stop,
// machineIsRunning, and cycle policy (Begin/End/AllowsAutomationStop).
// Included from shotStopper.cpp after logging helpers. Switch sample BSS lives
// in ShotStopperMachineSwitchSample.h, not in the stopper.

#include "ShotStopperMachineRelay.h"
#include "ShotStopperMachineSwitchSample.h"

struct MachineIntention {
  UserIntent intent = UserIntent::NONE;
  bool holdActive = false;
  bool turnedOn = false;
  bool turnedOff = false;
  bool stablyOff = false;
};

#if SHOT_STOPPER_MACHINE_TYPE == 0
#include "ShotStopperMachinePaddleInput.h"
#include "ShotStopperMachinePaddleControl.h"
#include "ShotStopperMachinePaddleState.h"
#include "ShotStopperMachinePaddlePolicy.h"

inline void machineReleasePhysicalSwitchToBrew() {}
inline void machineReconcileBrewOutcome() {}
inline bool machineSupportsRinse() { return true; }
inline bool reedIsOn() { return false; }
inline void machineNoteFirmwareStop() {}
inline void machineSampleInput() { updatePaddleInput(); }
inline void serviceMachine() { machineServiceReminders(); }
#else
#include "ShotStopperMachineMomentaryInput.h"
#if SHOT_STOPPER_MACHINE_TYPE == 2
#include "ShotStopperMachineMomentaryReedState.h"
#else
#include "ShotStopperMachineMomentaryOnlyState.h"
#endif
#include "ShotStopperMachineMomentaryControl.h"

inline bool machineSupportsRinse() { return false; }
inline void machineBeginCycle(bool) {}
inline void machineEndCycle() {}
inline bool machineHidesPhysicalStop() { return false; }
inline bool machineAllowsAutomationStop() { return true; }
inline uint32_t machineCloseLimitMs(uint32_t operationalWallMs) {
  return operationalWallMs;
}
inline bool machineCycleHardMaxArmedForTest() { return false; }
inline bool machineCyclePromotedToNaturalForTest() { return false; }
inline uint32_t machineLastRawEdgeMs() { return momentaryRawChangedAtMs; }
inline void machineNoteFirmwareStop() { momentaryUserStopThisCycle = true; }
inline void machineServiceReminders() {}
inline void machineSampleInput() { updatePaddleInput(); }

inline MachineIntention machinePollIntention() {
  MachineIntention out;
  out.turnedOn = paddleTurnedOn;
  out.turnedOff = paddleTurnedOff;
  out.holdActive = paddleOn || rawPaddleOn;
  out.stablyOff = paddleIsStablyOff();
  if (paddleTurnedOn) {
    out.intent = UserIntent::REQUEST_START;
  } else if (paddleTurnedOff) {
    out.intent = UserIntent::REQUEST_STOP;
  } else if (out.holdActive) {
    out.intent = UserIntent::HOLD_ACTIVE;
  } else if (out.stablyOff) {
    out.intent = UserIntent::STABLE_IDLE;
  }
  return out;
}
#endif

inline void machineInitialize() {
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  if (EXTERNAL_SAFETY_HARDWARE_PRESENT) {
    pinMode(SAFETY_HEARTBEAT_GPIO, OUTPUT);
    digitalWrite(SAFETY_HEARTBEAT_GPIO, LOW);
    pinMode(CIRCUIT_FEEDBACK_GPIO, INPUT_PULLUP);
  }
  initializePaddleInput();
}

inline bool machineBootSwitchHeldStably() {
  if (!readRawPaddleOn()) {
    return false;
  }
#ifndef SHOT_STOPPER_HOST_TEST
  const uint32_t startedAtMs = millis();
  while (static_cast<uint32_t>(millis() - startedAtMs) <
         PADDLE_DEBOUNCE_MS) {
    if (!readRawPaddleOn()) {
      return false;
    }
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
#endif
  return readRawPaddleOn();
}

inline void machineFillStatus(ControlStatusSnapshot &status) {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  status.reedOn = reedIsOn();
  status.physicalPaddleOn = readRawPaddleOn();
  status.circuitElapsedMs = MACHINE_USES_MOMENTARY_SWITCH
                                ? machineElapsedMs()
                                : (relay.closed ? elapsedMs(relay.closedAtMs) : 0);
#if SHOT_STOPPER_MACHINE_TYPE == 1
  status.machineStartAckPending = momentaryStartAwaitingAck;
  status.machineStopAckPending = momentaryStopAwaitingAck;
  status.machineOrphanRun = momentaryOrphanRun;
#else
  status.machineStartAckPending = false;
  status.machineStopAckPending = false;
  status.machineOrphanRun = false;
#endif
}
