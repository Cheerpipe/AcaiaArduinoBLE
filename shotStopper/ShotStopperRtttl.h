#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

constexpr uint8_t RTTTL_MAX_NOTES = 16;

struct RtttlNote {
  uint16_t freqHz;
  uint16_t durationMs;
};

inline int rtttlSkipWs(const char *s, int i) {
  while (s[i] == ' ' || s[i] == '\t') {
    ++i;
  }
  return i;
}

inline uint16_t rtttlMidiHz(int midi) {
  if (midi < 0) return 0;
  const double hz = 440.0 * pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
  if (hz < 1.0) return 0;
  if (hz > 20000.0) return 20000;
  return static_cast<uint16_t>(hz + 0.5);
}

// Parses a Nokia RTTTL string into timed notes. freqHz 0 is a rest. Bounded
// parsing rejects overflow instead of silently truncating a custom melody.
inline bool parseRtttlBounded(const char *rtttl, RtttlNote *out,
                              uint8_t capacity, uint8_t &count) {
  count = 0;
  // A null output validates the RTTTL grammar without materializing notes.
  // Keeping validation allocation-free is important while boot migration is
  // already using the bounded internal flash-I/O workspace.
  if (rtttl == nullptr || capacity == 0) return false;
  const char *firstColon = strchr(rtttl, ':');
  if (firstColon == nullptr) return false;
  const char *secondColon = strchr(firstColon + 1, ':');
  if (secondColon == nullptr) return false;

  uint8_t defaultDuration = 4;
  uint8_t defaultOctave = 6;
  uint16_t bpm = 63;
  for (const char *p = firstColon + 1; p < secondColon; ++p) {
    if ((*p == 'd' || *p == 'D') && p[1] == '=') {
      unsigned value = 0;
      p += 2;
      while (p < secondColon && *p >= '0' && *p <= '9') {
        value = value * 10U + static_cast<unsigned>(*p - '0');
        ++p;
      }
      if (value > 0 && value <= 64) defaultDuration = static_cast<uint8_t>(value);
      --p;
    } else if ((*p == 'o' || *p == 'O') && p[1] == '=') {
      unsigned value = 0;
      p += 2;
      while (p < secondColon && *p >= '0' && *p <= '9') {
        value = value * 10U + static_cast<unsigned>(*p - '0');
        ++p;
      }
      if (value >= 4 && value <= 8) defaultOctave = static_cast<uint8_t>(value);
      --p;
    } else if ((*p == 'b' || *p == 'B') && p[1] == '=') {
      unsigned value = 0;
      p += 2;
      while (p < secondColon && *p >= '0' && *p <= '9') {
        value = value * 10U + static_cast<unsigned>(*p - '0');
        ++p;
      }
      if (value >= 25 && value <= 900) bpm = static_cast<uint16_t>(value);
      --p;
    }
  }

  const uint32_t wholeMs = (240000U + (bpm / 2U)) / bpm;
  int i = static_cast<int>(secondColon - rtttl) + 1;
  while (rtttl[i] != '\0') {
    i = rtttlSkipWs(rtttl, i);
    if (rtttl[i] == '\0') break;
    if (rtttl[i] == ',') {
      ++i;
      continue;
    }
    if (count >= capacity) return false;
    unsigned duration = 0;
    while (rtttl[i] >= '0' && rtttl[i] <= '9') {
      duration = duration * 10U + static_cast<unsigned>(rtttl[i] - '0');
      ++i;
    }
    if (duration == 0) duration = defaultDuration;
    char note = rtttl[i];
    if (note >= 'A' && note <= 'Z') note = static_cast<char>(note - 'A' + 'a');
    if (note != 'p' && (note < 'a' || note > 'g')) return false;
    ++i;
    bool sharp = false;
    if (rtttl[i] == '#') {
      sharp = true;
      ++i;
    }
    bool dotted = false;
    if (rtttl[i] == '.') {
      dotted = true;
      ++i;
    }
    uint8_t octave = defaultOctave;
    if (rtttl[i] >= '0' && rtttl[i] <= '9') {
      octave = static_cast<uint8_t>(rtttl[i] - '0');
      ++i;
    }
    if (rtttl[i] == '.') {
      dotted = true;
      ++i;
    }
    uint32_t noteMs = wholeMs / duration;
    if (dotted) noteMs += noteMs / 2U;
    if (noteMs == 0) noteMs = 1;
    uint16_t freqHz = 0;
    if (note != 'p') {
      int semitone = 0;
      switch (note) {
        case 'c': semitone = 0; break;
        case 'd': semitone = 2; break;
        case 'e': semitone = 4; break;
        case 'f': semitone = 5; break;
        case 'g': semitone = 7; break;
        case 'a': semitone = 9; break;
        case 'b': semitone = 11; break;
        default: return false;
      }
      if (sharp) ++semitone;
      const int midi = (static_cast<int>(octave) + 1) * 12 + semitone;
      freqHz = rtttlMidiHz(midi);
    }
    if (out != nullptr) {
      out[count].freqHz = freqHz;
      out[count].durationMs = static_cast<uint16_t>(noteMs);
    }
    ++count;
  }
  return count > 0;
}

inline bool parseRtttl(const char *rtttl, RtttlNote *out, uint8_t &count) {
  return parseRtttlBounded(rtttl, out, RTTTL_MAX_NOTES, count);
}

}  // namespace shotstopper
