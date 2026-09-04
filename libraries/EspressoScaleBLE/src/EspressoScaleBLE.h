/*
  EspressoScaleBLE.h - backend-neutral BLE client facade for espresso scales.

  Maintainer: Felipe Urzúa <cheerpipe@gmail.com>
  https://github.com/Cheerpipe/AcaiaArduinoBLE

  Originally created by Tate Mazer, December 13, 2023.
  Felicita Arc support by Pio Baettig and A-TWJ.
  Protocol-driver refactor for multi-vendor scales.

  Known limitation: weights are reported in grams only.
*/
#ifndef EspressoScaleBLE_h
#define EspressoScaleBLE_h

#define LIBRARY_VERSION                 "5.0.0"
#define WRITE_CHAR_OLD_VERSION          "2a80"
#define READ_CHAR_OLD_VERSION           "2a80"
#define WRITE_CHAR_NEW_VERSION          "49535343-8841-43f4-a8d4-ecbe34729bb3"
#define READ_CHAR_NEW_VERSION           "49535343-1e4d-4bd9-ba61-23c647249616"
#define WRITE_CHAR_GENERIC              "ff12"
#define READ_CHAR_GENERIC               "ff11"
#define WRITE_CHAR_FELICITA             "ffe1"
#define READ_CHAR_FELICITA              "ffe1"
#define WRITE_CHAR_ECLAIR               "4F9A45BA-8E1B-4E07-E157-0814D393B968"
#define READ_CHAR_ECLAIR                "AD736C5F-BBC9-1F96-D304-CB5D5F41E160"
#define HEARTBEAT_PERIOD_MS              2750UL
#define FIRST_PACKET_TIMEOUT_MS          5000UL
#define MAX_PACKET_PERIOD_MS             5000UL
#define GENERIC_MAX_PACKET_PERIOD_MS     8000UL
#define SCALE_SCAN_TIMEOUT_MS            3000UL
#define BLE_OPERATION_TIMEOUT_MS          1000UL
#define BLE_CONNECT_TIMEOUT_MS            2000UL
#define BLE_DISCOVER_TIMEOUT_MS           3000UL
#define SCALE_CONNECT_SETTLE_MS           120UL
#define LINK_DOWN_DEBOUNCE_MS             120UL
// GAP scan duty while discovering. Connecting and GATT-up paths never start
// a scan. Intervals avoid 20/60/100/120 ms advertising harmonics.
// Light 25% (28.75/115 ms), Normal 50% (31.25/62.5 ms), Aggressive 100% (20/20).
#define BLE_SCAN_LIGHT_INTERVAL           0x00B8
#define BLE_SCAN_LIGHT_WINDOW             0x002E
#define BLE_SCAN_NORMAL_INTERVAL          0x0064
#define BLE_SCAN_NORMAL_WINDOW            0x0032
#define BLE_SCAN_AGGRESSIVE_INTERVAL      0x0020
#define BLE_SCAN_AGGRESSIVE_WINDOW        0x0020
#define SCALE_CONNECT_ATTEMPTS           3U
#define SCALE_CONNECT_BUDGET_MS         10000UL
#define MAX_BLE_PACKET_LENGTH           20
#define MAX_SUPPORTED_WEIGHT_GRAMS      10000.0f
#define MAX_CONSECUTIVE_REJECTED_PACKETS 8U
#define SCALE_MAC_CAPACITY               18U
#define SCALE_NAME_CAPACITY              32U
#define SCALE_LINK_RSSI_UNAVAILABLE      127
#define ACAIA_MAC_CAPACITY               SCALE_MAC_CAPACITY
#define ACAIA_NAME_CAPACITY              SCALE_NAME_CAPACITY

#include "ScaleBleBackend.h"
#include "ScaleBleTypes.h"
#include "ScaleFeatures.h"
#include "ScaleProtocol.h"
#include <stddef.h>
#include <stdint.h>

