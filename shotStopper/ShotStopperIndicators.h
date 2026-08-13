#pragma once

#include <stdint.h>

#include "ShotStopperDomain.h"

namespace shotstopper {

struct IndicatorColor {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
};

constexpr bool operator==(const IndicatorColor &left,
                          const IndicatorColor &right) {
  return left.red == right.red && left.green == right.green &&
         left.blue == right.blue;
}

constexpr bool operator!=(const IndicatorColor &left,
                          const IndicatorColor &right) {
  return !(left == right);
}

enum class IndicatorPattern : uint8_t {
  OFF,
  SOLID,
  SLOW_BLINK,
  MEDIUM_BLINK,
  FAST_BLINK
};

struct IndicatorSignal {
  IndicatorColor color;
  IndicatorPattern pattern = IndicatorPattern::OFF;
};

enum class ScaleIndicatorCondition : uint8_t {
  STARTING,
  AVAILABLE,
  DISCONNECTED,
  STALE,
  FAULT
};

constexpr IndicatorColor INDICATOR_OFF = {0, 0, 0};
constexpr IndicatorColor INDICATOR_GREEN = {0, 255, 0};
constexpr IndicatorColor INDICATOR_SALMON = {250, 128, 114};
constexpr IndicatorColor INDICATOR_AMBER = {255, 140, 0};
constexpr IndicatorColor INDICATOR_YELLOW = {255, 255, 0};
constexpr IndicatorColor INDICATOR_RED = {255, 0, 0};
constexpr IndicatorColor INDICATOR_BLUE = {0, 96, 255};

constexpr uint32_t INDICATOR_SLOW_HALF_PERIOD_MS = 750;
constexpr uint32_t INDICATOR_MEDIUM_HALF_PERIOD_MS = 300;
constexpr uint32_t INDICATOR_FAST_HALF_PERIOD_MS = 125;

inline IndicatorSignal scaleIndicatorSignal(
    ScaleIndicatorCondition condition) {
  switch (condition) {
    case ScaleIndicatorCondition::STARTING:
      return {INDICATOR_BLUE, IndicatorPattern::SLOW_BLINK};
    case ScaleIndicatorCondition::AVAILABLE:
      return {INDICATOR_GREEN, IndicatorPattern::SOLID};
    case ScaleIndicatorCondition::DISCONNECTED:
      return {INDICATOR_RED, IndicatorPattern::SOLID};
    case ScaleIndicatorCondition::STALE:
      return {INDICATOR_YELLOW, IndicatorPattern::SLOW_BLINK};
    case ScaleIndicatorCondition::FAULT:
      return {INDICATOR_RED, IndicatorPattern::FAST_BLINK};
  }
  return {INDICATOR_RED, IndicatorPattern::FAST_BLINK};
}

inline IndicatorSignal stopperIndicatorSignal(
    StopperState stopperState, RelaySafetyState safetyState,
    bool safetySubsystemReady, bool maintenanceActive,
    bool manualPalette) {
  if (safetyState == RelaySafetyState::BOOT_SAFE) {
    return {INDICATOR_BLUE, IndicatorPattern::SLOW_BLINK};
  }
  if (safetyState == RelaySafetyState::LOCKOUT ||
      safetyState == RelaySafetyState::TRIPPED || !safetySubsystemReady) {
    return {INDICATOR_RED, IndicatorPattern::FAST_BLINK};
  }
  if (maintenanceActive) {
    return {INDICATOR_BLUE, IndicatorPattern::SLOW_BLINK};
  }

  const IndicatorColor operatingColor =
      manualPalette ? INDICATOR_SALMON : INDICATOR_GREEN;
  switch (stopperState) {
    case StopperState::REQUIRES_OFF:
      return {INDICATOR_AMBER, IndicatorPattern::SLOW_BLINK};
    case StopperState::READY:
      return {operatingColor, IndicatorPattern::SOLID};
    case StopperState::BREW:
      return {operatingColor, IndicatorPattern::SLOW_BLINK};
    case StopperState::RINSE:
      return {operatingColor, IndicatorPattern::FAST_BLINK};
    case StopperState::MANUAL_NO_SCALE:
      return {INDICATOR_SALMON, IndicatorPattern::SLOW_BLINK};
  }
  return {INDICATOR_RED, IndicatorPattern::FAST_BLINK};
}

inline bool indicatorPatternIsLit(IndicatorPattern pattern,
                                  uint32_t nowMs) {
  switch (pattern) {
    case IndicatorPattern::OFF:
      return false;
    case IndicatorPattern::SOLID:
      return true;
    case IndicatorPattern::SLOW_BLINK:
      return (nowMs / INDICATOR_SLOW_HALF_PERIOD_MS) % 2U == 0U;
    case IndicatorPattern::MEDIUM_BLINK:
      return (nowMs / INDICATOR_MEDIUM_HALF_PERIOD_MS) % 2U == 0U;
    case IndicatorPattern::FAST_BLINK:
      return (nowMs / INDICATOR_FAST_HALF_PERIOD_MS) % 2U == 0U;
  }
  return false;
}

inline IndicatorColor renderIndicatorSignal(const IndicatorSignal &signal,
                                             uint32_t nowMs) {
  return indicatorPatternIsLit(signal.pattern, nowMs) ? signal.color
                                                       : INDICATOR_OFF;
}

}  // namespace shotstopper
