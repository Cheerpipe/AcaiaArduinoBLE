#pragma once

// Momentary + reed run view. The reed is canonical after the first stable OFF.
// Boot with the reed already ON is UNKNOWN until that OFF. Firmware auto-cut
// pulses only while the reed is canonically ON.

bool reedRawOn = false;
bool reedOn = false;
uint32_t reedChangedAtMs = 0;
uint32_t reedOnAtMs = 0;
bool reedSawStableOff = false;
bool momentaryStopRetryPending = false;
bool momentaryFirmwareStopIssued = false;

bool readRawReedOn() {
  return digitalRead(REED_GPIO) == REED_ACTIVE_LEVEL;
}

void sampleReed() {
  const bool sampledOn = readRawReedOn();
  if (sampledOn != reedRawOn) {
    reedRawOn = sampledOn;
    reedChangedAtMs = millis();
  }
  if (reedOn != reedRawOn &&
      elapsedMs(reedChangedAtMs) >= REED_DEBOUNCE_MS) {
    reedOn = reedRawOn;
    if (reedOn) {
      reedOnAtMs = millis();
    } else {
      reedSawStableOff = true;
    }
  } else if (!reedOn && !reedRawOn &&
             elapsedMs(reedChangedAtMs) >= REED_DEBOUNCE_MS) {
    reedSawStableOff = true;
  }
}

bool reedIsOn() { return reedOn; }

void noteMomentaryLogicalStart() {
  momentaryStopRetryPending = false;
  momentaryFirmwareStopIssued = false;
}
void noteMomentaryLogicalStop() {
  momentaryStopRetryPending = false;
  momentaryFirmwareStopIssued = true;
}
void serviceMomentaryRunSensors() { sampleReed(); }

bool machineAllowsFirmwareStopPulse() {
  sampleReed();
  if (momentaryFirmwareStopIssued && !momentaryUserStopThisCycle) {
    return false;
  }
  return reedOn && reedSawStableOff;
}

inline bool machineRunningElapsed(uint32_t &elapsedOut) {
  sampleReed();
  if (reedOn) {
    elapsedOut = elapsedMs(reedOnAtMs);
    return true;
  }
  if (momentaryLogicalRunActive) {
    elapsedOut = elapsedMs(momentaryLogicalRunStartedAtMs);
    return true;
  }
  elapsedOut = 0U;
  return false;
}

inline bool machineIsRunning() {
  sampleReed();
  return reedOn || momentaryLogicalRunActive;
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
    return (reedOn || momentaryLogicalRunActive) ? MachineRunState::UNKNOWN
                                                  : MachineRunState::CONFIRMED_OFF;
  }
  sampleReed();
  if (!reedSawStableOff && reedOn) {
    return MachineRunState::UNKNOWN;
  }
  if (reedOn) {
    return MachineRunState::CONFIRMED_ON;
  }
  if (momentaryLogicalRunActive) {
    return MachineRunState::ASSUMED_ON;
  }
  return MachineRunState::CONFIRMED_OFF;
}
