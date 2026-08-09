#pragma once

#include <stdint.h>

namespace shotstopper {

enum class RelaySafetyState : uint8_t {
  BOOT_SAFE,
  OPEN,
  ARMING,
  CLOSED,
  TRIPPED,
  LOCKOUT
};

enum class RelaySafetyFault : uint8_t {
  NONE,
  INITIALIZATION_FAILED,
  WATCHDOG_UNAVAILABLE,
  INVALID_LIMIT,
  TIMER_ARM_FAILED,
  HARD_LIMIT,
  OPERATIONAL_LIMIT,
  FEEDBACK_STUCK_CLOSED,
  FEEDBACK_FAILED_TO_CLOSE,
  FEEDBACK_CHANGED_UNEXPECTEDLY,
  TASK_WATCHDOG_FAILURE,
  RESET_DURING_CLOSE,
  UNSAFE_RESET,
  BOOT_LOOP
};

inline const char *relaySafetyStateName(RelaySafetyState state) {
  switch (state) {
    case RelaySafetyState::BOOT_SAFE: return "BOOT_SAFE";
    case RelaySafetyState::OPEN: return "OPEN";
    case RelaySafetyState::ARMING: return "ARMING";
    case RelaySafetyState::CLOSED: return "CLOSED";
    case RelaySafetyState::TRIPPED: return "TRIPPED";
    case RelaySafetyState::LOCKOUT: return "LOCKOUT";
  }
  return "UNKNOWN";
}

inline const char *relaySafetyFaultName(RelaySafetyFault fault) {
  switch (fault) {
    case RelaySafetyFault::NONE: return "NONE";
    case RelaySafetyFault::INITIALIZATION_FAILED:
      return "INITIALIZATION_FAILED";
    case RelaySafetyFault::WATCHDOG_UNAVAILABLE:
      return "WATCHDOG_UNAVAILABLE";
    case RelaySafetyFault::INVALID_LIMIT: return "INVALID_LIMIT";
    case RelaySafetyFault::TIMER_ARM_FAILED: return "TIMER_ARM_FAILED";
    case RelaySafetyFault::HARD_LIMIT: return "HARD_LIMIT";
    case RelaySafetyFault::OPERATIONAL_LIMIT: return "OPERATIONAL_LIMIT";
    case RelaySafetyFault::FEEDBACK_STUCK_CLOSED:
      return "FEEDBACK_STUCK_CLOSED";
    case RelaySafetyFault::FEEDBACK_FAILED_TO_CLOSE:
      return "FEEDBACK_FAILED_TO_CLOSE";
    case RelaySafetyFault::FEEDBACK_CHANGED_UNEXPECTEDLY:
      return "FEEDBACK_CHANGED_UNEXPECTEDLY";
    case RelaySafetyFault::TASK_WATCHDOG_FAILURE:
      return "TASK_WATCHDOG_FAILURE";
    case RelaySafetyFault::RESET_DURING_CLOSE:
      return "RESET_DURING_CLOSE";
    case RelaySafetyFault::UNSAFE_RESET: return "UNSAFE_RESET";
    case RelaySafetyFault::BOOT_LOOP: return "BOOT_LOOP";
  }
  return "UNKNOWN";
}

struct RelaySafetySnapshot {
  RelaySafetyState state = RelaySafetyState::BOOT_SAFE;
  RelaySafetyFault fault = RelaySafetyFault::NONE;
  bool closed = false;
  bool commandedClosed = false;
  bool feedbackClosed = false;
  bool feedbackAvailable = false;
  bool externalSafetyPresent = false;
  bool watchdogReady = false;
  bool timersReady = false;
  bool tripped = false;
  bool operationalTripped = false;
  uint32_t generation = 0;
  uint32_t closedAtMs = 0;
  uint32_t operationalLimitMs = 0;
  uint32_t resetReasonCode = 0;
  uint32_t unsafeResetCount = 0;
  bool resetRecoveryRequired = false;
  bool bootLoopDetected = false;
};

}  // namespace shotstopper
