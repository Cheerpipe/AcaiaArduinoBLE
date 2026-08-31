#pragma once

#include <stdint.h>

namespace shotstopper {

constexpr uint8_t RESET_HISTORY_CAPACITY = 10;

struct ResetHistoryEntry {
  uint32_t reasonCode = 0;
  uint32_t uptimeMs = 0;
};

}  // namespace shotstopper
