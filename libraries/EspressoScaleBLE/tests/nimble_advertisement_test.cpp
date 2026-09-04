#include "nimble/NimbleAdvertisement.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

int checks = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " \
                << #condition << std::endl;                                    \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

void testAdvAndScanResponseMerge() {
  const uint8_t advertisement[] = {3, 0x03, 0x11, 0xff};
  const uint8_t scanResponse[] = {
      7, 0x09, 'B', 'O', 'O', 'K', 'O', 'O'};
  NimbleAdvertisementData data = {};
  CHECK(nimbleAccumulateAdvertisement(advertisement,
                                      sizeof(advertisement), data));
  CHECK(data.compatibleUuid16);
  CHECK(!data.namePresent);
  CHECK(nimbleAccumulateAdvertisement(scanResponse, sizeof(scanResponse),
                                      data));
  CHECK(data.nameComplete);
  CHECK(std::strcmp(data.name, "BOOKOO") == 0);
  CHECK(nimbleAdvertisementIsCompatible(data));
}

void testMalformedAndBounds() {
  NimbleAdvertisementData data = {};
  const uint8_t malformed[] = {5, 0x09, 'A'};
  CHECK(!nimbleAccumulateAdvertisement(malformed, sizeof(malformed), data));
  CHECK(!data.namePresent);
  CHECK(!nimbleAccumulateAdvertisement(nullptr, 1, data));

  uint8_t longName[40] = {};
  longName[0] = 39;
  longName[1] = 0x09;
  for (size_t i = 2; i < sizeof(longName); ++i) {
    longName[i] = 'X';
  }
  CHECK(nimbleAccumulateAdvertisement(longName, sizeof(longName), data));
  CHECK(data.name[NIMBLE_SCALE_ADV_NAME_CAPACITY - 1] == '\0');
}

void testShortNameCannotReplaceCompleteName() {
  const uint8_t complete[] = {7, 0x09, 'B', 'O', 'O', 'K', 'O', 'O'};
  const uint8_t shortened[] = {4, 0x08, 'A', 'C', 'A'};
  NimbleAdvertisementData data = {};
  CHECK(nimbleAccumulateAdvertisement(complete, sizeof(complete), data));
  CHECK(nimbleAccumulateAdvertisement(shortened, sizeof(shortened), data));
  CHECK(std::strcmp(data.name, "BOOKOO") == 0);
}

void fuzzEveryTruncation() {
  const uint8_t payload[] = {
      2, 0x01, 0x06, 3, 0x03, 0x11, 0xff,
      7, 0x09, 'B', 'O', 'O', 'K', 'O', 'O'};
  for (size_t length = 0; length <= sizeof(payload); ++length) {
    NimbleAdvertisementData data = {};
    const bool valid =
        nimbleAccumulateAdvertisement(payload, length, data);
    CHECK(valid == (length == 0 || length == 3 || length == 7 ||
                    length == sizeof(payload)));
  }
}

}  // namespace

int main() {
  testAdvAndScanResponseMerge();
  testMalformedAndBounds();
  testShortNameCannotReplaceCompleteName();
  fuzzEveryTruncation();
  std::cout << "NimBLE advertisement tests passed: " << checks << " checks\n";
  return 0;
}
