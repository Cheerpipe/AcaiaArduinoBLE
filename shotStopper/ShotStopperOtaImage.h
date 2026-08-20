#pragma once

// Pure, host-testable parsing of an ESP32-S3 application image.
//
// Nothing here touches flash or the network: it decides whether a stream of
// bytes looks like a Shot Stopper build for a given board, so the OTA path can
// refuse a wrong file before it is ever allowed to become the boot image.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace shotstopper {

constexpr size_t OTA_ARCH_CAPACITY = 16;
constexpr size_t OTA_VERSION_CAPACITY = 48;
constexpr size_t OTA_TAG_BODY_CAPACITY = 160;
constexpr size_t OTA_TAG_PREFIX_CAPACITY = 32;

// esp_image_header_t (24 B) + one esp_image_segment_header_t (8 B) precede the
// esp_app_desc_t that esptool places at the start of the first segment.
constexpr size_t OTA_APP_DESC_OFFSET = 32;
constexpr size_t OTA_APP_DESC_BYTES = 256;
constexpr size_t OTA_IMAGE_PREFIX_BYTES =
    OTA_APP_DESC_OFFSET + OTA_APP_DESC_BYTES;
constexpr size_t OTA_APP_DESC_PROJECT_NAME_OFFSET = OTA_APP_DESC_OFFSET + 48;
constexpr size_t OTA_APP_DESC_NAME_BYTES = 32;

constexpr uint8_t OTA_ESP_IMAGE_MAGIC = 0xE9;
constexpr uint16_t OTA_ESP_CHIP_ID_ESP32S3 = 0x0009;
constexpr uint32_t OTA_APP_DESC_MAGIC = 0xABCD5432U;

// Arduino-ESP32 cores produced by esp32-arduino-lib-builder share this name.
// Native IDF firmware (./scripts/build-idf) uses CMake project(shotstopper).
// Either name proves a Shot Stopper-capable ESP32-S3 image; the tag below is
// what identifies the sketch.
constexpr const char *OTA_EXPECTED_PROJECT_NAME = "arduino-lib-builder";
constexpr const char *OTA_EXPECTED_PROJECT_NAME_IDF = "shotstopper";

// The literal an image must contain, minus its "arch=…" payload. It is stored
// split so that a compiled firmware holds exactly one contiguous copy of the
// full prefix: its own tag, never this search needle.
constexpr const char *OTA_TAG_PREFIX_PART_1 = "SHOTSTOPPER";
constexpr const char *OTA_TAG_PREFIX_PART_2 = "_FW_TAG_V1|";
constexpr const char *OTA_TAG_TERMINATOR = "|END";
constexpr size_t OTA_TAG_TERMINATOR_LENGTH = 4;

struct OtaImageTag {
  bool valid = false;
  char arch[OTA_ARCH_CAPACITY] = {};
  char version[OTA_VERSION_CAPACITY] = {};
  uint32_t packed = 0;
};

enum class OtaImageHeaderResult : uint8_t {
  OK = 0,
  TOO_SHORT,
  BAD_MAGIC,
  WRONG_CHIP,
  BAD_APP_DESC,
  WRONG_PROJECT,
};

inline void otaTagPrefix(char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) {
    return;
  }
  snprintf(output, capacity, "%s%s", OTA_TAG_PREFIX_PART_1,
           OTA_TAG_PREFIX_PART_2);
}

inline bool otaArchCharAllowed(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9');
}

inline bool otaVersionCharAllowed(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '.' ||
         character == '+' || character == '-' || character == '_';
}

// "unknown" is what gen_version.sh emits when no board was named. Refusing it
// on both sides keeps an image that was never built for a real controller from
// ever matching one.
inline bool otaArchIsUsable(const char *arch) {
  if (arch == nullptr || arch[0] == '\0') {
    return false;
  }
  if (strcmp(arch, "unknown") == 0) {
    return false;
  }
  for (const char *cursor = arch; *cursor != '\0'; ++cursor) {
    if (!otaArchCharAllowed(*cursor)) {
      return false;
    }
  }
  return true;
}

inline bool otaCopyField(const char *value, size_t length, char *output,
                         size_t capacity, bool (*allowed)(char)) {
  if (length == 0 || length + 1 > capacity) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    if (!allowed(value[index])) {
      return false;
    }
  }
  memcpy(output, value, length);
  output[length] = '\0';
  return true;
}

inline bool otaParseUint32(const char *value, size_t length, uint32_t &output) {
  if (length == 0 || length > 10) {
    return false;
  }
  uint64_t accumulator = 0;
  for (size_t index = 0; index < length; ++index) {
    if (value[index] < '0' || value[index] > '9') {
      return false;
    }
    accumulator = accumulator * 10U + static_cast<uint64_t>(value[index] - '0');
    if (accumulator > 0xFFFFFFFFULL) {
      return false;
    }
  }
  output = static_cast<uint32_t>(accumulator);
  return true;
}

