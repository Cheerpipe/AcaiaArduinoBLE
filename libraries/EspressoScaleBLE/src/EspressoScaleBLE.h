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

#define LIBRARY_VERSION                 "4.1.0"
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

#if defined(ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE)
#include "Arduino.h"
#include <ArduinoBLE.h>
#endif

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
    CONNECTION_FAILED_TO_ESTABLISH
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
        // Native backend status for diagnostics. ArduinoBLE cannot expose a
        // stable raw status and returns zero; NimBLE preserves the last
        // ble_hs/HCI value without collapsing it into a domain reason.
        int32_t lastBackendStatus() const;
        int linkRssi();

    private:
#if defined(ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE)
        bool isScaleName(const char *name) const;
        bool configureCharacteristics(BLEDevice& peripheral,
                                      const ScaleProtocol *protocol);
        bool parseWeightPacket(const uint8_t data[], int length,
                               float& weight) const;
        bool parseTimerPacket(const uint8_t data[], int length,
                              uint32_t& timerMs) const;
        bool supportedPacketLength(int length) const;
        ScaleCommandResult writeCommand(const uint8_t command[], int length);
        ScaleCommandResult writeOp(ScaleOp op, uint8_t arg = 0);
        bool beginConnection(BLEDevice& peripheral);
        bool advanceConnection();
        bool finishConnectionSuccess();
        bool detectAndConfigureScale();
        bool runInitWrites();
        void clearConnectingState();
        void logVersionOnce();
        void stopIdleScan(ScaleDisconnectReason reason);
        uint32_t maxPacketPeriodMs() const;

        void retainCharacteristic(BLECharacteristic& destination,
                                  const BLECharacteristic& source);
        void clearCharacteristic(BLECharacteristic& characteristic);
        void rememberPeripheral(const BLEDevice& peripheral);
        void clearPeripheral();
        void resetConnection(bool disconnectPeer,
                             ScaleDisconnectReason reason);
        void rejectPacket(const char* reason);
        ScaleDisconnectReason mapHciDisconnectReason() const;

        void exploreService(BLEService service);
        void exploreCharacteristic(BLECharacteristic characteristic);
        void exploreDescriptor(BLEDescriptor descriptor);

        float               _currentWeight;
        uint32_t            _currentTimerMs;
        uint32_t            _lastTimerPacket;
        bool                _hasTimer;
        BLECharacteristic   _write;
        BLECharacteristic   _read;
        BLEDevice           _peripheral;
        uint32_t            _lastHeartBeat;
        uint32_t            _connectedAt;
        uint32_t            _lastPacket;
        uint32_t            _packetPeriod;
        uint32_t            _rejectedPackets;
        uint8_t             _consecutiveRejectedPackets;
        uint32_t            _reconnects;
        uint32_t            _successfulConnections;
        bool                _hasPeripheral;
        bool                _hasValidPacket;
        uint32_t            _scanStartedAt;
        bool                _scanning;
        bool                _connected;
        bool                _connecting;
        bool                _loggedVersion;
        const ScaleProtocol *_protocol;
        bool                _debug;
        enum class ConnectStep : uint8_t {
            Idle = 0,
            Settle,
            Connect,
            Discover,
            Configure,
            Subscribe,
            InitWrites
        };
        ConnectStep         _connectStep;
        uint32_t            _connectStartedAt;
        uint32_t            _connectSettleStartedAt;
        uint8_t             _connectAttempts;
        uint32_t            _linkDownSince;
        char                _scanMac[SCALE_MAC_CAPACITY];
        bool                _scanAddressFilter;
        uint16_t            _scanInterval;
        uint16_t            _scanWindow;
        char                _address[SCALE_MAC_CAPACITY];
        char                _localName[SCALE_NAME_CAPACITY];
        char                _seenMac[SCALE_MAC_CAPACITY];
        char                _seenName[SCALE_NAME_CAPACITY];
        bool                _seenPending;
        ScaleBleTimingSnapshot _timingSnapshot;
        ScaleDisconnectReason _lastDisconnectReason;
#else
        // Placement storage keeps the implementation private, fixed-size and
        // allocation-free while preventing NimBLE types from leaking through
        // the public facade into ShotStopperScaleWorker.
        static constexpr size_t NIMBLE_CLIENT_STORAGE_SIZE = 2176;
        alignas(8) uint8_t _nimbleClientStorage[NIMBLE_CLIENT_STORAGE_SIZE];
#endif
};

#endif
