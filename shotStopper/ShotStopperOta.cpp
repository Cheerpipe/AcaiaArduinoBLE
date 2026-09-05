#include "ShotStopperOta.h"

#include "ShotStopperPsram.h"
#include "ShotStopperPreferences.h"
#include "ShotStopperVersion.h"
#include "ShotStopperWatchdog.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace shotstopper {

// The identity marker every flashable Shot Stopper image carries. It has
// external linkage and is read back by begin(), which is what keeps the linker
// from garbage-collecting the one thing an OTA verify looks for.
extern "C" const char SHOT_STOPPER_FW_IMAGE_TAG[] = FW_IMAGE_TAG_STRING;

namespace {

// Transfer buffer size. Allocated from internal heap only for the duration of
// stage(): the flash driver cannot DMA from PSRAM, and a permanent 4 KiB BSS
// member is wasted between updates.
constexpr size_t OTA_CHUNK_BYTES = 4096;

struct OtaChunkBuffer {
  uint8_t *bytes = nullptr;

  explicit OtaChunkBuffer(size_t capacity)
      : bytes(static_cast<uint8_t *>(allocInternal(capacity))) {}

  ~OtaChunkBuffer() { heapCapsFree(bytes); }

  OtaChunkBuffer(const OtaChunkBuffer &) = delete;
  OtaChunkBuffer &operator=(const OtaChunkBuffer &) = delete;

  bool ok() const { return bytes != nullptr; }
};

// A real image is well over a megabyte; anything this small is not one.
constexpr uint32_t OTA_MIN_IMAGE_BYTES = 65536;

// Progress logs every 256 KiB. Safety is checked on every chunk so a paddle
// pull aborts the transfer immediately instead of after 64 KiB of Wi-Fi.
constexpr uint32_t OTA_PROGRESS_INTERVAL_BYTES = 262144;
constexpr uint32_t OTA_SESSION_TTL_MS = 15U * 60U * 1000U;
constexpr uint32_t OTA_JOURNAL_MAGIC = 0x4f544a31U;  // OTJ1
constexpr uint16_t OTA_JOURNAL_VERSION = 1;

// Two NVS records make a power cut during the metadata update harmless. The
// image itself remains in the inactive OTA partition; this journal only says
// which already-written prefix may be resumed.
struct OtaJournalRecord {
  uint32_t magic = OTA_JOURNAL_MAGIC;
  uint16_t version = OTA_JOURNAL_VERSION;
  uint16_t reserved = 0;
  uint32_t generation = 0;
  uint32_t received = 0;
  OtaSessionIdentity identity = {};
  char prefixSha256[OTA_SHA256_HEX_CAPACITY] = {};
  uint32_t checksum = 0;
};

constexpr size_t OTA_TAG_REREAD_BYTES =
    OTA_TAG_PREFIX_CAPACITY + OTA_TAG_BODY_CAPACITY;

bool sameTag(const OtaImageTag &left, const OtaImageTag &right) {
  return left.valid && right.valid && left.packed == right.packed &&
         strncmp(left.arch, right.arch, OTA_ARCH_CAPACITY) == 0 &&
         strncmp(left.version, right.version, OTA_VERSION_CAPACITY) == 0;
}

bool sameText(const char *left, const char *right, size_t capacity) {
  return strncmp(left, right, capacity) == 0;
}

bool sameSession(const OtaSessionIdentity &left,
                 const OtaSessionIdentity &right) {
  return left.size == right.size &&
         sameText(left.sha256, right.sha256, sizeof(left.sha256)) &&
         sameText(left.arch, right.arch, sizeof(left.arch)) &&
         sameText(left.version, right.version, sizeof(left.version)) &&
         sameText(left.transferId, right.transferId,
                  sizeof(left.transferId));
}

void sha256Hex(const uint8_t digest[32], char output[OTA_SHA256_HEX_CAPACITY]) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  for (size_t index = 0; index < 32; ++index) {
    output[index * 2] = HEX_DIGITS[digest[index] >> 4U];
    output[index * 2 + 1] = HEX_DIGITS[digest[index] & 0x0fU];
  }
  output[64] = '\0';
}

