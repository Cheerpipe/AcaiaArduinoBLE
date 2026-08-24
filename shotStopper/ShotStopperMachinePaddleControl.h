#pragma once

// =============================================================================
// SPECIALIZATION: Paddle / latch-switch — actuator (control)
// =============================================================================
// WHAT: Start latches the circuit closed; stop opens it (via relay safety).
//       Each loop, ON reasserts K1 if already commanded closed. OFF never
//       opens here — only machineRequestStop does (asymmetric mirror).
//
// BOUNDARY: Paddle-only K1 requests. No momentary pulse/mirror logic. Upper
// layers call machineRequestStart/Stop on the façade, not this header.

void applyPaddleRelayDrive() {
  const bool held = activatorOn || rawActivatorOn;
  if (!held) {
    machineNoteActivatorReleased();
    return;
  }
  if (!machineMayForwardActivatorOn()) {
    if (getRelaySafetySnapshot().closed) {
      (void)setMachineCircuitClosed(false);
    }
    return;
  }
  if (getRelaySafetySnapshot().closed) {
    (void)setMachineCircuitClosed(true, HARD_MAX_CIRCUIT_CLOSED_MS);
  }
}

inline bool machineRequestStart(uint32_t operationalLimitMs) {
  return setMachineCircuitClosed(true, operationalLimitMs);
}

inline bool machineRequestStop() {
  if (activatorOn || rawActivatorOn) {
    machineActivatorDriveSuppressedThisHold = true;
  }
  return setMachineCircuitClosed(false);
}