// Parses the bytes that follow the tag prefix, up to and including "|END".
inline bool parseOtaImageTagBody(const char *body, size_t length,
                                 OtaImageTag &output) {
  output = OtaImageTag{};
  if (body == nullptr || length < OTA_TAG_TERMINATOR_LENGTH) {
    return false;
  }
  if (memcmp(body + length - OTA_TAG_TERMINATOR_LENGTH, OTA_TAG_TERMINATOR,
             OTA_TAG_TERMINATOR_LENGTH) != 0) {
    return false;
  }
  const size_t payload = length - OTA_TAG_TERMINATOR_LENGTH;
  bool haveArch = false;
  bool haveVersion = false;
  bool havePacked = false;
  size_t cursor = 0;
  while (cursor < payload) {
    size_t separator = cursor;
    while (separator < payload && body[separator] != '|') {
      ++separator;
    }
    const char *field = body + cursor;
    const size_t fieldLength = separator - cursor;
    const char *equals =
        static_cast<const char *>(memchr(field, '=', fieldLength));
    if (equals != nullptr) {
      const size_t nameLength = static_cast<size_t>(equals - field);
      const char *value = equals + 1;
      const size_t valueLength = fieldLength - nameLength - 1;
      if (nameLength == 4 && memcmp(field, "arch", 4) == 0) {
        haveArch = otaCopyField(value, valueLength, output.arch,
                                sizeof(output.arch), otaArchCharAllowed);
        if (!haveArch) return false;
      } else if (nameLength == 3 && memcmp(field, "ver", 3) == 0) {
        haveVersion = otaCopyField(value, valueLength, output.version,
                                   sizeof(output.version),
                                   otaVersionCharAllowed);
        if (!haveVersion) return false;
      } else if (nameLength == 6 && memcmp(field, "packed", 6) == 0) {
        havePacked = otaParseUint32(value, valueLength, output.packed);
        if (!havePacked) return false;
      }
    }
    cursor = separator + 1;
  }
  if (!haveArch || !haveVersion || !havePacked) {
    return false;
  }
  output.valid = true;
  return true;
}

inline OtaImageHeaderResult validateOtaImageHeader(const uint8_t *bytes,
                                                   size_t length) {
  if (bytes == nullptr || length < OTA_IMAGE_PREFIX_BYTES) {
    return OtaImageHeaderResult::TOO_SHORT;
  }
  if (bytes[0] != OTA_ESP_IMAGE_MAGIC) {
    return OtaImageHeaderResult::BAD_MAGIC;
  }
  const uint16_t chipId = static_cast<uint16_t>(
      static_cast<uint16_t>(bytes[12]) |
      (static_cast<uint16_t>(bytes[13]) << 8));
  if (chipId != OTA_ESP_CHIP_ID_ESP32S3) {
    return OtaImageHeaderResult::WRONG_CHIP;
  }
  uint32_t descMagic = 0;
  memcpy(&descMagic, bytes + OTA_APP_DESC_OFFSET, sizeof(descMagic));
  if (descMagic != OTA_APP_DESC_MAGIC) {
    return OtaImageHeaderResult::BAD_APP_DESC;
  }
  char projectName[OTA_APP_DESC_NAME_BYTES + 1] = {};
  memcpy(projectName, bytes + OTA_APP_DESC_PROJECT_NAME_OFFSET,
         OTA_APP_DESC_NAME_BYTES);
  projectName[OTA_APP_DESC_NAME_BYTES] = '\0';
  if (strcmp(projectName, OTA_EXPECTED_PROJECT_NAME) != 0 &&
      strcmp(projectName, OTA_EXPECTED_PROJECT_NAME_IDF) != 0) {
    return OtaImageHeaderResult::WRONG_PROJECT;
  }
  return OtaImageHeaderResult::OK;
}

inline const char *otaImageHeaderResultName(OtaImageHeaderResult result) {
  switch (result) {
    case OtaImageHeaderResult::OK: return "OK";
    case OtaImageHeaderResult::TOO_SHORT: return "TOO_SHORT";
    case OtaImageHeaderResult::BAD_MAGIC: return "BAD_MAGIC";
    case OtaImageHeaderResult::WRONG_CHIP: return "WRONG_CHIP";
    case OtaImageHeaderResult::BAD_APP_DESC: return "BAD_APP_DESC";
    case OtaImageHeaderResult::WRONG_PROJECT: return "WRONG_PROJECT";
  }
  return "UNKNOWN";
}

