#pragma once

#include "ShotStopperScaleTypes.h"

// =============================================================================
// LAYER: Cup (presence FSM)
// =============================================================================
// WHAT: PRESENT / ABSENT from weight hysteresis on the scale stream. Tare does
//       not change state. No heap; state is BSS.
//
// BOUNDARY: Scale-weight sensing only. Must not talk to machine, paddle,
// momentary, reed, or brew actuators. Emits CupPresenceEvent; the stopper
// decides whether REMOVED cuts the shot. No MachineType / paddle details.

namespace {

struct CupPresenceRuntime {
  CupPresenceState state = CupPresenceState::ABSENT;
  bool holdTransitions = false;
  bool inNegativeHole = false;
  bool removedArmed = false;
  uint8_t removedConfirmations = 0;
  uint8_t placeStabilitySamples = 0;
  uint32_t lastRemovedAtMs = 0;
  uint32_t lastRemovedPacketSequence = 0;
  uint32_t placeStabilityStartedAtMs = 0;
  uint32_t placeLastSampleAtMs = 0;
  float holeWeightG = 0.0f;
  float placeCandidateWeightG = 0.0f;
};

CupPresenceRuntime cupPresence;

void resetCupPlaceStabilityStreak() {
  cupPresence.placeStabilitySamples = 0;
  cupPresence.placeStabilityStartedAtMs = 0;
  cupPresence.placeLastSampleAtMs = 0;
  cupPresence.placeCandidateWeightG = 0.0f;
}

}  // namespace

CupPresenceState cupPresenceState() { return cupPresence.state; }

void resetCupPresence() {
  cupPresence = CupPresenceRuntime{};
}

void holdCupPresenceTransitions(bool hold) {
  cupPresence.holdTransitions = hold;
  resetCupPlaceStabilityStreak();
  cupPresence.removedConfirmations = 0;
  cupPresence.lastRemovedAtMs = 0;
  cupPresence.lastRemovedPacketSequence = 0;
}

void notifyCupPresenceTare() {
  cupPresence.inNegativeHole = false;
  cupPresence.holeWeightG = 0.0f;
  resetCupPlaceStabilityStreak();
}

CupPresenceEvent feedCupPresence(float weight, uint32_t receivedAtMs,
                                 uint32_t packetSequence) {
  if (!isfinite(weight)) {
    return CupPresenceEvent::NONE;
  }

  const float minCupG = runtimeConfig.minimumCupWeightG;
  const float removedG = runtimeConfig.cupRemovedWeightG;

  if (weight > removedG) {
    cupPresence.removedArmed = true;
    cupPresence.removedConfirmations = 0;
    cupPresence.lastRemovedAtMs = 0;
    cupPresence.lastRemovedPacketSequence = 0;
  }

  if (cupPresence.holdTransitions) {
    return CupPresenceEvent::NONE;
  }

  if (cupPresence.state == CupPresenceState::PRESENT) {
    if (weight > removedG) {
      resetCupPlaceStabilityStreak();
      return CupPresenceEvent::NONE;
    }
    if (!cupPresence.removedArmed) {
      return CupPresenceEvent::NONE;
    }

    const bool consecutive = cupPresence.removedConfirmations > 0 &&
        static_cast<int32_t>(receivedAtMs - cupPresence.lastRemovedAtMs) >= 0 &&
        static_cast<uint32_t>(receivedAtMs - cupPresence.lastRemovedAtMs) <=
            DIRECT_STOP_CONFIRMATION_WINDOW_MS &&
        (packetSequence == 0 || cupPresence.lastRemovedPacketSequence == 0 ||
         packetSequence == cupPresence.lastRemovedPacketSequence + 1U);
    cupPresence.removedConfirmations = consecutive
        ? static_cast<uint8_t>(cupPresence.removedConfirmations + 1U)
        : 1U;
    cupPresence.lastRemovedAtMs = receivedAtMs;
    cupPresence.lastRemovedPacketSequence = packetSequence;

    if (cupPresence.removedConfirmations < DIRECT_STOP_CONFIRMATION_SAMPLES) {
      return CupPresenceEvent::NONE;
    }

    cupPresence.state = CupPresenceState::ABSENT;
    cupPresence.inNegativeHole = true;
    cupPresence.holeWeightG = weight;
    cupPresence.removedConfirmations = 0;
    resetCupPlaceStabilityStreak();
    return CupPresenceEvent::REMOVED;
  }

  if (cupPresence.inNegativeHole && weight < cupPresence.holeWeightG) {
    cupPresence.holeWeightG = weight;
  }

  const bool placeCandidate = weight >= minCupG;
  const bool putBackCandidate =
      cupPresence.inNegativeHole &&
      (weight - cupPresence.holeWeightG) >= minCupG;
  if (!placeCandidate && !putBackCandidate) {
    resetCupPlaceStabilityStreak();
    return CupPresenceEvent::NONE;
  }

  if (cupPresence.placeStabilitySamples == 0) {
    cupPresence.placeCandidateWeightG = weight;
    cupPresence.placeStabilitySamples = 1;
    cupPresence.placeStabilityStartedAtMs = receivedAtMs;
    cupPresence.placeLastSampleAtMs = receivedAtMs;
    return CupPresenceEvent::NONE;
  }

  if (static_cast<uint32_t>(receivedAtMs - cupPresence.placeLastSampleAtMs) >
          runtimeConfig.retareStabilityMaxGapMs ||
      fabsf(weight - cupPresence.placeCandidateWeightG) >
          runtimeConfig.retareStabilityToleranceG) {
    cupPresence.placeCandidateWeightG = weight;
    cupPresence.placeStabilitySamples = 1;
    cupPresence.placeStabilityStartedAtMs = receivedAtMs;
    cupPresence.placeLastSampleAtMs = receivedAtMs;
    return CupPresenceEvent::NONE;
  }

  cupPresence.placeCandidateWeightG = weight;
  cupPresence.placeLastSampleAtMs = receivedAtMs;
  if (cupPresence.placeStabilitySamples < UINT8_MAX) {
    ++cupPresence.placeStabilitySamples;
  }
  const uint32_t stableDurationMs = static_cast<uint32_t>(
      receivedAtMs - cupPresence.placeStabilityStartedAtMs);
  const bool samplesMet = cupPresence.placeStabilitySamples >=
                          runtimeConfig.retareStabilitySamples;
  const bool durationMet =
      runtimeConfig.retareStabilityMinDurationMs == 0U ||
      stableDurationMs >= runtimeConfig.retareStabilityMinDurationMs;
  if (!samplesMet || !durationMet) {
    return CupPresenceEvent::NONE;
  }

  const bool placedByWeight =
      cupPresence.placeCandidateWeightG >= minCupG;
  const bool placedByPutBack = cupPresence.inNegativeHole &&
      (cupPresence.placeCandidateWeightG - cupPresence.holeWeightG) >= minCupG;
  if (!placedByWeight && !placedByPutBack) {
    return CupPresenceEvent::NONE;
  }

  cupPresence.state = CupPresenceState::PRESENT;
  cupPresence.inNegativeHole = false;
  cupPresence.holeWeightG = 0.0f;
  cupPresence.removedArmed = true;
  resetCupPlaceStabilityStreak();
  return CupPresenceEvent::PLACED;
}