uint32_t journalChecksum(const OtaJournalRecord &record) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
  const size_t length = offsetof(OtaJournalRecord, checksum);
  uint32_t value = 2166136261U;
  for (size_t index = 0; index < length; ++index) {
    value ^= bytes[index];
    value *= 16777619U;
  }
  return value;
}

bool validJournal(const OtaJournalRecord &record) {
  return record.magic == OTA_JOURNAL_MAGIC &&
         record.version == OTA_JOURNAL_VERSION && record.received <= record.identity.size &&
         record.identity.size != 0 && record.checksum == journalChecksum(record);
}

bool validJournalIdentity(const OtaSessionIdentity &identity) {
  if (!otaArchIsUsable(identity.arch) || identity.version[0] == '\0' ||
      identity.transferId[0] == '\0' || strlen(identity.sha256) != 64) {
    return false;
  }
  for (const char *cursor = identity.version; *cursor != '\0'; ++cursor) {
    if (!otaVersionCharAllowed(*cursor)) return false;
  }
  for (const char *cursor = identity.transferId; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  for (const char *cursor = identity.sha256; *cursor != '\0'; ++cursor) {
    if (!((*cursor >= '0' && *cursor <= '9') ||
          (*cursor >= 'a' && *cursor <= 'f'))) return false;
  }
  return true;
}

bool readJournal(const char *key, OtaJournalRecord &record) {
  Preferences preferences;
  if (!preferences.begin("ota", true)) return false;
  const size_t length = preferences.getBytes(key, &record, sizeof(record));
  preferences.end();
  return length == sizeof(record) && validJournal(record);
}

bool writeJournal(const char *key, const OtaJournalRecord &record) {
  Preferences preferences;
  if (!preferences.begin("ota", false)) return false;
  const size_t length = preferences.putBytes(key, &record, sizeof(record));
  preferences.end();
  return length == sizeof(record);
}

void clearJournal() {
  Preferences preferences;
  if (!preferences.begin("ota", false)) return;
  preferences.remove("j0");
  preferences.remove("j1");
  preferences.end();
}

}  // namespace

ShotStopperOta &ShotStopperOta::instance() {
  static ShotStopperOta shared;
  return shared;
}

void ShotStopperOta::begin() {
  if (started_) {
    return;
  }
  started_ = true;

  OtaImageTagScanner scanner;
  scanner.feed(reinterpret_cast<const uint8_t *>(SHOT_STOPPER_FW_IMAGE_TAG),
               strlen(SHOT_STOPPER_FW_IMAGE_TAG));
  if (scanner.found()) {
    runningTag_ = scanner.tag();
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  runningPartition_ = running;
  targetPartition_ = target;
  available_ = running != nullptr && target != nullptr && target != running;
  slotBytes_ = target != nullptr ? target->size : 0;
  state_ = available_ ? OtaState::IDLE : OtaState::UNAVAILABLE;

  if (running != nullptr) {
    esp_ota_img_states_t imageState = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &imageState) == ESP_OK) {
      pendingVerify_ = imageState == ESP_OTA_IMG_PENDING_VERIFY;
    }
  }
  // A slot that is not awaiting confirmation is already permanent.
  confirmed_ = !pendingVerify_;
  if (available_ && confirmed_) {
    restoreSessionJournal();
  }
}

void ShotStopperOta::removeSessionJournal() { clearJournal(); }

