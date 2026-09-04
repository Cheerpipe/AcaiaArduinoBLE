#include "ShotStopperBleCompanionNimble.h"

#include "ShotStopperBleRuntime.h"

#include "esp_timer.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"

#include <string.h>

namespace shotstopper {
namespace {

constexpr uint16_t kNoConnection = BLE_HS_CONN_HANDLE_NONE;
constexpr size_t kCharacteristicCount =
    static_cast<size_t>(BleCompanionCharacteristic::Count);
constexpr size_t kRequestTypeCount = 9;

ShotStopperBleCompanion *gOwner = nullptr;

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0xfe, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

#define SHOT_STOPPER_COMPANION_UUID(suffixLow)                           \
  BLE_UUID128_INIT(suffixLow, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)

const ble_uuid128_t kCharacteristicUuids[kCharacteristicCount] = {
    SHOT_STOPPER_COMPANION_UUID(0x10),
    SHOT_STOPPER_COMPANION_UUID(0x11),
    SHOT_STOPPER_COMPANION_UUID(0x12),
    SHOT_STOPPER_COMPANION_UUID(0x13),
    SHOT_STOPPER_COMPANION_UUID(0x14),
    SHOT_STOPPER_COMPANION_UUID(0x15),
    SHOT_STOPPER_COMPANION_UUID(0x16),
    SHOT_STOPPER_COMPANION_UUID(0x17),
    SHOT_STOPPER_COMPANION_UUID(0x18),
    SHOT_STOPPER_COMPANION_UUID(0x19),
    SHOT_STOPPER_COMPANION_UUID(0x20),
    SHOT_STOPPER_COMPANION_UUID(0x21),
    SHOT_STOPPER_COMPANION_UUID(0x22),
    SHOT_STOPPER_COMPANION_UUID(0x23),
    SHOT_STOPPER_COMPANION_UUID(0x24),
    SHOT_STOPPER_COMPANION_UUID(0x25),
    SHOT_STOPPER_COMPANION_UUID(0x26),
};

#undef SHOT_STOPPER_COMPANION_UUID

BleCompanionCharacteristic kCharacteristicIds[kCharacteristicCount] = {
    BleCompanionCharacteristic::Enabled,
    BleCompanionCharacteristic::WeightValue,
    BleCompanionCharacteristic::ReedSwitch,
    BleCompanionCharacteristic::Momentary,
    BleCompanionCharacteristic::AutoTare,
    BleCompanionCharacteristic::MinShotDuration,
    BleCompanionCharacteristic::MaxShotDuration,
    BleCompanionCharacteristic::DripDelay,
    BleCompanionCharacteristic::FirmwareVersion,
    BleCompanionCharacteristic::ScaleStatus,
    BleCompanionCharacteristic::ShotStatus,
    BleCompanionCharacteristic::OtaRequested,
    BleCompanionCharacteristic::WifiSsid,
    BleCompanionCharacteristic::WifiPassword,
    BleCompanionCharacteristic::WifiIp,
    BleCompanionCharacteristic::Reboot,
    BleCompanionCharacteristic::EnableAp,
};

uint16_t kValueHandles[kCharacteristicCount] = {};

constexpr ble_gatt_chr_flags nimbleFlags(uint8_t properties) {
  ble_gatt_chr_flags flags = 0;
  if ((properties & BLE_COMPANION_READ) != 0) flags |= BLE_GATT_CHR_F_READ;
  if ((properties & BLE_COMPANION_WRITE) != 0) flags |= BLE_GATT_CHR_F_WRITE;
  if ((properties & BLE_COMPANION_NOTIFY) != 0) flags |= BLE_GATT_CHR_F_NOTIFY;
  return flags;
}

}  // namespace

ShotStopperBleCompanion::ShotStopperBleCompanion() {
  status_.protocolVersion = BLE_COMPANION_PROTOCOL_VERSION;
}

bool ShotStopperBleCompanion::prepare(EnqueueRequest enqueueRequest) {
  portENTER_CRITICAL(&mux_);
  const bool canPrepare = !prepared_ && gOwner == nullptr;
  if (canPrepare) {
    enqueueRequest_ = enqueueRequest;
    prepared_ = true;
    gOwner = this;
  }
  portEXIT_CRITICAL(&mux_);
  if (!canPrepare) return false;
  if (!shotStopperBleRuntimeConfigureGattProfile(registerGattThunk, this)) {
    portENTER_CRITICAL(&mux_);
    prepared_ = false;
    enqueueRequest_ = nullptr;
    if (gOwner == this) gOwner = nullptr;
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  return true;
}

bool ShotStopperBleCompanion::begin(EnqueueRequest enqueueRequest) {
  portENTER_CRITICAL(&mux_);
  if (enqueueRequest_ == nullptr) enqueueRequest_ = enqueueRequest;
  begun_ = prepared_ && registered_ && shotStopperBleRuntimeReady();
  status_.enabled = begun_;
  status_.stackReady = begun_;
  advertisingPaused_ = true;
  status_.advertising = false;
  forceSync_ = true;
  const bool begun = begun_;
  portEXIT_CRITICAL(&mux_);
  return begun;
}

int ShotStopperBleCompanion::registerGattThunk(void *context) {
  return context == nullptr
             ? BLE_HS_EINVAL
             : static_cast<ShotStopperBleCompanion *>(context)->registerGatt();
}

int ShotStopperBleCompanion::registerGatt() {
#define SHOT_STOPPER_GATT_CHR(index)                                      \
  {                                                                       \
    &kCharacteristicUuids[index].u, accessThunk,                          \
        &kCharacteristicIds[index], nullptr,                              \
        nimbleFlags(BLE_COMPANION_CHARACTERISTICS[index].properties), 0,  \
        &kValueHandles[index], nullptr                                    \
  }
  static const ble_gatt_chr_def characteristics[] = {
      SHOT_STOPPER_GATT_CHR(0),  SHOT_STOPPER_GATT_CHR(1),
      SHOT_STOPPER_GATT_CHR(2),  SHOT_STOPPER_GATT_CHR(3),
      SHOT_STOPPER_GATT_CHR(4),  SHOT_STOPPER_GATT_CHR(5),
      SHOT_STOPPER_GATT_CHR(6),  SHOT_STOPPER_GATT_CHR(7),
      SHOT_STOPPER_GATT_CHR(8),  SHOT_STOPPER_GATT_CHR(9),
      SHOT_STOPPER_GATT_CHR(10), SHOT_STOPPER_GATT_CHR(11),
      SHOT_STOPPER_GATT_CHR(12), SHOT_STOPPER_GATT_CHR(13),
      SHOT_STOPPER_GATT_CHR(14), SHOT_STOPPER_GATT_CHR(15),
      SHOT_STOPPER_GATT_CHR(16), {}};
#undef SHOT_STOPPER_GATT_CHR
  static const ble_gatt_svc_def services[] = {
      {BLE_GATT_SVC_TYPE_PRIMARY, &kServiceUuid.u, nullptr, characteristics},
      {}};

  int rc = ble_svc_gap_device_name_set(BLE_COMPANION_LOCAL_NAME);
  if (rc == 0) rc = ble_gatts_count_cfg(services);
  if (rc == 0) rc = ble_gatts_add_svcs(services);
  portENTER_CRITICAL(&mux_);
  registered_ = rc == 0;
  status_.lastRawError = rc;
  portEXIT_CRITICAL(&mux_);
  return rc;
}

int ShotStopperBleCompanion::accessThunk(
    uint16_t connHandle, uint16_t attrHandle,
    ble_gatt_access_ctxt *context, void *arg) {
  if (gOwner == nullptr || context == nullptr || arg == nullptr) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  return gOwner->access(connHandle, attrHandle, context,
                        *static_cast<BleCompanionCharacteristic *>(arg));
}

int ShotStopperBleCompanion::access(
    uint16_t connHandle, uint16_t attrHandle, ble_gatt_access_ctxt *context,
    BleCompanionCharacteristic characteristic) {
  (void)connHandle;
  (void)attrHandle;
  const BleCompanionCharacteristicSpec *spec =
      bleCompanionCharacteristicSpec(characteristic);
  if (spec == nullptr) return BLE_ATT_ERR_UNLIKELY;

  if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    if ((spec->properties & BLE_COMPANION_READ) == 0) {
      return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    BleCompanionRuntimeSnapshot snapshot;
    portENTER_CRITICAL(&mux_);
    snapshot = synced_;
    portEXIT_CRITICAL(&mux_);
    uint8_t value[WIFI_PASSWORD_CAPACITY] = {};
    const size_t length =
        readBleCompanionValue(characteristic, snapshot, value, sizeof(value));
    if (context->offset > length) return BLE_ATT_ERR_INVALID_OFFSET;
    const size_t remaining = length - context->offset;
    const int rc = os_mbuf_append(context->om, value + context->offset,
                                  static_cast<uint16_t>(remaining));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if ((spec->properties & BLE_COMPANION_WRITE) == 0) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  const uint16_t length = OS_MBUF_PKTLEN(context->om);
  if (length > spec->maxLength || length > WIFI_PASSWORD_CAPACITY) {
    if (characteristic == BleCompanionCharacteristic::WifiPassword) {
      portENTER_CRITICAL(&mux_);
      clearBleCompanionWifiStage(protocol_);
      portEXIT_CRITICAL(&mux_);
    }
    rejectLocal(BleCompanionRejectReason::INVALID_PAYLOAD);
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  uint8_t value[WIFI_PASSWORD_CAPACITY] = {};
  if (length > 0 &&
      ble_hs_mbuf_to_flat(context->om, value, sizeof(value), nullptr) != 0) {
    if (characteristic == BleCompanionCharacteristic::WifiPassword) {
      portENTER_CRITICAL(&mux_);
      clearBleCompanionWifiStage(protocol_);
      portEXIT_CRITICAL(&mux_);
    }
    memset(value, 0, sizeof(value));
    rejectLocal(BleCompanionRejectReason::INVALID_PAYLOAD);
    return BLE_ATT_ERR_UNLIKELY;
  }

  BleCompanionWriteResult result;
  portENTER_CRITICAL(&mux_);
  result = processBleCompanionWrite(protocol_, characteristic, value, length,
                                    static_cast<uint32_t>(esp_timer_get_time() /
                                                          1000));
  portEXIT_CRITICAL(&mux_);
  if (result.disposition == BleCompanionWriteDisposition::Rejected) {
    memset(value, 0, sizeof(value));
    rejectLocal(result.reject);
    return result.reject == BleCompanionRejectReason::INVALID_PAYLOAD
               ? BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN
               : 0;
  }
  if (result.disposition == BleCompanionWriteDisposition::Enqueue) {
    const uint32_t nowMs =
        static_cast<uint32_t>(esp_timer_get_time() / 1000);
    (void)enqueue(result.request, nowMs);
    memset(result.request.ssid, 0, sizeof(result.request.ssid));
    memset(result.request.password, 0, sizeof(result.request.password));
  }
  memset(value, 0, sizeof(value));
  return 0;
}

bool ShotStopperBleCompanion::enqueue(BleCompanionRequest &request,
                                      uint32_t nowMs) {
  const size_t pendingIndex = static_cast<size_t>(request.type);
  portENTER_CRITICAL(&mux_);
  const bool pending = pendingIndex >= kRequestTypeCount ||
                       pendingSequence_[pendingIndex] != 0;
  EnqueueRequest enqueueRequest = enqueueRequest_;
  if (!pending) {
    pendingSequence_[pendingIndex] = request.sequence;
    pendingAtMs_[pendingIndex] = nowMs;
  }
  portEXIT_CRITICAL(&mux_);
  if (pending) {
    rejectLocal(BleCompanionRejectReason::REQUEST_PENDING);
    return false;
  }
  if (enqueueRequest != nullptr && enqueueRequest(request)) return true;

  portENTER_CRITICAL(&mux_);
  if (pendingSequence_[pendingIndex] == request.sequence) {
    pendingSequence_[pendingIndex] = 0;
    pendingAtMs_[pendingIndex] = 0;
  }
  ++status_.rejectedWrites;
  status_.lastReject = BleCompanionRejectReason::QUEUE_FULL;
  portEXIT_CRITICAL(&mux_);
  return false;
}

void ShotStopperBleCompanion::service(
    const BleCompanionRuntimeSnapshot &snapshot, uint32_t nowMs) {
  expirePending(nowMs);
  bool notifications[4] = {};
  const BleCompanionCharacteristic notifyIds[4] = {
      BleCompanionCharacteristic::ScaleStatus,
      BleCompanionCharacteristic::ShotStatus,
      BleCompanionCharacteristic::WifiIp,
      BleCompanionCharacteristic::EnableAp,
  };
  const bool runtimeReady = shotStopperBleRuntimeReady();
  portENTER_CRITICAL(&mux_);
  expireBleCompanionWifiStage(protocol_, nowMs);
  status_.stackReady = begun_ && registered_ && runtimeReady;
  status_.apActive = snapshot.apActive;
  if (!status_.stackReady) {
    status_.advertising = false;
    status_.connected = false;
    connectionHandle_ = kNoConnection;
    clearBleCompanionWifiStage(protocol_);
  }
  for (size_t index = 0; index < 4; ++index) {
    notifications[index] = status_.stackReady && status_.connected &&
        bleCompanionNotificationChanged(notifyIds[index], synced_, snapshot,
                                        forceSync_);
  }
  synced_ = snapshot;
  forceSync_ = false;
  portEXIT_CRITICAL(&mux_);

  for (size_t index = 0; index < 4; ++index) {
    if (notifications[index]) {
      const size_t handleIndex = static_cast<size_t>(notifyIds[index]);
      if (kValueHandles[handleIndex] != 0) {
        ble_gatts_chr_updated(kValueHandles[handleIndex]);
      }
    }
  }
}

void ShotStopperBleCompanion::noteResult(const BleCompanionResult &result) {
  portENTER_CRITICAL(&mux_);
  clearPending(result.sequence);
  if (result.accepted) {
    ++status_.acceptedWrites;
  } else {
    ++status_.rejectedWrites;
    status_.lastReject = result.reason;
  }
  forceSync_ = true;
  portEXIT_CRITICAL(&mux_);
}

BleCompanionStatusSnapshot ShotStopperBleCompanion::status() const {
  portENTER_CRITICAL(&mux_);
  const BleCompanionStatusSnapshot snapshot = status_;
  portEXIT_CRITICAL(&mux_);
  return snapshot;
}

void ShotStopperBleCompanion::setAdvertisingPaused(bool paused) {
  portENTER_CRITICAL(&mux_);
  advertisingPaused_ = paused;
  const bool stop = paused && status_.advertising;
  const bool start = !paused && status_.stackReady && !status_.connected &&
                     !status_.advertising;
  if (stop) status_.advertising = false;
  portEXIT_CRITICAL(&mux_);

  if (stop) {
    const int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
      portENTER_CRITICAL(&mux_);
      status_.lastRawError = rc;
      portEXIT_CRITICAL(&mux_);
    }
  } else if (start) {
    (void)startAdvertising();
  }
}

bool ShotStopperBleCompanion::startAdvertising() {
  ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = const_cast<ble_uuid128_t *>(&kServiceUuid);
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  int rc = ble_gap_adv_set_fields(&fields);

  ble_hs_adv_fields response = {};
  response.name = reinterpret_cast<uint8_t *>(
      const_cast<char *>(BLE_COMPANION_LOCAL_NAME));
  response.name_len = strlen(BLE_COMPANION_LOCAL_NAME);
  response.name_is_complete = 1;
  if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&response);

  ble_gap_adv_params params = {};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  params.itvl_min = BLE_COMPANION_ADV_INTERVAL;
  params.itvl_max = BLE_COMPANION_ADV_INTERVAL;
  if (rc == 0) {
    rc = ble_gap_adv_start(shotStopperBleRuntimeOwnAddressType(), nullptr,
                           BLE_HS_FOREVER, &params, gapEventThunk, this);
  }

  portENTER_CRITICAL(&mux_);
  if (rc == 0) {
    status_.advertising = true;
    ++status_.advertisingStarts;
  } else {
    status_.advertising = false;
    status_.lastRawError = rc;
    ++status_.advertisingFailures;
  }
  portEXIT_CRITICAL(&mux_);
  return rc == 0;
}

int ShotStopperBleCompanion::gapEventThunk(ble_gap_event *event,
                                           void *context) {
  return context == nullptr
             ? 0
             : static_cast<ShotStopperBleCompanion *>(context)->onGapEvent(
                   event);
}

int ShotStopperBleCompanion::onGapEvent(ble_gap_event *event) {
  if (event == nullptr) return 0;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      portENTER_CRITICAL(&mux_);
      status_.advertising = false;
      status_.lastRawError = event->connect.status;
      if (event->connect.status == 0) {
        connectionHandle_ = event->connect.conn_handle;
        status_.connected = true;
        ++status_.phoneConnects;
        forceSync_ = true;
      }
      portEXIT_CRITICAL(&mux_);
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      portENTER_CRITICAL(&mux_);
      if (connectionHandle_ == event->disconnect.conn.conn_handle) {
        status_.lastRawError = event->disconnect.reason;
        ++status_.phoneDisconnects;
        status_.connected = false;
        status_.advertising = false;
        connectionHandle_ = kNoConnection;
        clearBleCompanionWifiStage(protocol_);
      }
      portEXIT_CRITICAL(&mux_);
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      portENTER_CRITICAL(&mux_);
      status_.advertising = false;
      status_.lastRawError = event->adv_complete.reason;
      portEXIT_CRITICAL(&mux_);
      return 0;
    default:
      return 0;
  }
}

void ShotStopperBleCompanion::rejectLocal(
    BleCompanionRejectReason reason) {
  portENTER_CRITICAL(&mux_);
  ++status_.rejectedWrites;
  status_.lastReject = reason;
  portEXIT_CRITICAL(&mux_);
}

void ShotStopperBleCompanion::clearPending(uint32_t sequence) {
  if (sequence == 0) return;
  for (size_t index = 0; index < kRequestTypeCount; ++index) {
    if (pendingSequence_[index] == sequence) {
      pendingSequence_[index] = 0;
      pendingAtMs_[index] = 0;
      return;
    }
  }
}

void ShotStopperBleCompanion::expirePending(uint32_t nowMs) {
  portENTER_CRITICAL(&mux_);
  for (size_t index = 0; index < kRequestTypeCount; ++index) {
    if (pendingSequence_[index] != 0 &&
        static_cast<uint32_t>(nowMs - pendingAtMs_[index]) >=
            BLE_COMPANION_REQUEST_TIMEOUT_MS) {
      pendingSequence_[index] = 0;
      pendingAtMs_[index] = 0;
      ++status_.rejectedWrites;
      status_.lastReject = BleCompanionRejectReason::COMMAND_REJECTED;
    }
  }
  portEXIT_CRITICAL(&mux_);
}

}  // namespace shotstopper
