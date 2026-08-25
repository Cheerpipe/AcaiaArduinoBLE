#pragma once

// =============================================================================
// SPECIALIZATION: Momentary + reed — run view (state)
// =============================================================================
// WHAT: Outside an assumed window, the reed is canonical: OFF → CONFIRMED_OFF,
//       ON → CONFIRMED_ON. Boot with the reed already ON is CONFIRMED_ON.
//       K1 TRIPPED/LOCKOUT does not hide the reed. Firmware auto-cut still
//       requires a stable OFF earlier this boot (reedSawStableOff) so a
//       stuck-ON reed is not pulsed.
//
//       ASSUMED_ON/OFF is the short window after the shot start/stop edge — the
//       same press or release chosen by momentaryStartOnPress — where machine
//       may disagree with reed (solenoid lag). The confirm-timeout clock starts
//       at that logical edge, not at the 1:1 relay close. If reed matches the
//       expected level, confirm immediately. If the timeout elapses, confirm
//       the actual reed.
//
//       GRACE_ON/OFF is the same timeout after a press that exceeded
//       Single-press limit: machine holds the pre-press run/idle view, then
//       reed is canonical.
//
// BOUNDARY: This file alone determines momentary+reed run state. Reed GPIO and
// assume windows stay here. Brew/stopper/guards must not sample reed or know
// ReedAssume — only machineRunState() / façade flags. No paddle latch policy.

enum class ReedAssume : uint8_t {
  NONE = 0,
  ON = 1,
  OFF = 2,
  GRACE_ON = 3,
  GRACE_OFF = 4
};

bool reedRawOn = false;
bool reedOn = false;
uint32_t reedChangedAtMs = 0;
uint32_t reedOnAtMs = 0;
bool reedSawStableOff = false;
bool momentaryStopRetryPending = false;
bool momentaryFirmwareStopIssued = false;
ReedAssume reedAssume = ReedAssume::NONE;
uint32_t reedAssumeAtMs = 0;

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

void resetReedRuntime() {
  reedRawOn = false;
  reedOn = false;
  reedSawStableOff = false;
  reedAssume = ReedAssume::NONE;
  reedChangedAtMs = millis();
  momentaryFirmwareStopIssued = false;
  momentaryStopRetryPending = false;
}

void clearReedAssume() { reedAssume = ReedAssume::NONE; }

void beginReedAssume(ReedAssume assume) {
  reedAssume = assume;
  reedAssumeAtMs = millis();
}

void noteMomentaryLogicalStart() {
  // Arm ASSUMED_ON here: this is the shot-start edge (press or release).
  momentaryStopRetryPending = false;
  momentaryFirmwareStopIssued = false;
  sampleReed();
  if (reedOn) {
    clearReedAssume();
  } else {
    beginReedAssume(ReedAssume::ON);
  }
}
void noteMomentaryLogicalStop() {
  // Do not clobber a single-press grace window (finalizeCycle also stops).
  if (reedAssume == ReedAssume::GRACE_ON ||
      reedAssume == ReedAssume::GRACE_OFF) {
    return;
  }
  momentaryStopRetryPending = false;
  momentaryFirmwareStopIssued = true;
  sampleReed();
  if (!reedOn) {
    clearReedAssume();
  } else {
    beginReedAssume(ReedAssume::OFF);
  }
}
void noteMomentaryLogicalStartCanceled() {
  momentaryStopRetryPending = false;
  momentaryFirmwareStopIssued = false;
  clearReedAssume();
}

void noteMomentaryDisqualifiedPress(bool restoreRunning) {
  momentaryStopRetryPending = false;
  momentaryFirmwareStopIssued = false;
  sampleReed();
  beginReedAssume(restoreRunning ? ReedAssume::GRACE_ON : ReedAssume::GRACE_OFF);
}

void serviceReedAssumeWindow() {
  sampleReed();
  if (reedAssume == ReedAssume::NONE) {
    return;
  }
  if (reedAssume == ReedAssume::ON) {
    if (reedOn) {
      clearReedAssume();
      return;
    }
    if (elapsedMs(reedAssumeAtMs) >= runtimeReedConfirmTimeoutMs(runtimeConfig)) {
      clearReedAssume();
      momentaryLogicalRunActive = false;
      activatorTurnedOff = true;
    }
    return;
  }
  if (reedAssume == ReedAssume::GRACE_ON ||
      reedAssume == ReedAssume::GRACE_OFF) {
    if (elapsedMs(reedAssumeAtMs) >= runtimeReedConfirmTimeoutMs(runtimeConfig)) {
      clearReedAssume();
    }
    return;
  }
  if (reedOn) {
    if (elapsedMs(reedAssumeAtMs) >= runtimeReedConfirmTimeoutMs(runtimeConfig)) {
      clearReedAssume();
    }
    return;
  }
  clearReedAssume();
}

void serviceMomentaryRunSensors() { serviceReedAssumeWindow(); }

bool machineAllowsFirmwareStopPulse() {
  sampleReed();
  if (momentaryPhysicalOn) {
    return false;
  }
  if (momentaryFirmwareStopIssued && !momentaryUserStopThisCycle) {
    return false;
  }
  return reedOn && reedSawStableOff && reedAssume != ReedAssume::OFF &&
         reedAssume != ReedAssume::GRACE_OFF;
}

inline bool machineRunningElapsed(uint32_t &elapsedOut) {
  sampleReed();
  if (reedAssume == ReedAssume::GRACE_OFF) {
    elapsedOut = 0U;
    return false;
  }
  if (reedOn) {
    elapsedOut = elapsedMs(reedOnAtMs);
    return true;
  }
  if (reedAssume == ReedAssume::ON || reedAssume == ReedAssume::GRACE_ON ||
      momentaryLogicalRunActive) {
    elapsedOut = elapsedMs(momentaryLogicalRunStartedAtMs);
    return true;
  }
  elapsedOut = 0U;
  return false;
}

inline bool machineIsRunning() {
  sampleReed();
  if (reedAssume == ReedAssume::GRACE_OFF) {
    return false;
  }
  if (reedAssume == ReedAssume::GRACE_ON) {
    return true;
  }
  return reedOn || reedAssume == ReedAssume::ON || momentaryLogicalRunActive;
}

inline uint32_t machineElapsedMs() {
  uint32_t elapsed = 0U;
  if (machineRunningElapsed(elapsed)) {
    return elapsed;
  }
  return momentaryElapsedLatched ? momentaryLatchedElapsedMs : 0U;
}

inline MachineRunState machineRunState() {
  sampleReed();
  if ((reedAssume == ReedAssume::ON || reedAssume == ReedAssume::GRACE_ON) &&
      !reedOn) {
    return MachineRunState::ASSUMED_ON;
  }
  if ((reedAssume == ReedAssume::OFF || reedAssume == ReedAssume::GRACE_OFF) &&
      reedOn) {
    return MachineRunState::ASSUMED_OFF;
  }
  if (reedOn) {
    return MachineRunState::CONFIRMED_ON;
  }
  return MachineRunState::CONFIRMED_OFF;
}

inline void machineFillInferenceStatus(ControlStatusSnapshot &status) {
  status.reedOn = reedOn;
  status.machineStartAckPending =
      reedAssume == ReedAssume::ON || reedAssume == ReedAssume::GRACE_ON;
  status.machineStopAckPending =
      reedAssume == ReedAssume::OFF || reedAssume == ReedAssume::GRACE_OFF;
  status.machineOrphanRun = false;
}
