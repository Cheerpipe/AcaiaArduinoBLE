#pragma once

// Paddle / latch-switch actuator: start latches the circuit closed, stop opens it.
// Included from ShotStopperMachine.h after the relay driver.

inline bool machineRequestStart(uint32_t operationalLimitMs) {
  return setCn9Closed(true, operationalLimitMs);
}

inline bool machineRequestStop() {
  return setCn9Closed(false);
}
