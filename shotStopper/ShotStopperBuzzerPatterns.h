#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ShotStopperDomain.h"

namespace shotstopper {

constexpr uint32_t BUZZER_TONE_HZ = 2700;
constexpr uint32_t BUZZER_BEEP_ON_MS = 80;
constexpr uint32_t BUZZER_BEEP_GAP_MS = 70;
constexpr uint32_t BUZZER_SINGLE_ON_MS = 120;
constexpr uint32_t BUZZER_LONG_ON_MS = 800;
constexpr uint32_t BUZZER_RECOVERY_LONG_ON_MS = 1500;
constexpr uint32_t BUZZER_RECOVERY_PULSE_ON_MS = 50;
constexpr uint32_t BUZZER_RECOVERY_PULSE_GAP_MS = 50;
constexpr uint32_t BUZZER_PULSE_TRAIN_ON_MS = 20;
constexpr uint32_t BUZZER_PULSE_TRAIN_PERIOD_MS = 500;
constexpr uint32_t BUZZER_PULSE_TRAIN_GAP_MS =
    BUZZER_PULSE_TRAIN_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_3HZ_PERIOD_MS = 333;
constexpr uint32_t BUZZER_PULSE_TRAIN_3HZ_GAP_MS =
    BUZZER_PULSE_TRAIN_3HZ_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_4HZ_PERIOD_MS = 250;
constexpr uint32_t BUZZER_PULSE_TRAIN_4HZ_GAP_MS =
    BUZZER_PULSE_TRAIN_4HZ_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_5HZ_PERIOD_MS = 200;
constexpr uint32_t BUZZER_PULSE_TRAIN_5HZ_GAP_MS =
    BUZZER_PULSE_TRAIN_5HZ_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_DEBUG_MS = 3000;

struct BuzzerNote {
  uint16_t onMs;
  uint16_t gapMs;
};

// Irregular finite motifs (not equal beeps, not a metronome).
constexpr BuzzerNote BUZZER_CHIME_NOTES[] = {{60, 50}, {60, 80}, {220, 0}};
constexpr BuzzerNote BUZZER_SWING_NOTES[] = {{180, 40}, {50, 40}, {50, 0}};
constexpr BuzzerNote BUZZER_ECHO_NOTES[] = {
    {50, 50}, {50, 220}, {50, 50}, {50, 0}};
// Echo inverted: long bookends with short middle ticks (trailing gap 0).
constexpr BuzzerNote BUZZER_ECHO_INVERTED_NOTES[] = {
    {220, 50}, {50, 50}, {50, 50}, {220, 0}};
constexpr BuzzerNote BUZZER_MORSE_NOTES[] = {{250, 80}, {50, 80}, {250, 0}};
constexpr BuzzerNote BUZZER_SNAP_NOTES[] = {
    {70, 30}, {200, 50}, {70, 30}, {200, 0}};
constexpr BuzzerNote BUZZER_RECOVERY_NETWORK_NOTES[] = {
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, 0}};
constexpr BuzzerNote BUZZER_RECOVERY_FACTORY_NOTES[] = {
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, 0}};
// A deliberately distinct long-short-long failure motif.
constexpr BuzzerNote BUZZER_RECOVERY_ERROR_NOTES[] = {
    {300, 100}, {70, 100}, {300, 0}};

constexpr bool buzzerNotesStartAndEndWithSound(const BuzzerNote *notes,
                                               size_t count) {
  if (notes == nullptr || count == 0) {
    return false;
  }
  if (notes[count - 1].gapMs != 0) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (notes[i].onMs == 0) {
      return false;
    }
  }
  return true;
}

template <size_t N>
constexpr bool buzzerSequenceStartsAndEndsWithSound(
    const BuzzerNote (&notes)[N]) {
  return buzzerNotesStartAndEndWithSound(notes, N);
}

