#pragma once

// =============================================================================
// SPECIALIZATION: Paddle / latch-switch — actuator (control)
// =============================================================================
// WHAT: Start latches the circuit closed; stop opens it (via relay safety).
//       Included from ShotStopperMachine.h after the relay driver.
//
// BOUNDARY: Paddle-only K1 requests. No momentary pulse/mirror logic. Upper
// layers call machineRequestStart/Stop on the façade, not this header.

inline bool machineRequestStart(uint32_t operationalLimitMs) {
  return setMachineCircuitClosed(true, operationalLimitMs);
}

inline bool machineRequestStop() {
  return setMachineCircuitClosed(false);
}
