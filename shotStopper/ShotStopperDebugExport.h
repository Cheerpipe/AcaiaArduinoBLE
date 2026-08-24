#pragma once

// =============================================================================
// DEBUG EXPORT MAINTENANCE
// =============================================================================
// When adding settings, status fields, state machines, GPIO/inference state,
// or other diagnostically relevant internals, evaluate whether they belong in
// the debug export (DebugExportExtras + debugExportHandler). Bump
// DEBUG_EXPORT_SCHEMA_VERSION when the top-level schema or section shapes
// change materially. Never include secrets (passwords, auth hashes, tokens).
// =============================================================================

#include "ShotStopperDomain.h"
#include "ShotStopperSafety.h"

namespace shotstopper {

constexpr uint32_t DEBUG_EXPORT_SCHEMA_VERSION = 1;
constexpr size_t DEBUG_EXPORT_SHOT_SUMMARY_LIMIT = 10;

// Internals not fully represented on ControlStatusSnapshot / status APIs.
// New fields: consider serializing them in debugExportHandler().
struct DebugExportExtras {
  // Session (CycleSession subset; zeros when inactive).
  bool sessionActive = false;
  bool sessionAutomaticEnabled = false;
  bool sessionBbwProtectionEnabled = false;
  bool sessionBbwProtectionEnded = false;
  bool sessionStartedWithScale = false;
  bool sessionScaleWasLost = false;
  bool sessionCupRemovedPending = false;
  bool sessionExtractionExtended = false;
  bool sessionSlowExtractionExtended = false;
  bool sessionTargetReachedEarly = false;
  bool sessionAutoToManualGuardArmed = false;
  bool sessionAutoToManualGuardEnforced = false;
  bool sessionAccidentalTouchHolding = false;
  uint8_t sessionAccidentalTouchPhase = 0;
  uint8_t sessionAccidentalTouchClass = 0;
  uint8_t sessionAccidentalTouchPendingCount = 0;
  uint8_t sessionActivePresetId = 0;
  uint8_t sessionWeightControlState = 0;
  uint32_t sessionId = 0;
  uint32_t sessionStartedAtMs = 0;
  uint32_t sessionFirstDropMs = 0;
  uint32_t sessionAutoToManualGuardDeadlineAtMs = 0;
  uint32_t sessionTargetReachedAtMs = 0;

  // Cup presence FSM.
  uint8_t cupState = 0;
  bool cupHoldTransitions = false;
  bool cupInNegativeHole = false;
  bool cupRemovedArmed = false;
  uint8_t cupRemovedConfirmations = 0;
  uint8_t cupPlaceStabilitySamples = 0;
  float cupHoleWeightG = 0.0f;
  float cupPlaceCandidateWeightG = 0.0f;

  // Scale link (CLI SCALE_STATUS parity).
  uint8_t scaleLinkState = 0;
  uint32_t scaleDisconnectSequence = 0;
  uint32_t scaleConnectionGeneration = 0;
  uint32_t scalePacketSequence = 0;
  uint32_t scaleWorkerProgressAtMs = 0;
  bool scaleTimerValid = false;
  uint32_t scaleTimerMs = 0;
  uint32_t scaleTimerAgeMs = 0;
  char scaleProtocolName[20] = "none";

  // Relay safety full snapshot.
  RelaySafetySnapshot relay = {};

  // Machine / GPIO.
  bool rawActivatorOn = false;
  bool physicalActivatorOn = false;
  uint8_t machineRunState = 0;
  bool machineStartAckPending = false;
  bool machineStopAckPending = false;
  bool machineOrphanRun = false;

  // Health alert latches.
  bool healthHeapAlertLatched = false;
  bool healthStackAlertLatched = false;
  bool healthLoopGapAlertLatched = false;

  // Guard holds.
  bool noScaleShotGuardHold = false;
  bool noScaleShotGuardScaleWasAvailable = false;
  bool cupStartGuardHold = false;
};

inline const char *debugExportScaleLinkStateName(uint8_t state) {
  return state == 1 ? "CONNECTED" : "DISCONNECTED";
}

}  // namespace shotstopper