bool ShotStopperOta::persistSession() {
  if (!sessionActive_) return true;
  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  if (target == nullptr || receivedBytes_ > target->size) return false;
  OtaChunkBuffer chunk(OTA_CHUNK_BYTES);
  if (!chunk.ok()) return false;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  uint32_t offset = 0;
  while (ok && offset < receivedBytes_) {
    const size_t length = receivedBytes_ - offset < OTA_CHUNK_BYTES
                              ? receivedBytes_ - offset
                              : OTA_CHUNK_BYTES;
    ok = esp_partition_read(target, offset, chunk.bytes, length) == ESP_OK &&
         mbedtls_sha256_update(&hash, chunk.bytes, length) == 0;
    offset += static_cast<uint32_t>(length);
  }
  OtaJournalRecord record;
  record.generation = ++sessionGeneration_;
  record.received = receivedBytes_;
  record.identity = session_;
  uint8_t digest[32] = {};
  ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  if (!ok) return false;
  sha256Hex(digest, record.prefixSha256);
  record.checksum = journalChecksum(record);
  return writeJournal((record.generation & 1U) == 0 ? "j0" : "j1", record);
}

void ShotStopperOta::restoreSessionJournal() {
  OtaJournalRecord first;
  OtaJournalRecord second;
  const bool firstValid = readJournal("j0", first);
  const bool secondValid = readJournal("j1", second);
  if (!firstValid && !secondValid) return;
  const OtaJournalRecord &record =
      (!firstValid || (secondValid &&
       static_cast<int32_t>(second.generation - first.generation) > 0)) ? second : first;
  if (!validJournalIdentity(record.identity) || record.identity.size > slotBytes_) {
    clearJournal();
    return;
  }
  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  OtaChunkBuffer chunk(OTA_CHUNK_BYTES);
  if (target == nullptr || !chunk.ok()) return;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  OtaImageTagScanner scanner;
  uint32_t offset = 0;
  bool headerChecked = record.received == 0;
  while (ok && offset < record.received) {
    const size_t length = record.received - offset < OTA_CHUNK_BYTES
                              ? record.received - offset
                              : OTA_CHUNK_BYTES;
    ok = esp_partition_read(target, offset, chunk.bytes, length) == ESP_OK &&
         mbedtls_sha256_update(&hash, chunk.bytes, length) == 0;
    if (!headerChecked && offset == 0) {
      headerChecked = length >= OTA_IMAGE_PREFIX_BYTES &&
                      validateOtaImageHeader(chunk.bytes, length) == OtaImageHeaderResult::OK;
      ok = ok && headerChecked;
    }
    scanner.feed(chunk.bytes, length);
    offset += static_cast<uint32_t>(length);
  }
  uint8_t digest[32] = {};
  ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  char prefix[OTA_SHA256_HEX_CAPACITY] = {};
  sha256Hex(digest, prefix);
  if (!ok || !sameText(prefix, record.prefixSha256, sizeof(prefix))) {
    clearJournal();
    return;
  }
  session_ = record.identity;
  sessionGeneration_ = record.generation;
  receivedBytes_ = record.received;
  expectedBytes_ = record.identity.size;
  scanner_ = scanner;
  sessionLastActivityMs_ = millis();
  sessionActive_ = true;
  state_ = OtaState::RECEIVING;
  if (record.received != 0) {
    esp_ota_handle_t handle = 0;
    if (esp_ota_resume(target, 0, record.received, &handle) != ESP_OK) {
      clearSession(false);
      clearJournal();
      receivedBytes_ = 0;
      expectedBytes_ = 0;
      state_ = OtaState::IDLE;
      return;
    }
    otaHandle_ = static_cast<uint32_t>(handle);
    handleOpen_ = true;
  }
}

OtaStatusSnapshot ShotStopperOta::snapshot() const {
  OtaStatusSnapshot copy;
  copy.state = state_;
  copy.slotBytes = slotBytes_;
  copy.receivedBytes = receivedBytes_;
  copy.expectedBytes = expectedBytes_;
  copy.lastResult = lastResult_;
  copy.lastReceivedBytes = lastReceivedBytes_;
  copy.lastExpectedBytes = lastExpectedBytes_;
  copy.stagedValid = stagedValid_;
  copy.staged = stagedTag_;
  copy.pendingVerify = pendingVerify_;
  copy.confirmed = confirmed_;
  copy.sessionActive = sessionActive_;
  copy.nextOffset = receivedBytes_;
  copy.session = session_;
  memcpy(copy.lastChunkSha256, lastChunkSha256_, sizeof(lastChunkSha256_));
  if (sessionActive_) {
    const uint32_t age = static_cast<uint32_t>(millis() - sessionLastActivityMs_);
    copy.sessionExpiresInMs = age < OTA_SESSION_TTL_MS ? OTA_SESSION_TTL_MS - age : 0;
  }
  return copy;
}