enum scale_type {
    OLD,
    NEW,
    GENERIC,
    FELICITA,
    ECLAIR
};

enum class ScaleDisconnectReason : uint8_t {
    NONE,
    USER_REQUEST,
    SCAN_START_FAILED,
    SCAN_TIMEOUT,
    CONNECT_FAILED,
    DISCOVERY_FAILED,
    UNSUPPORTED_SCALE,
    SUBSCRIBE_FAILED,
    INITIALIZATION_WRITE_FAILED,
    REMOTE_DISCONNECTED,
    FIRST_PACKET_TIMEOUT,
    PACKET_TIMEOUT,
    INVALID_PACKET_STREAM,
    COMMAND_WRITE_FAILED,
    SUPERVISION_TIMEOUT,
    CONNECTION_FAILED_TO_ESTABLISH,
    RX_QUEUE_OVERFLOW,
    EVENT_QUEUE_OVERFLOW,
    HOST_RESET,
    OPERATION_TIMEOUT,
    MBUF_ALLOCATION_FAILED
};

class EspressoScaleBLE {
    public:
        explicit EspressoScaleBLE(bool debug);
        ~EspressoScaleBLE();

        EspressoScaleBLE(const EspressoScaleBLE&) = delete;
        EspressoScaleBLE& operator=(const EspressoScaleBLE&) = delete;

        bool init(const char *mac = nullptr);

        bool startScan(const char *mac = nullptr, bool forceRestart = false,
                       uint16_t interval = BLE_SCAN_NORMAL_INTERVAL,
                       uint16_t window = BLE_SCAN_NORMAL_WINDOW,
                       bool addressScan = false);
        bool pollScan();
        bool isScanning() const;
        bool isConnecting() const;

        void disconnect();

        ScaleCommandResult tare();
        ScaleCommandResult startTimer();
        ScaleCommandResult stopTimer();
        ScaleCommandResult resetTimer();
        ScaleCommandResult tareStartTimer();
        bool supportsTareStartTimer() const;

        ScaleCommandResult beep();
        bool supportsIndependentBeep() const;
        bool supportsCommandFeedback() const;
        ScaleCommandResult beepWithoutStateChange();
        ScaleCommandResult setBeepLevel(uint8_t level);

        ScaleCommandResult heartbeat();
        float getWeight() const;
        bool hasTimer() const;
        uint32_t getTimerMs() const;
        uint32_t lastTimerAgeMs() const;
        bool heartbeatRequired() const;
        bool isConnected();
        bool isLinkUp() const;
        bool newWeightAvailable();
        ScaleFeatureSet features() const;
        const char* connectedProtocolName() const;
        const char* address() const;
        const char* localName() const;
        bool isDirectedScan() const;
        bool takeSeenAdvertisement(char *macOut, size_t macCapacity,
                                   char *nameOut, size_t nameCapacity);

        ScaleDisconnectReason lastDisconnectReason() const;
        const char* lastDisconnectReasonName() const;
        uint8_t connectAttemptCount() const;
        uint8_t connectStepId() const;
        uint32_t lastValidPacketAgeMs() const;
        uint32_t rejectedPacketCount() const;
        uint32_t reconnectCount() const;
        ScaleBleTimingSnapshot timingSnapshot() const;
        // Native NimBLE status for diagnostics. Preserve the last ble_hs/HCI
        // value without collapsing it into a domain reason.
        int32_t lastBackendStatus() const;
        ScaleBleBackendHealth backendHealth() const;
        int linkRssi();

    private:
        // Placement storage keeps the implementation private, fixed-size and
        // allocation-free while preventing NimBLE types from leaking through
        // the public facade into ShotStopperScaleWorker.
        static constexpr size_t NIMBLE_CLIENT_STORAGE_SIZE = 3072;
        alignas(8) uint8_t _nimbleClientStorage[NIMBLE_CLIENT_STORAGE_SIZE];
};

#endif
