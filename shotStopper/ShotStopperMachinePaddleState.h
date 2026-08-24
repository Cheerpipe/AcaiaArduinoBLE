#pragma once

// =============================================================================
// SPECIALIZATION: Paddle / latch-switch — run view (state)
// =============================================================================
// WHAT: MachineRunState for paddle builds. Group brewing == circuit closed
//       (relay snapshot). Included from ShotStopperMachine.h after the relay.
//
// BOUNDARY: Paddle-only. This file owns paddle run-state derivation. Do not
// put momentary/reed inference here. Stopper/brew/cup/scale never include this
// directly — only via the machine façade.

// One snapshot: closed + elapsed. Callers that need both must use this so an
// ISR open between two getRelaySafetySnapshot() calls cannot stamp 0 ms.
inline bool machineRunningElapsed(uint32_t &elapsedOut) {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (!relay.closed) {
    elapsedOut = 0U;
    return false;
  }
  elapsedOut = elapsedMs(relay.closedAtMs);
  return true;
}

inline bool machineIsRunning() {
  return getRelaySafetySnapshot().closed;
}

inline uint32_t machineElapsedMs() {
  uint32_t elapsed = 0U;
  (void)machineRunningElapsed(elapsed);
  return elapsed;
}

inline MachineRunState machineRunState() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (relay.state == RelaySafetyState::LOCKOUT ||
      relay.state == RelaySafetyState::TRIPPED) {
    return relay.closed ? MachineRunState::UNKNOWN : MachineRunState::CONFIRMED_OFF;
  }
  if (relay.state == RelaySafetyState::ARMING) {
    return MachineRunState::ASSUMED_ON;
  }
  if (relay.closed || relay.state == RelaySafetyState::CLOSED) {
    return MachineRunState::CONFIRMED_ON;
  }
  return MachineRunState::CONFIRMED_OFF;
}

inline void machineFillInferenceStatus(ControlStatusSnapshot &status) {
  status.machineStartAckPending = false;
  status.machineStopAckPending = false;
  status.machineOrphanRun = false;
}
