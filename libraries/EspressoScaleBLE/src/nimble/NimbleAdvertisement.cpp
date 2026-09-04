#include "nimble/NimbleAdvertisement.h"

#include "ScaleProtocol.h"

#include <string.h>

namespace {

constexpr uint8_t kAdTypeUuid16Incomplete = 0x02;
constexpr uint8_t kAdTypeUuid16Complete = 0x03;
constexpr uint8_t kAdTypeShortName = 0x08;
constexpr uint8_t kAdTypeCompleteName = 0x09;

bool validTlvStream(const uint8_t *data, size_t length) {
  if (data == nullptr && length != 0) {
    return false;
  }
  size_t offset = 0;
  while (offset < length) {
    const size_t fieldLength = data[offset];
    if (fieldLength == 0) {
      return offset + 1 == length;
    }
    if (fieldLength > length - offset - 1) {
      return false;
    }
    offset += fieldLength + 1;
  }
  return offset == length;
}

void accumulateUuid16(const uint8_t *value, size_t length,
                      NimbleAdvertisementData &out) {
  for (size_t offset = 0; offset + 1 < length; offset += 2) {
    const uint16_t uuid = static_cast<uint16_t>(value[offset]) |
                          static_cast<uint16_t>(value[offset + 1]) << 8;
    nimbleAccumulateAdvertisementUuid16(uuid, out);
  }
}

}  // namespace

void nimbleAccumulateAdvertisementName(const uint8_t *value, size_t length,
                                       bool complete,
                                       NimbleAdvertisementData &out) {
  if (value == nullptr || length == 0 || (out.nameComplete && !complete)) {
    return;
  }
  size_t copyLength = length;
  if (copyLength >= sizeof(out.name)) {
    copyLength = sizeof(out.name) - 1;
  }
  memcpy(out.name, value, copyLength);
  out.name[copyLength] = '\0';
  out.namePresent = true;
  out.nameComplete = complete;
}

void nimbleAccumulateAdvertisementUuid16(uint16_t uuid,
                                         NimbleAdvertisementData &out) {
  if (scaleUuid16AllowsNamelessConnect(uuid)) {
    out.compatibleUuid16 = true;
  }
}

bool nimbleAccumulateAdvertisement(const uint8_t *data, size_t length,
                                   NimbleAdvertisementData &out) {
  if (!validTlvStream(data, length)) {
    return false;
  }
  size_t offset = 0;
  while (offset < length) {
    const size_t fieldLength = data[offset];
    if (fieldLength == 0) {
      break;
    }
    const uint8_t type = data[offset + 1];
    const uint8_t *value = data + offset + 2;
    const size_t valueLength = fieldLength - 1;
    if (type == kAdTypeCompleteName || type == kAdTypeShortName) {
      nimbleAccumulateAdvertisementName(
          value, valueLength, type == kAdTypeCompleteName, out);
    } else if (type == kAdTypeUuid16Complete ||
               type == kAdTypeUuid16Incomplete) {
      accumulateUuid16(value, valueLength, out);
    }
    offset += fieldLength + 1;
  }
  return true;
}

bool nimbleAdvertisementIsCompatible(const NimbleAdvertisementData &data) {
  return data.compatibleUuid16 ||
         (data.namePresent && scaleNameIsCompatible(data.name));
}