OtaResult ShotStopperOta::finishFailure(OtaResult result) {
  // Keep this evidence after clearing the active transfer. The HTTP handler
  // reads its snapshot after stage() returns, so resetting first used to make
  // every failed upload look as though it had failed at byte zero.
  lastResult_ = result;
  lastReceivedBytes_ = receivedBytes_;
  lastExpectedBytes_ = expectedBytes_;
  clearSession(true);
  busy_ = false;
  receivedBytes_ = 0;
  expectedBytes_ = 0;
  // A failed transfer leaves a partial image in the inactive slot. It can never
  // boot: the selection is untouched and esp_ota_set_boot_partition would
  // refuse it anyway.
  stagedValid_ = false;
  stagedTag_ = OtaImageTag{};
  state_ = available_ ? OtaState::IDLE : OtaState::UNAVAILABLE;
  return result;
}

void ShotStopperOta::clearSession(bool abortHandle) {
  if (abortHandle && handleOpen_) {
    esp_ota_abort(static_cast<esp_ota_handle_t>(otaHandle_));
  }
  handleOpen_ = false;
  otaHandle_ = 0;
  sessionActive_ = false;
  sessionLastActivityMs_ = 0;
  session_ = OtaSessionIdentity{};
  lastChunkSha256_[0] = '\0';
  lastChunkOffset_ = 0;
  lastChunkLength_ = 0;
  scanner_.reset();
  removeSessionJournal();
}

bool ShotStopperOta::isExactSession(const OtaSessionIdentity &identity) const {
  return (sessionActive_ || stagedValid_) && sameSession(session_, identity);
}

bool ShotStopperOta::isDuplicateRange(uint32_t offset,
                                      uint32_t contentLength) const {
  return sessionActive_ && contentLength != 0 &&
         offset == lastChunkOffset_ && contentLength == lastChunkLength_ &&
         offset + contentLength == receivedBytes_;
}

void ShotStopperOta::expireSession(uint32_t now) {
  if (!sessionActive_) {
    return;
  }
  if (static_cast<uint32_t>(now - sessionLastActivityMs_) >= OTA_SESSION_TTL_MS) {
    finishFailure(OtaResult::SESSION_EXPIRED);
  }
}

OtaResult ShotStopperOta::createSession(const OtaSessionIdentity &identity,
                                        uint32_t now) {
  if (!started_ || !available_) {
    return OtaResult::UNAVAILABLE;
  }
  expireSession(now);
  if (busy_ || state_ == OtaState::COMMITTED) {
    return OtaResult::BUSY;
  }
  // esp_ota_begin refuses to write while the running slot is still
  // ESP_OTA_IMG_PENDING_VERIFY, and rightly so: overwriting the only other
  // slot would leave nothing to roll back to.
  if (!confirmed_) {
    return OtaResult::PENDING_VERIFY;
  }
  if (!otaArchIsUsable(runningTag_.arch)) {
    return OtaResult::NO_IDENTITY;
  }
  if (identity.size < OTA_MIN_IMAGE_BYTES) {
    return OtaResult::BAD_LENGTH;
  }
  if (identity.size > slotBytes_) {
    return OtaResult::TOO_LARGE;
  }
  if (!otaArchIsUsable(identity.arch) || identity.sha256[0] == '\0' ||
      identity.transferId[0] == '\0' || identity.version[0] == '\0') {
    return OtaResult::SESSION_IDENTITY_MISMATCH;
  }
  if (sessionActive_ || stagedValid_) {
    return isExactSession(identity) ? OtaResult::OK
                                    : OtaResult::SESSION_CONFLICT;
  }

  session_ = identity;
  ++sessionGeneration_;
  sessionActive_ = true;
  sessionLastActivityMs_ = now;
  scanner_.reset();
  lastChunkSha256_[0] = '\0';
  lastChunkOffset_ = 0;
  lastChunkLength_ = 0;
  busy_ = false;
  state_ = OtaState::RECEIVING;
  stagedValid_ = false;
  stagedTag_ = OtaImageTag{};
  receivedBytes_ = 0;
  expectedBytes_ = identity.size;
  lastResult_ = OtaResult::OK;
  if (!persistSession()) return finishFailure(OtaResult::INTERNAL);
  return OtaResult::OK;
}

