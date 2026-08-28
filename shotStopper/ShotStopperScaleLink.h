#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ShotStopperScaleTypes.h"

#if defined(SHOT_STOPPER_HOST_TEST)
#include "../libraries/EspressoScaleBLE/src/ScaleFeatures.h"
#else
#include <ScaleFeatures.h>
#endif

namespace shotstopper {

// =============================================================================
// LAYER: Scale link port (worker ↔ orchestrator)
// =============================================================================
// WHAT: Command/event/snapshot types for the BLE scale worker. The worker
//       owns the radio, ATT writes, and latest-wins weight mailbox. The
//       orchestrator consumes events and decides brew.
//
// BOUNDARY: No CycleSession, stopper state, cup, or brew policy.

enum class ScaleLinkState : uint8_t { DISCONNECTED, CONNECTED };

enum class ScaleCommandType : uint8_t {
  START_TIMER_AND_TARE,
  TARE_ONLY,
  STOP_TIMER
};

enum class ScaleEventType : uint8_t {
  WEIGHT,
  TIMER_START_RESULT,
  TARE_RESULT,
  TIMER_STOP_RESULT
};

struct ScaleCommand {
  ScaleCommandType type = ScaleCommandType::STOP_TIMER;
  uint32_t cycleId = 0;
  bool autoTare = false;
  bool canTareStartTimer = false;
  bool commandFeedbackExpected = false;
};

struct ScaleEvent {
  ScaleEventType type = ScaleEventType::WEIGHT;
  uint32_t cycleId = 0;
  uint32_t receivedAtMs = 0;
  uint32_t connectionGeneration = 0;
  uint32_t packetSequence = 0;
  float weightG = 0.0f;
  bool commandAttempted = false;
  bool writeSucceeded = false;
  bool usedCombinedTareStart = false;
  bool commandFeedbackExpected = false;
};

struct ScaleLinkSnapshot {
  ScaleLinkState state;
  bool connecting;
  uint32_t disconnectSequence;
  uint32_t connectionGeneration;
  uint32_t packetSequence;
  uint32_t packetGaps;
  uint32_t rejectedPackets;
  uint32_t reconnects;
  uint8_t lastDisconnectReason;
  uint32_t workerProgressAtMs;
  bool timerValid;
  uint32_t timerMs;
  uint32_t timerAgeMs;
  char protocolName[20];
  ScaleFeatureSet features;
};

}  // namespace shotstopper
