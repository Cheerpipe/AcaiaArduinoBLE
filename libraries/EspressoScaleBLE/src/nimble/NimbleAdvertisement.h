#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t NIMBLE_SCALE_ADV_NAME_CAPACITY = 32;

struct NimbleAdvertisementData {
  char name[NIMBLE_SCALE_ADV_NAME_CAPACITY];
  bool namePresent;
  bool nameComplete;
  bool compatibleUuid16;
};

// Shared accumulation primitives used by the native NimBLE adapter after
// ble_hs_adv_parse_fields() and by the portable TLV tests below.
void nimbleAccumulateAdvertisementName(const uint8_t *value, size_t length,
                                       bool complete,
                                       NimbleAdvertisementData &out);
void nimbleAccumulateAdvertisementUuid16(uint16_t uuid,
                                         NimbleAdvertisementData &out);

// Validates the complete BLE AD TLV stream before updating `out`. Multiple
// calls intentionally merge ADV and scan-response payloads for one address.
bool nimbleAccumulateAdvertisement(const uint8_t *data, size_t length,
                                   NimbleAdvertisementData &out);

bool nimbleAdvertisementIsCompatible(const NimbleAdvertisementData &data);