bool ShotStopperOta::verifySessionSha256() {
  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  if (target == nullptr || session_.size == 0) {
    return false;
  }
  OtaChunkBuffer chunk(OTA_CHUNK_BYTES);
  if (!chunk.ok()) {
    return false;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  uint32_t offset = 0;
  while (ok && offset < session_.size) {
    const size_t length = session_.size - offset < OTA_CHUNK_BYTES
                              ? session_.size - offset
                              : OTA_CHUNK_BYTES;
    ok = esp_partition_read(target, offset, chunk.bytes, length) == ESP_OK &&
         mbedtls_sha256_update(&hash, chunk.bytes, length) == 0;
    offset += static_cast<uint32_t>(length);
  }
  uint8_t digest[32] = {};
  ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  if (!ok) {
    return false;
  }
  char actual[OTA_SHA256_HEX_CAPACITY] = {};
  sha256Hex(digest, actual);
  return sameText(actual, session_.sha256, sizeof(actual));
}

OtaResult ShotStopperOta::writeRange(uint32_t offset, uint32_t contentLength,
                                     const OtaStreamIo &io, uint32_t now) {
  if (!sessionActive_) {
    return OtaResult::SESSION_REQUIRED;
  }
  expireSession(now);
  if (!sessionActive_) {
    return OtaResult::SESSION_EXPIRED;
  }
  if (busy_) {
    return OtaResult::BUSY;
  }
  if (io.read == nullptr || io.stillSafe == nullptr || contentLength == 0 ||
      contentLength > OTA_TRANSFER_CHUNK_BYTES || offset != receivedBytes_ ||
      contentLength > expectedBytes_ - receivedBytes_) {
    return offset != receivedBytes_ ? OtaResult::OFFSET_MISMATCH
                                    : OtaResult::INVALID_RANGE;
  }
  if (offset == 0 && contentLength < OTA_IMAGE_PREFIX_BYTES) {
    return OtaResult::INVALID_RANGE;
  }

  busy_ = true;
  // Flash cache is disabled briefly for every write. The buffer and hash
  // state live on the internal httpd stack, never in PSRAM.
  (void)esp_wifi_set_ps(WIFI_PS_NONE);

  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  if (target == nullptr) {
    return finishFailure(OtaResult::UNAVAILABLE);
  }

  OtaChunkBuffer chunk(OTA_CHUNK_BYTES);
  if (!chunk.ok()) {
    return finishFailure(OtaResult::NO_MEMORY);
  }
  size_t chunkBytes = OTA_CHUNK_BYTES;
  uint8_t *const buffer = chunk.bytes;

  // Flash erase plus the closing SHA-256 pass exceed the normal watchdog
  // budget; restored by the destructor on every exit path below.
  TaskWatchdogOtaWindow watchdogWindow;

  mbedtls_sha256_context chunkHash;
  mbedtls_sha256_init(&chunkHash);
  bool hashStarted = mbedtls_sha256_starts(&chunkHash, 0) == 0;
  uint32_t rangeReceived = 0;
  OtaResult failure = hashStarted ? OtaResult::OK : OtaResult::INTERNAL;

  while (failure == OtaResult::OK && rangeReceived < contentLength) {
    // Shot always wins: abort as soon as the paddle moves, a cycle starts, or
    // K1 closes. Do not wait for a byte interval — Wi-Fi would already be
    // competing with the scale.
    if (!io.stillSafe(io.context)) {
      failure = OtaResult::SAFETY_LOST;
      break;
    }
    const uint32_t remaining = contentLength - rangeReceived;
    size_t want = chunkBytes;
    if (remaining < want) {
      want = static_cast<size_t>(remaining);
    }
    const int got = io.read(io.context, buffer, want);
    if (got <= 0) {
      failure = OtaResult::RECEIVE_FAILED;
      break;
    }
    // The callback is trusted to honour `want`, but a return larger than the
    // allocation would walk off the buffer into flash. Clamp rather than trust.
    const size_t length = static_cast<size_t>(got) > want
                              ? want
                              : static_cast<size_t>(got);

    if (!handleOpen_) {
      if (rangeReceived != 0 || length < OTA_IMAGE_PREFIX_BYTES ||
          validateOtaImageHeader(buffer, length) != OtaImageHeaderResult::OK) {
        failure = OtaResult::BAD_IMAGE;
        break;
      }
      esp_ota_handle_t handle = 0;
      if (esp_ota_begin(target, expectedBytes_, &handle) !=
          ESP_OK) {
        failure = OtaResult::WRITE_FAILED;
        break;
      }
      otaHandle_ = static_cast<uint32_t>(handle);
      handleOpen_ = true;
    }
    if (esp_ota_write(static_cast<esp_ota_handle_t>(otaHandle_), buffer,
                      length) != ESP_OK ||
        mbedtls_sha256_update(&chunkHash, buffer, length) != 0) {
      failure = OtaResult::WRITE_FAILED;
      break;
    }

    scanner_.feed(buffer, length);
    rangeReceived += static_cast<uint32_t>(length);
    receivedBytes_ += static_cast<uint32_t>(length);
    // The marker is in the early read-only segment. Reject a wrong board or
    // a same-version-but-different build identity before it consumes a full
    // slot, while the final SHA-256 remains the authoritative whole-image
    // check.
    if (scanner_.found()) {
      const OtaImageTag &tag = scanner_.tag();
      if (!otaArchIsUsable(tag.arch)) {
        failure = OtaResult::NO_TAG;
        break;
      }
      if (!sameText(tag.arch, runningTag_.arch, OTA_ARCH_CAPACITY) ||
          !sameText(tag.arch, session_.arch, OTA_ARCH_CAPACITY)) {
        failure = OtaResult::ARCH_MISMATCH;
        break;
      }
      if (!sameText(tag.version, session_.version, OTA_VERSION_CAPACITY)) {
        failure = OtaResult::SESSION_IDENTITY_MISMATCH;
        break;
      }
      if (tag.packed < FW_VERSION_PACKED) {
        failure = OtaResult::DOWNGRADE;
        break;
      }
    }
    if (io.progress != nullptr) io.progress(io.context, receivedBytes_, expectedBytes_);
  }
  uint8_t chunkDigest[32] = {};
  const bool hashFinished = mbedtls_sha256_finish(&chunkHash, chunkDigest) == 0;
  mbedtls_sha256_free(&chunkHash);

  if (failure == OtaResult::OK && (!handleOpen_ || rangeReceived != contentLength ||
                                  !hashFinished)) {
    failure = OtaResult::RECEIVE_FAILED;
  }
  if (failure != OtaResult::OK) {
    return finishFailure(failure);
  }
  sha256Hex(chunkDigest, lastChunkSha256_);
  lastChunkOffset_ = offset;
  lastChunkLength_ = contentLength;
  sessionLastActivityMs_ = now;

  if (!persistSession()) return finishFailure(OtaResult::INTERNAL);

  if (receivedBytes_ < expectedBytes_) {
    busy_ = false;
    lastResult_ = OtaResult::OK;
    return OtaResult::OK;
  }
  if (!scanner_.found()) return finishFailure(OtaResult::NO_TAG);
  const OtaImageTag &tag = scanner_.tag();
  if (!otaArchIsUsable(tag.arch)) return finishFailure(OtaResult::NO_TAG);
  if (!sameText(tag.arch, runningTag_.arch, OTA_ARCH_CAPACITY) ||
      !sameText(tag.arch, session_.arch, OTA_ARCH_CAPACITY)) {
    return finishFailure(OtaResult::ARCH_MISMATCH);
  }
  if (!sameText(tag.version, session_.version, OTA_VERSION_CAPACITY)) {
    return finishFailure(OtaResult::SESSION_IDENTITY_MISMATCH);
  }
  if (tag.packed < FW_VERSION_PACKED) return finishFailure(OtaResult::DOWNGRADE);

  // esp_ota_end re-reads the whole slot and verifies the appended SHA-256, so
  // a transfer that was silently corrupted in flight fails here.
  const esp_ota_handle_t closingHandle = static_cast<esp_ota_handle_t>(otaHandle_);
  const esp_err_t endStatus = esp_ota_end(closingHandle);
  if (endStatus != ESP_OK) {
    esp_ota_abort(closingHandle);
    handleOpen_ = false;
    otaHandle_ = 0;
    return finishFailure(OtaResult::VERIFY_FAILED);
  }
  handleOpen_ = false;
  otaHandle_ = 0;
  if (!verifySessionSha256()) return finishFailure(OtaResult::HASH_MISMATCH);

  // One last look at the machine before advertising the image as flashable.
  if (!io.stillSafe(io.context)) {
    return finishFailure(OtaResult::SAFETY_LOST);
  }

  stagedTag_ = tag;
  stagedTagOffset_ = scanner_.tagOffset();
  stagedSizeBytes_ = receivedBytes_;
  stagedValid_ = true;
  sessionActive_ = false;
  removeSessionJournal();
  busy_ = false;
  state_ = OtaState::STAGED;
  lastResult_ = OtaResult::OK;
  lastReceivedBytes_ = receivedBytes_;
  lastExpectedBytes_ = expectedBytes_;
  return OtaResult::OK;
}

bool ShotStopperOta::reconfirmStagedTag() {
  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  if (target == nullptr || !stagedValid_) {
    return false;
  }
  if (stagedTagOffset_ >= target->size) {
    return false;
  }
  size_t length = OTA_TAG_REREAD_BYTES;
  if (stagedTagOffset_ + length > target->size) {
    length = target->size - stagedTagOffset_;
  }
  uint8_t bytes[OTA_TAG_REREAD_BYTES] = {};
  if (esp_partition_read(target, stagedTagOffset_, bytes, length) != ESP_OK) {
    return false;
  }
  OtaImageTagScanner scanner;
  scanner.feed(bytes, length);
  return scanner.found() && sameTag(scanner.tag(), stagedTag_);
}

OtaResult ShotStopperOta::commit() {
  if (!started_ || !available_) {
    return OtaResult::UNAVAILABLE;
  }
  if (busy_ || sessionActive_) {
    return OtaResult::BUSY;
  }
  if (!stagedValid_) {
    return OtaResult::NOTHING_STAGED;
  }
  // Re-read the identity straight from flash: it proves the slot still holds
  // the exact image that was verified, not just that a verify once happened.
  if (!reconfirmStagedTag()) {
    return finishFailure(OtaResult::VERIFY_FAILED);
  }
  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  // esp_ota_set_boot_partition runs a full image verification of its own and
  // refuses to select a slot that would not boot.
  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    return finishFailure(OtaResult::COMMIT_FAILED);
  }
  stagedValid_ = false;
  state_ = OtaState::COMMITTED;
  return OtaResult::OK;
}

