#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace persistence_host {

inline std::map<std::string, std::vector<uint8_t>> records;
inline bool failNextWrite = false;
inline bool corruptNextWrite = false;
inline uint32_t randomState = 0x13579BDFU;

inline std::string storageKey(const char *nameSpace, const char *key) {
  return std::string(nameSpace == nullptr ? "" : nameSpace) + "/" +
         (key == nullptr ? "" : key);
}

inline void reset() {
  records.clear();
  failNextWrite = false;
  corruptNextWrite = false;
  randomState = 0x13579BDFU;
}

inline bool corrupt(const char *nameSpace, const char *key, size_t offset) {
  auto found = records.find(storageKey(nameSpace, key));
  if (found == records.end() || offset >= found->second.size()) {
    return false;
  }
  found->second[offset] ^= 0x5AU;
  return true;
}

inline void putRaw(const char *nameSpace, const char *key, const void *data,
                   size_t length) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  records[storageKey(nameSpace, key)] =
      std::vector<uint8_t>(bytes, bytes + length);
}

}  // namespace persistence_host

class PersistenceHostEEPROM {
 public:
  uint8_t read(size_t address) const { return bytes.at(address); }
  void write(size_t address, uint8_t value) { bytes.at(address) = value; }
  bool commit() { return true; }

  std::array<uint8_t, 2> bytes = {0xFF, 0xFF};
};

inline PersistenceHostEEPROM EEPROM;

class Preferences {
 public:
  bool begin(const char *nameSpace, bool readOnly = false) {
    nameSpace_ = nameSpace == nullptr ? "" : nameSpace;
    readOnly_ = readOnly;
    active_ = true;
    return true;
  }

  void end() { active_ = false; }

  size_t getBytesLength(const char *key) const {
    if (!active_) return 0;
    const auto found = persistence_host::records.find(
        persistence_host::storageKey(nameSpace_.c_str(), key));
    return found == persistence_host::records.end() ? 0 : found->second.size();
  }

  size_t getBytes(const char *key, void *output, size_t capacity) const {
    if (!active_ || output == nullptr) return 0;
    const auto found = persistence_host::records.find(
        persistence_host::storageKey(nameSpace_.c_str(), key));
    if (found == persistence_host::records.end() ||
        capacity < found->second.size()) {
      return 0;
    }
    std::memcpy(output, found->second.data(), found->second.size());
    return found->second.size();
  }

  size_t putBytes(const char *key, const void *input, size_t length) {
    if (!active_ || readOnly_ || input == nullptr ||
        persistence_host::failNextWrite) {
      persistence_host::failNextWrite = false;
      return 0;
    }
    persistence_host::putRaw(nameSpace_.c_str(), key, input, length);
    if (persistence_host::corruptNextWrite && length > 0) {
      persistence_host::corruptNextWrite = false;
      auto &stored = persistence_host::records[
          persistence_host::storageKey(nameSpace_.c_str(), key)];
      stored[length - 1] ^= 0x5AU;
    }
    return length;
  }

  bool remove(const char *key) {
    if (!active_ || readOnly_ || key == nullptr) {
      return false;
    }
    return persistence_host::records.erase(
               persistence_host::storageKey(nameSpace_.c_str(), key)) > 0;
  }

  bool clear() {
    if (!active_ || readOnly_) return false;
    const std::string prefix = nameSpace_ + "/";
    for (auto record = persistence_host::records.begin();
         record != persistence_host::records.end();) {
      if (record->first.compare(0, prefix.size(), prefix) == 0) {
        record = persistence_host::records.erase(record);
      } else {
        ++record;
      }
    }
    return true;
  }

 private:
  std::string nameSpace_;
  bool readOnly_ = false;
  bool active_ = false;
};

inline void esp_fill_random(void *output, size_t length) {
  auto *bytes = static_cast<uint8_t *>(output);
  for (size_t index = 0; index < length; ++index) {
    persistence_host::randomState =
        persistence_host::randomState * 1664525U + 1013904223U;
    bytes[index] = static_cast<uint8_t>(persistence_host::randomState >> 24U);
  }
}

struct mbedtls_sha256_context {
  uint32_t hash = 2166136261U;
};

inline void mbedtls_sha256_init(mbedtls_sha256_context *context) {
  context->hash = 2166136261U;
}

inline int mbedtls_sha256_starts(mbedtls_sha256_context *context, int) {
  context->hash = 2166136261U;
  return 0;
}

inline int mbedtls_sha256_update(mbedtls_sha256_context *context,
                                 const uint8_t *input, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    context->hash = (context->hash ^ input[index]) * 16777619U;
  }
  return 0;
}

inline int mbedtls_sha256_finish(mbedtls_sha256_context *context,
                                 uint8_t output[32]) {
  uint32_t value = context->hash;
  for (size_t index = 0; index < 32; ++index) {
    value = value * 1103515245U + 12345U;
    output[index] = static_cast<uint8_t>(value >> 24U);
  }
  return 0;
}

inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}
