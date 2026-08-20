#pragma once

// Wi-Fi firmware updates for the Shot Stopper controller.
//
// The design goal is that no reachable failure can leave the machine without a
// bootable application:
//
//  * A transfer only ever writes the OTA slot that is NOT running, so the
//    working firmware survives a mid-upload power cut, reset or abort.
//  * The boot selection is changed only by an explicit, separate commit, after
//    the whole image passed header, identity and SHA-256 checks.
//  * A committed image boots as ESP_OTA_IMG_PENDING_VERIFY. It becomes
//    permanent only once the new firmware proves it can serve its Web UI; a
//    firmware that crashes, hangs or cannot bring up HTTP is rolled back to the
//    previous slot by the bootloader.

#include "ShotStopperOtaImage.h"

#include <stddef.h>
#include <stdint.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <sdkconfig.h>
// The last line of defence is the bootloader, not this firmware: it is what
// still recovers an image too broken to run any of the code below. Losing that
// silently, because a future core version ships a different configuration, is
// exactly the failure this whole module exists to prevent.
#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#error "Wi-Fi updates require CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif
#if !defined(CONFIG_APP_ROLLBACK_ENABLE)
#error "Wi-Fi updates require CONFIG_APP_ROLLBACK_ENABLE"
#endif
// An image that boots into an infinite loop before the task watchdog is armed
// is only ever recovered by the bootloader's own timer.
#if !defined(CONFIG_BOOTLOADER_WDT_ENABLE)
#error "Wi-Fi updates require CONFIG_BOOTLOADER_WDT_ENABLE"
#endif
#endif

namespace shotstopper {

enum class OtaState : uint8_t {
  UNAVAILABLE = 0,  // partition table has no usable second app slot
  IDLE,
  RECEIVING,
  STAGED,
  COMMITTED,  // boot partition switched; waiting for the restart
};

enum class OtaResult : uint8_t {
  OK = 0,
  UNAVAILABLE,
  BUSY,
  PENDING_VERIFY,  // this boot has not proven itself yet; esp_ota_begin refuses
  BAD_LENGTH,
  TOO_LARGE,
  RECEIVE_FAILED,
  BAD_IMAGE,
  NO_TAG,
  ARCH_MISMATCH,
  DOWNGRADE,
  WRITE_FAILED,
  VERIFY_FAILED,
  SAFETY_LOST,
  NOTHING_STAGED,
  COMMIT_FAILED,
  NO_MEMORY,
  // This build never declared which board it is for, so no image can be proven
  // compatible with it. Reachable on firmware not produced by ./scripts/build.
  NO_IDENTITY,
  INTERNAL,
};

struct OtaStatusSnapshot {
  OtaState state = OtaState::UNAVAILABLE;
  uint32_t slotBytes = 0;
  uint32_t receivedBytes = 0;
  uint32_t expectedBytes = 0;
  bool stagedValid = false;
  OtaImageTag staged = {};
  // The running image was booted by an OTA commit and has not been confirmed.
  bool pendingVerify = false;
  bool confirmed = false;
};

// Transport hooks supplied by the HTTP layer. Keeping them as plain function
// pointers keeps this module free of any dependency on esp_http_server.
struct OtaStreamIo {
  // Returns the number of bytes read, 0 if the peer closed early, or a
  // negative value on a transport error.
  int (*read)(void *context, uint8_t *buffer, size_t capacity) = nullptr;
  // Polled during the transfer. Returning false aborts immediately; used to
  // stop an update the moment the machine stops being safe to update.
  bool (*stillSafe)(void *context) = nullptr;
  void (*progress)(void *context, uint32_t received, uint32_t expected) =
      nullptr;
  void *context = nullptr;
};

class ShotStopperOta {
  public:
  static ShotStopperOta &instance();

  // Discovers the slot pair and records whether this boot is awaiting
  // confirmation. Safe to call more than once.
  void begin();

  bool available() const { return available_; }
  bool busy() const { return busy_; }
  uint32_t slotBytes() const { return slotBytes_; }
  OtaStatusSnapshot snapshot() const;

  // Streams an image into the inactive slot and validates it. Never changes
  // the boot selection.
  OtaResult stage(uint32_t contentLength, bool allowDowngrade,
                  const OtaStreamIo &io);
  // Points the bootloader at the staged image. The caller performs the
  // restart, so CN9 can be opened first.
  OtaResult commit();
  void discard();

  bool bootPendingVerify() const { return pendingVerify_; }
  bool runningImageConfirmed() const { return confirmed_; }
  bool runningImageRejected() const { return rejected_; }
  // Cancels the pending rollback: the running image becomes permanent.
  bool confirmRunningImage();
  // Selects the previous slot without rebooting, so the caller can restart
  // through the normal CN9-safe path. Returns false when no other slot holds a
  // bootable application, in which case staying on this image is the only
  // option that keeps the machine usable. After a successful reject, both
  // confirmRunningImage() and a second reject are no-ops, so a later tick
  // cannot silently cancel the armed rollback.
  bool rejectRunningImage();

  const OtaImageTag &runningTag() const { return runningTag_; }

  static const char *resultName(OtaResult result);
  static const char *stateName(OtaState state);

  private:
  ShotStopperOta() = default;

  OtaResult finishFailure(OtaResult result);
  bool reconfirmStagedTag();

  bool started_ = false;
  bool available_ = false;
  bool busy_ = false;
  bool pendingVerify_ = false;
  bool confirmed_ = false;
  bool rejected_ = false;
  bool stagedValid_ = false;
  OtaState state_ = OtaState::UNAVAILABLE;
  uint32_t slotBytes_ = 0;
  uint32_t receivedBytes_ = 0;
  uint32_t expectedBytes_ = 0;
  uint32_t stagedTagOffset_ = 0;
  uint32_t stagedSizeBytes_ = 0;
  OtaImageTag stagedTag_ = {};
  OtaImageTag runningTag_ = {};
  const void *runningPartition_ = nullptr;
  const void *targetPartition_ = nullptr;
  alignas(4) uint8_t chunkBuffer_[4096] = {};
};

}  // namespace shotstopper