// Finds the Shot Stopper tag in a byte stream delivered in arbitrary chunks.
//
// Uses Knuth-Morris-Pratt so a partial match that fails can still resume inside
// itself: the prefix "SHOTSTOPPER…" overlaps itself at "SHOTS", and a naive
// restart would miss a tag preceded by such a fragment.
class OtaImageTagScanner {
  public:
  OtaImageTagScanner() { reset(); }

  void reset() {
    otaTagPrefix(prefix_, sizeof(prefix_));
    prefixLength_ = strlen(prefix_);
    buildFailureTable();
    matchLength_ = 0;
    bodyLength_ = 0;
    capturing_ = false;
    done_ = false;
    tagOffset_ = 0;
    position_ = 0;
    tag_ = OtaImageTag{};
  }

  void feed(const uint8_t *data, size_t length) {
    if (data == nullptr) {
      return;
    }
    for (size_t index = 0; index < length && !done_; ++index) {
      feedByte(data[index]);
      ++position_;
    }
  }

  bool found() const { return done_; }
  const OtaImageTag &tag() const { return tag_; }
  // Byte offset of the first character of the tag prefix within the image.
  uint32_t tagOffset() const { return tagOffset_; }
  size_t prefixLength() const { return prefixLength_; }

  private:
  void buildFailureTable() {
    if (prefixLength_ == 0) {
      return;
    }
    failure_[0] = 0;
    size_t border = 0;
    for (size_t index = 1; index < prefixLength_; ++index) {
      while (border > 0 && prefix_[index] != prefix_[border]) {
        border = failure_[border - 1];
      }
      if (prefix_[index] == prefix_[border]) {
        ++border;
      }
      failure_[index] = border;
    }
  }

  void matchOnly(uint8_t byte) {
    while (matchLength_ > 0 &&
           static_cast<char>(byte) != prefix_[matchLength_]) {
      matchLength_ = failure_[matchLength_ - 1];
    }
    if (static_cast<char>(byte) == prefix_[matchLength_]) {
      ++matchLength_;
    }
    if (matchLength_ == prefixLength_) {
      capturing_ = true;
      bodyLength_ = 0;
      matchLength_ = 0;
      tagOffset_ = static_cast<uint32_t>(position_ + 1 - prefixLength_);
    }
  }

  // Re-scan bytes consumed into an abandoned capture. The live `body_` buffer
  // is copied first because a new capture writes into it. Recursion is bounded
  // by shrinking the replayed window by one byte each time.
  void replayAbandonedBody() {
    const size_t replay = bodyLength_;
    if (replay == 0) {
      capturing_ = false;
      matchLength_ = 0;
      tag_ = OtaImageTag{};
      return;
    }
    char saved[OTA_TAG_BODY_CAPACITY];
    memcpy(saved, body_, replay);
    bodyLength_ = 0;
    capturing_ = false;
    matchLength_ = 0;
    tag_ = OtaImageTag{};
    position_ -= replay;
    // Skip the first saved byte: it is what started this failed capture, and
    // re-offering it would immediately recapture the same candidate.
    ++position_;
    for (size_t index = 1; index < replay && !done_; ++index) {
      feedByte(static_cast<uint8_t>(saved[index]));
      ++position_;
    }
  }

  void feedByte(uint8_t byte) {
    if (capturing_) {
      if (byte == 0) {
        replayAbandonedBody();
        return;
      }
      if (bodyLength_ >= OTA_TAG_BODY_CAPACITY) {
        replayAbandonedBody();
        if (done_) {
          return;
        }
        feedByte(byte);
        return;
      }
      body_[bodyLength_++] = static_cast<char>(byte);
      if (bodyLength_ >= OTA_TAG_TERMINATOR_LENGTH &&
          memcmp(body_ + bodyLength_ - OTA_TAG_TERMINATOR_LENGTH,
                 OTA_TAG_TERMINATOR, OTA_TAG_TERMINATOR_LENGTH) == 0) {
        if (parseOtaImageTagBody(body_, bodyLength_, tag_)) {
          capturing_ = false;
          done_ = true;
          return;
        }
        replayAbandonedBody();
      }
      return;
    }
    matchOnly(byte);
  }

  char prefix_[OTA_TAG_PREFIX_CAPACITY] = {};
  size_t failure_[OTA_TAG_PREFIX_CAPACITY] = {};
  size_t prefixLength_ = 0;
  size_t matchLength_ = 0;
  char body_[OTA_TAG_BODY_CAPACITY] = {};
  size_t bodyLength_ = 0;
  bool capturing_ = false;
  bool done_ = false;
  uint32_t tagOffset_ = 0;
  size_t position_ = 0;
  OtaImageTag tag_ = {};
};

}   // namespace shotstopper
