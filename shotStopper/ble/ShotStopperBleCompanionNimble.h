#pragma once

#include "../ShotStopperBleCompanion.h"
#include "ShotStopperBleCompanionProtocol.h"

#include "freertos/FreeRTOS.h"

#include <stddef.h>
#include <stdint.h>

struct ble_gap_event;
struct ble_gatt_access_ctxt;

namespace shotstopper {

class ShotStopperBleCompanion {
 public:
  using EnqueueRequest = bool (*)(const BleCompanionRequest &request);

  ShotStopperBleCompanion();

  // Installs the static GATT registration hook. This must run after the
  // request queue exists and before shotStopperBleRuntimeStart().
  bool prepare(EnqueueRequest enqueueRequest);
  bool begin(EnqueueRequest enqueueRequest);
  void service(const BleCompanionRuntimeSnapshot &snapshot, uint32_t nowMs);
  void noteResult(const BleCompanionResult &result);
  BleCompanionStatusSnapshot status() const;
  void setAdvertisingPaused(bool paused);

 private:
  static int registerGattThunk(void *context);
  static int accessThunk(uint16_t connHandle, uint16_t attrHandle,
                         ::ble_gatt_access_ctxt *context, void *arg);
  static int gapEventThunk(::ble_gap_event *event, void *context);

  int registerGatt();
  int access(uint16_t connHandle, uint16_t attrHandle,
             ::ble_gatt_access_ctxt *context,
             BleCompanionCharacteristic characteristic);
  int onGapEvent(::ble_gap_event *event);
  bool startAdvertising();
  bool enqueue(BleCompanionRequest &request, uint32_t nowMs);
  void rejectLocal(BleCompanionRejectReason reason);
  void clearPending(uint32_t sequence);
  void expirePending(uint32_t nowMs);

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  EnqueueRequest enqueueRequest_ = nullptr;
  BleCompanionStatusSnapshot status_ = {};
  BleCompanionRuntimeSnapshot synced_ = {};
  BleCompanionProtocolState protocol_ = {};
  uint32_t pendingSequence_[9] = {};
  uint32_t pendingAtMs_[9] = {};
  uint16_t connectionHandle_ = 0xffff;
  bool prepared_ = false;
  bool registered_ = false;
  bool begun_ = false;
  bool advertisingPaused_ = true;
  bool forceSync_ = true;
};

}  // namespace shotstopper