void ShotStopperOta::discard() {
  if (busy_ || state_ == OtaState::COMMITTED) {
    return;
  }
  clearSession(true);
  stagedValid_ = false;
  stagedTag_ = OtaImageTag{};
  stagedTagOffset_ = 0;
  stagedSizeBytes_ = 0;
  receivedBytes_ = 0;
  expectedBytes_ = 0;
  state_ = available_ ? OtaState::IDLE : OtaState::UNAVAILABLE;
}

bool ShotStopperOta::confirmRunningImage() {
  if (confirmed_) {
    return true;
  }
  // A rejected image must stay rejected: flipping it back to VALID would
  // cancel the armed rollback and leave the bootloader targeting the same
  // slot that just failed to prove itself.
  if (rejected_) {
    return false;
  }
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    return false;
  }
  confirmed_ = true;
  pendingVerify_ = false;
  return true;
}

bool ShotStopperOta::rejectRunningImage() {
  if (rejected_) {
    return true;
  }
  if (confirmed_ || busy_) {
    return false;
  }
  // Refusing here is the safe outcome: without a bootable alternative the
  // caller must keep running this image rather than restart into nothing.
  if (!esp_ota_check_rollback_is_possible()) {
    return false;
  }
  if (esp_ota_mark_app_invalid_rollback() != ESP_OK) {
    return false;
  }
  rejected_ = true;
  pendingVerify_ = false;
  return true;
}

