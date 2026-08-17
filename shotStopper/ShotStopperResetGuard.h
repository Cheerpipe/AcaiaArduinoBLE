#pragma once

#include <stdint.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <esp_attr.h>
#include <esp_system.h>
#endif

namespace shotstopper {

constexpr uint32_t SAFETY_RESET_RECORD_MAGIC = 0x534E3947UL;
constexpr uint32_t SAFETY_RELAY_OPEN_MARKER = 0x4F50454EUL;
constexpr uint32_t SAFETY_RELAY_CLOSED_MARKER = 0x434C5344UL;
constexpr uint32_t SAFETY_BOOT_LOOP_THRESHOLD = 3;

struct SafetyResetRecord {
  uint32_t magic;
  uint32_t magicInverse;
  uint32_t relayMarker;
  uint32_t relayMarkerInverse;
  uint32_t unsafeResetCount;
  uint32_t unsafeResetCountInverse;
};

struct SafetyResetSnapshot {
  uint32_t reasonCode = 0;
  uint32_t unsafeResetCount = 0;
  bool resetDuringClose = false;
  bool unsafeReset = false;
  bool bootLoopDetected = false;
  bool recoveryRequired = false;
};

#ifndef SHOT_STOPPER_HOST_TEST
RTC_NOINIT_ATTR static volatile SafetyResetRecord safetyResetRecord;
#else
static volatile SafetyResetRecord safetyResetRecord;
#endif

inline bool safetyResetRecordValid() {
  return safetyResetRecord.magic == SAFETY_RESET_RECORD_MAGIC &&
         safetyResetRecord.magicInverse == ~SAFETY_RESET_RECORD_MAGIC &&
         safetyResetRecord.relayMarkerInverse ==
             ~safetyResetRecord.relayMarker &&
         safetyResetRecord.unsafeResetCountInverse ==
             ~safetyResetRecord.unsafeResetCount &&
         (safetyResetRecord.relayMarker == SAFETY_RELAY_OPEN_MARKER ||
          safetyResetRecord.relayMarker == SAFETY_RELAY_CLOSED_MARKER);
}

inline void initializeSafetyResetRecord(
    uint32_t relayMarker, uint32_t unsafeResetCount) {
  safetyResetRecord.magic = 0;
  safetyResetRecord.magicInverse = 0;
  safetyResetRecord.relayMarker = relayMarker;
  safetyResetRecord.relayMarkerInverse = ~relayMarker;
  safetyResetRecord.unsafeResetCount = unsafeResetCount;
  safetyResetRecord.unsafeResetCountInverse = ~unsafeResetCount;
  safetyResetRecord.magicInverse = ~SAFETY_RESET_RECORD_MAGIC;
  safetyResetRecord.magic = SAFETY_RESET_RECORD_MAGIC;
}

inline void recordRelayCommandedClosed(bool closed) {
  // setup() initializes the record before CN9 can close. The fallback keeps
  // host/fault-injection calls deterministic without adding work to the ISR.
  if (!safetyResetRecordValid()) {
    initializeSafetyResetRecord(SAFETY_RELAY_OPEN_MARKER, 0);
  }
  const uint32_t marker = closed ? SAFETY_RELAY_CLOSED_MARKER
                                 : SAFETY_RELAY_OPEN_MARKER;
  safetyResetRecord.relayMarkerInverse = ~marker;
  safetyResetRecord.relayMarker = marker;
}

inline uint32_t currentSafetyResetReasonCode() {
#ifdef SHOT_STOPPER_HOST_TEST
  return hostSafetyResetReasonCode;
#else
  return static_cast<uint32_t>(esp_reset_reason());
#endif
}

inline bool currentSafetyResetIsUnsafe() {
#ifdef SHOT_STOPPER_HOST_TEST
  return hostSafetyResetReasonUnsafe;
#else
  switch (esp_reset_reason()) {
    case ESP_RST_UNKNOWN:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
    case ESP_RST_EFUSE:
    case ESP_RST_PWR_GLITCH:
    case ESP_RST_CPU_LOCKUP:
      return true;
    case ESP_RST_POWERON:
    case ESP_RST_EXT:
    case ESP_RST_SW:
    case ESP_RST_DEEPSLEEP:
    case ESP_RST_SDIO:
    case ESP_RST_USB:
    case ESP_RST_JTAG:
      return false;
  }
  return true;
#endif
}

inline bool currentSafetyResetIsPowerOn() {
#ifdef SHOT_STOPPER_HOST_TEST
  return hostSafetyResetReasonPowerOn;
#else
  return esp_reset_reason() == ESP_RST_POWERON;
#endif
}

inline const char *safetyResetReasonName(uint32_t code) {
  switch (code) {
    case 0: return "Unknown";
    case 1: return "Power-on";
    case 2: return "External";
    case 3: return "Software";
    case 4: return "Panic";
    case 5: return "Interrupt WDT";
    case 6: return "Task WDT";
    case 7: return "Watchdog";
    case 8: return "Deep sleep";
    case 9: return "Brownout";
    case 10: return "SDIO";
    case 11: return "USB";
    case 12: return "JTAG";
    case 13: return "eFuse";
    case 14: return "Power glitch";
    case 15: return "CPU lockup";
  }
  return "Unknown";
}

inline SafetyResetSnapshot beginSafetyResetGuard() {
  SafetyResetSnapshot snapshot;
  const bool valid = safetyResetRecordValid();
  const bool resetDuringClose =
      valid &&
      safetyResetRecord.relayMarker == SAFETY_RELAY_CLOSED_MARKER;
  const uint32_t previousCount =
      valid ? safetyResetRecord.unsafeResetCount : 0;

  snapshot.reasonCode = currentSafetyResetReasonCode();
  snapshot.resetDuringClose = resetDuringClose;
  // A torn or corrupted redundant record is unsafe after any warm reset. A
  // real power-on may legitimately start with undefined RTC no-init memory.
  snapshot.unsafeReset = currentSafetyResetIsUnsafe() || resetDuringClose ||
                         (!valid && !currentSafetyResetIsPowerOn());
  snapshot.unsafeResetCount =
      snapshot.unsafeReset && previousCount < UINT32_MAX
          ? previousCount + 1
          : (snapshot.unsafeReset ? previousCount : 0);
  snapshot.bootLoopDetected =
      snapshot.unsafeResetCount >= SAFETY_BOOT_LOOP_THRESHOLD;
  // Reset history is diagnostic only. setup() has already forced the relay
  // output OPEN, so a panic or reset-during-close must not latch the next
  // boot or require a local recovery gesture.
  snapshot.recoveryRequired = false;

  // The physical relay has already been driven OPEN by setup() before this
  // function is called. Publish that safe fact while retaining crash count.
  initializeSafetyResetRecord(SAFETY_RELAY_OPEN_MARKER,
                              snapshot.unsafeResetCount);
  return snapshot;
}

#ifdef SHOT_STOPPER_HOST_TEST
inline void resetSafetyResetGuardForHost() {
  safetyResetRecord.magic = 0;
  safetyResetRecord.magicInverse = 0;
  safetyResetRecord.relayMarker = 0;
  safetyResetRecord.relayMarkerInverse = 0;
  safetyResetRecord.unsafeResetCount = 0;
  safetyResetRecord.unsafeResetCountInverse = 0;
  hostSafetyResetReasonCode = 1;
  hostSafetyResetReasonUnsafe = false;
  hostSafetyResetReasonPowerOn = true;
}
#endif

}  // namespace shotstopper
