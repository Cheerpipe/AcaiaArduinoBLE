#pragma once

#include <cmath>
#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

namespace shotstopper {

struct HwmonSnapshot {
  uint8_t cpuUsagePct = 0;
  float tempC = 0.0f;
  float tempPeakC = 0.0f;
  bool tempValid = false;
  uint32_t ramTotalBytes = 0;
  uint32_t ramUsedBytes = 0;
  uint32_t ramFreeBytes = 0;
};

class Hwmon {
 public:
  void noteLoopBusyMs(uint32_t busyMs) { loopBusyMsAccum_ += busyMs; }

  HwmonSnapshot sample(uint32_t intervalMs) {
    HwmonSnapshot out;
    if (intervalMs > 0U) {
      const uint32_t pct = (loopBusyMsAccum_ * 100U) / intervalMs;
      out.cpuUsagePct =
          pct > 100U ? 100U : static_cast<uint8_t>(pct);
    }
    loopBusyMsAccum_ = 0;

#ifndef ARDUINO
    out.tempValid = true;
    out.tempC = 42.0f;
    if (!tempPeakValid_ || out.tempC > tempPeakC_) {
      tempPeakC_ = out.tempC;
      tempPeakValid_ = true;
    }
    out.tempPeakC = tempPeakC_;
    out.ramTotalBytes = 327680U;
    out.ramFreeBytes = 200000U;
    out.ramUsedBytes = out.ramTotalBytes - out.ramFreeBytes;
#else
    out.ramFreeBytes = static_cast<uint32_t>(ESP.getFreeHeap());
    out.ramTotalBytes = static_cast<uint32_t>(ESP.getHeapSize());
    if (out.ramTotalBytes >= out.ramFreeBytes) {
      out.ramUsedBytes = out.ramTotalBytes - out.ramFreeBytes;
    }

    const float temp = temperatureRead();
    if (std::isfinite(temp) && temp >= -40.0f && temp <= 125.0f) {
      out.tempValid = true;
      out.tempC = temp;
      if (!tempPeakValid_ || temp > tempPeakC_) {
        tempPeakC_ = temp;
        tempPeakValid_ = true;
      }
      out.tempPeakC = tempPeakC_;
    } else if (tempPeakValid_) {
      out.tempPeakC = tempPeakC_;
    }
#endif
    return out;
  }

 private:
  uint32_t loopBusyMsAccum_ = 0;
  float tempPeakC_ = 0.0f;
  bool tempPeakValid_ = false;
};

}  // namespace shotstopper
