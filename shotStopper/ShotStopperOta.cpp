#include "ShotStopperOta.h"

#include "ShotStopperPsram.h"
#include "ShotStopperVersion.h"
#include "ShotStopperWatchdog.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_wifi.h>
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

constexpr size_t OTA_TAG_REREAD_BYTES =
    OTA_TAG_PREFIX_CAPACITY + OTA_TAG_BODY_CAPACITY;

bool sameTag(const OtaImageTag &left, const OtaImageTag &right) {
  return left.valid && right.valid && left.packed == right.packed &&
         strncmp(left.arch, right.arch, OTA_ARCH_CAPACITY) == 0 &&
         strncmp(left.version, right.version, OTA_VERSION_CAPACITY) == 0;
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
  return copy;
}

OtaResult ShotStopperOta::finishFailure(OtaResult result) {
  // Keep this evidence after clearing the active transfer. The HTTP handler
  // reads its snapshot after stage() returns, so resetting first used to make
  // every failed upload look as though it had failed at byte zero.
  lastResult_ = result;
  lastReceivedBytes_ = receivedBytes_;
  lastExpectedBytes_ = expectedBytes_;
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

OtaResult ShotStopperOta::stage(uint32_t contentLength, bool allowDowngrade,
                                const OtaStreamIo &io) {
  if (!started_ || !available_) {
    return OtaResult::UNAVAILABLE;
  }
  if (busy_ || state_ == OtaState::COMMITTED) {
    return OtaResult::BUSY;
  }
  // esp_ota_begin refuses to write while the running slot is still
  // ESP_OTA_IMG_PENDING_VERIFY, and rightly so: overwriting the only other
  // slot would leave nothing to roll back to.
  if (!confirmed_) {
    return OtaResult::PENDING_VERIFY;
  }
  if (io.read == nullptr || io.stillSafe == nullptr) {
    return OtaResult::INTERNAL;
  }
  if (!otaArchIsUsable(runningTag_.arch)) {
    return OtaResult::NO_IDENTITY;
  }
  if (contentLength < OTA_MIN_IMAGE_BYTES) {
    return OtaResult::BAD_LENGTH;
  }
  if (contentLength > slotBytes_) {
    return OtaResult::TOO_LARGE;
  }

  busy_ = true;
  state_ = OtaState::RECEIVING;
  stagedValid_ = false;
  stagedTag_ = OtaImageTag{};
  receivedBytes_ = 0;
  expectedBytes_ = contentLength;
  // Idle modem sleep mid-transfer drops the TCP session while this task is
  // blocked on flash writes; keep STA awake until finishFailure() clears busy_.
  (void)esp_wifi_set_ps(WIFI_PS_NONE);

  const esp_partition_t *target =
      static_cast<const esp_partition_t *>(targetPartition_);
  if (target == nullptr) {
    // available_ was established at boot, but do not leave OTA permanently
    // busy if an unexpected partition lookup inconsistency is ever reached.
    return finishFailure(OtaResult::UNAVAILABLE);
  }

  OtaChunkBuffer chunk(OTA_CHUNK_BYTES);
  if (!chunk.ok()) {
    busy_ = false;
    state_ = OtaState::IDLE;
    expectedBytes_ = 0;
    return OtaResult::NO_MEMORY;
  }
  size_t chunkBytes = OTA_CHUNK_BYTES;
  uint8_t *const buffer = chunk.bytes;

  // Flash erase plus the closing SHA-256 pass exceed the normal watchdog
  // budget; restored by the destructor on every exit path below.
  TaskWatchdogOtaWindow watchdogWindow;

  OtaImageTagScanner scanner;
  uint8_t headerBytes[OTA_IMAGE_PREFIX_BYTES] = {};
  size_t headerCollected = 0;
  bool headerAccepted = false;
  bool tagChecked = false;
  esp_ota_handle_t handle = 0;
  bool handleOpen = false;
  uint32_t received = 0;
  uint32_t nextProgress = OTA_PROGRESS_INTERVAL_BYTES;
  OtaResult failure = OtaResult::OK;

  while (received < contentLength) {
    // Shot always wins: abort as soon as the paddle moves, a cycle starts, or
    // K1 closes. Do not wait for a byte interval — Wi-Fi would already be
    // competing with the scale.
    if (!io.stillSafe(io.context)) {
      failure = OtaResult::SAFETY_LOST;
      break;
    }
    const uint32_t remaining = contentLength - received;
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

    // Nothing reaches flash until the image header proves this is an
    // ESP32-S3 Arduino application, so a stray file costs no flash wear.
    if (!headerAccepted) {
      const size_t copy =
          length < sizeof(headerBytes) - headerCollected
              ? length
              : sizeof(headerBytes) - headerCollected;
      memcpy(headerBytes + headerCollected, buffer, copy);
      headerCollected += copy;
      if (headerCollected < sizeof(headerBytes)) {
        scanner.feed(buffer, length);
        received += static_cast<uint32_t>(length);
        receivedBytes_ = received;
        continue;
      }
      if (validateOtaImageHeader(headerBytes, headerCollected) !=
          OtaImageHeaderResult::OK) {
        failure = OtaResult::BAD_IMAGE;
        break;
      }
      headerAccepted = true;
      if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) !=
          ESP_OK) {
        failure = OtaResult::WRITE_FAILED;
        break;
      }
      handleOpen = true;
      // Flush the bytes that were held back for the header check.
      if (esp_ota_write(handle, headerBytes, headerCollected) != ESP_OK) {
        failure = OtaResult::WRITE_FAILED;
        break;
      }
      const size_t tail = length - copy;
      if (tail > 0 && esp_ota_write(handle, buffer + copy, tail) != ESP_OK) {
        failure = OtaResult::WRITE_FAILED;
        break;
      }
    } else if (esp_ota_write(handle, buffer, length) != ESP_OK) {
      failure = OtaResult::WRITE_FAILED;
      break;
    }

    scanner.feed(buffer, length);
    received += static_cast<uint32_t>(length);
    receivedBytes_ = received;

    // The tag sits in the first read-only segment, so a wrong-board image is
    // rejected seconds in rather than after a full transfer.
    if (!tagChecked && scanner.found()) {
      tagChecked = true;
      const OtaImageTag &tag = scanner.tag();
      if (!otaArchIsUsable(tag.arch)) {
        failure = OtaResult::NO_TAG;
        break;
      }
      if (strncmp(tag.arch, runningTag_.arch, OTA_ARCH_CAPACITY) != 0) {
        failure = OtaResult::ARCH_MISMATCH;
        break;
      }
      if (!allowDowngrade && tag.packed < FW_VERSION_PACKED) {
        failure = OtaResult::DOWNGRADE;
        break;
      }
    }

    if (io.progress != nullptr && received >= nextProgress) {
      nextProgress = received + OTA_PROGRESS_INTERVAL_BYTES;
      io.progress(io.context, received, contentLength);
    }
    // The HTTP server task is not a watchdog subscriber, so there is nothing to
    // feed here: the widened window above is what keeps the subscribed control
    // tasks from tripping while the cache is held during flash writes.
  }

  if (failure == OtaResult::OK && (!headerAccepted || !handleOpen)) {
    // Only reachable if the peer sent fewer bytes than it promised, which the
    // read loop already treats as a failure; keeping the check means no path
    // can reach esp_ota_end() without a handle.
    failure = OtaResult::RECEIVE_FAILED;
  }
  if (failure == OtaResult::OK && !scanner.found()) {
    failure = OtaResult::NO_TAG;
  }

  if (failure != OtaResult::OK) {
    if (handleOpen) {
      esp_ota_abort(handle);
    }
    return finishFailure(failure);
  }

  // esp_ota_end re-reads the whole slot and verifies the appended SHA-256, so
  // a transfer that was silently corrupted in flight fails here.
  const esp_err_t endStatus = esp_ota_end(handle);
  if (endStatus != ESP_OK) {
    return finishFailure(OtaResult::VERIFY_FAILED);
  }

  // One last look at the machine before advertising the image as flashable.
  if (!io.stillSafe(io.context)) {
    return finishFailure(OtaResult::SAFETY_LOST);
  }

  stagedTag_ = scanner.tag();
  stagedTagOffset_ = scanner.tagOffset();
  stagedSizeBytes_ = received;
  stagedValid_ = true;
  busy_ = false;
  state_ = OtaState::STAGED;
  receivedBytes_ = received;
  lastResult_ = OtaResult::OK;
  lastReceivedBytes_ = received;
  lastExpectedBytes_ = contentLength;
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
  if (busy_) {
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