static_assert(BUZZER_BEEP_ON_MS > 0 && BUZZER_SINGLE_ON_MS > 0 &&
                  BUZZER_LONG_ON_MS > 0 && BUZZER_RECOVERY_LONG_ON_MS > 0 &&
                  BUZZER_RECOVERY_PULSE_ON_MS > 0 &&
                  BUZZER_PULSE_TRAIN_ON_MS > 0,
              "fixed-length beeps must start with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_CHIME_NOTES),
              "CHIME must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_SWING_NOTES),
              "SWING must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_ECHO_NOTES),
              "ECHO must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_ECHO_INVERTED_NOTES),
              "ECHO_INVERTED must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_MORSE_NOTES),
              "MORSE must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_SNAP_NOTES),
              "SNAP must start and end with sound");
static_assert(
    buzzerSequenceStartsAndEndsWithSound(BUZZER_RECOVERY_NETWORK_NOTES),
    "RECOVERY_NETWORK_OK must start and end with sound");
static_assert(
    buzzerSequenceStartsAndEndsWithSound(BUZZER_RECOVERY_FACTORY_NOTES),
    "RECOVERY_FACTORY_OK must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_RECOVERY_ERROR_NOTES),
              "RECOVERY_ERROR must start and end with sound");

inline const BuzzerNote *buzzerSequenceNotes(BuzzerPattern pattern,
                                             uint8_t &count) {
  switch (pattern) {
    case BuzzerPattern::CHIME:
      count = static_cast<uint8_t>(sizeof(BUZZER_CHIME_NOTES) /
                                   sizeof(BUZZER_CHIME_NOTES[0]));
      return BUZZER_CHIME_NOTES;
    case BuzzerPattern::SWING:
      count = static_cast<uint8_t>(sizeof(BUZZER_SWING_NOTES) /
                                   sizeof(BUZZER_SWING_NOTES[0]));
      return BUZZER_SWING_NOTES;
    case BuzzerPattern::ECHO:
      count = static_cast<uint8_t>(sizeof(BUZZER_ECHO_NOTES) /
                                   sizeof(BUZZER_ECHO_NOTES[0]));
      return BUZZER_ECHO_NOTES;
    case BuzzerPattern::ECHO_INVERTED:
      count = static_cast<uint8_t>(sizeof(BUZZER_ECHO_INVERTED_NOTES) /
                                   sizeof(BUZZER_ECHO_INVERTED_NOTES[0]));
      return BUZZER_ECHO_INVERTED_NOTES;
    case BuzzerPattern::MORSE:
      count = static_cast<uint8_t>(sizeof(BUZZER_MORSE_NOTES) /
                                   sizeof(BUZZER_MORSE_NOTES[0]));
      return BUZZER_MORSE_NOTES;
    case BuzzerPattern::SNAP:
      count = static_cast<uint8_t>(sizeof(BUZZER_SNAP_NOTES) /
                                   sizeof(BUZZER_SNAP_NOTES[0]));
      return BUZZER_SNAP_NOTES;
    case BuzzerPattern::RECOVERY_NETWORK_OK:
      count = static_cast<uint8_t>(sizeof(BUZZER_RECOVERY_NETWORK_NOTES) /
                                   sizeof(BUZZER_RECOVERY_NETWORK_NOTES[0]));
      return BUZZER_RECOVERY_NETWORK_NOTES;
    case BuzzerPattern::RECOVERY_FACTORY_OK:
      count = static_cast<uint8_t>(sizeof(BUZZER_RECOVERY_FACTORY_NOTES) /
                                   sizeof(BUZZER_RECOVERY_FACTORY_NOTES[0]));
      return BUZZER_RECOVERY_FACTORY_NOTES;
    case BuzzerPattern::RECOVERY_ERROR:
      count = static_cast<uint8_t>(sizeof(BUZZER_RECOVERY_ERROR_NOTES) /
                                   sizeof(BUZZER_RECOVERY_ERROR_NOTES[0]));
      return BUZZER_RECOVERY_ERROR_NOTES;
    default:
      count = 0;
      return nullptr;
  }
}

}  // namespace shotstopper