const char *ShotStopperOta::resultName(OtaResult result) {
  switch (result) {
    case OtaResult::OK: return "OK";
    case OtaResult::UNAVAILABLE: return "UNAVAILABLE";
    case OtaResult::BUSY: return "BUSY";
    case OtaResult::PENDING_VERIFY: return "PENDING_VERIFY";
    case OtaResult::BAD_LENGTH: return "BAD_LENGTH";
    case OtaResult::TOO_LARGE: return "TOO_LARGE";
    case OtaResult::RECEIVE_FAILED: return "RECEIVE_FAILED";
    case OtaResult::BAD_IMAGE: return "BAD_IMAGE";
    case OtaResult::NO_TAG: return "NO_TAG";
    case OtaResult::ARCH_MISMATCH: return "ARCH_MISMATCH";
    case OtaResult::DOWNGRADE: return "DOWNGRADE";
    case OtaResult::WRITE_FAILED: return "WRITE_FAILED";
    case OtaResult::VERIFY_FAILED: return "VERIFY_FAILED";
    case OtaResult::SAFETY_LOST: return "SAFETY_LOST";
    case OtaResult::NOTHING_STAGED: return "NOTHING_STAGED";
    case OtaResult::COMMIT_FAILED: return "COMMIT_FAILED";
    case OtaResult::NO_MEMORY: return "NO_MEMORY";
    case OtaResult::NO_IDENTITY: return "NO_IDENTITY";
    case OtaResult::SESSION_REQUIRED: return "SESSION_REQUIRED";
    case OtaResult::SESSION_CONFLICT: return "OTA_SESSION_CONFLICT";
    case OtaResult::SESSION_IDENTITY_MISMATCH:
      return "OTA_SESSION_IDENTITY_MISMATCH";
    case OtaResult::OFFSET_MISMATCH: return "OTA_OFFSET_MISMATCH";
    case OtaResult::INVALID_RANGE: return "OTA_INVALID_RANGE";
    case OtaResult::HASH_MISMATCH: return "OTA_SHA256_MISMATCH";
    case OtaResult::SESSION_EXPIRED: return "OTA_SESSION_EXPIRED";
    case OtaResult::INTERNAL: return "INTERNAL";
  }
  return "UNKNOWN";
}

const char *ShotStopperOta::stateName(OtaState state) {
  switch (state) {
    case OtaState::UNAVAILABLE: return "unavailable";
    case OtaState::IDLE: return "idle";
    case OtaState::RECEIVING: return "receiving";
    case OtaState::STAGED: return "staged";
    case OtaState::COMMITTED: return "committed";
  }
  return "unknown";
}

}  // namespace shotstopper
