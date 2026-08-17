/*
  AcaiaArduinoBLE.h - Library for connecting to an Acaia-compatible scale
  using ArduinoBLE.

  Created by Tate Mazer, December 13, 2023.
  Felicita Arc support by Pio Baettig and A-TWJ.
  Released into the public domain.

  Known limitation: weights are reported in grams only.
*/
#ifndef AcaiaArduinoBLE_h
#define AcaiaArduinoBLE_h

#define LIBRARY_VERSION                 "3.7.0"
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
#define SCALE_SCAN_TIMEOUT_MS            3000UL
#define BLE_OPERATION_TIMEOUT_MS          1000UL
#define MAX_SUPPORTED_WEIGHT_GRAMS      10000.0f
#define MAX_CONSECUTIVE_REJECTED_PACKETS 8U
#define ACAIA_MAC_CAPACITY               18U
#define ACAIA_NAME_CAPACITY              32U

#include "Arduino.h"
#include <ArduinoBLE.h>

enum scale_type {
    OLD,     // Lunar (pre-2021)
    NEW,     // Lunar (2021), Pyxis
    GENERIC, // Bookoo Themis, Decent, etc. (ff11/ff12)
    FELICITA, // Felicita Arc (ffe1)
    ECLAIR    // AtomHeart Eclair
};

enum class AcaiaDisconnectReason : uint8_t {
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
    COMMAND_WRITE_FAILED
};

class AcaiaArduinoBLE {
    public:
        explicit AcaiaArduinoBLE(bool debug);
        ~AcaiaArduinoBLE();

        // BLECharacteristic in ArduinoBLE 2.1.0 is not safely assignable.
        // Prevent copying this owner object so its retained handles cannot be
        // duplicated through the same defective assignment path.
        AcaiaArduinoBLE(const AcaiaArduinoBLE&) = delete;
        AcaiaArduinoBLE& operator=(const AcaiaArduinoBLE&) = delete;

        // Blocking helper for sketches: reset, scan up to
        // SCALE_SCAN_TIMEOUT_MS, then connect. Prefer startScan()/pollScan()
        // on a dedicated BLE owner task so idle scanning does not block.
        // mac may be nullptr or empty for a name scan.
        bool init(const char *mac = nullptr);

        // Non-blocking GAP name scan (BLE.scan). Never calls BLE.begin()/end().
        // If a scan is already active with the same connect filter, this is a
        // no-op success. A different filter (or forceRestart) stops and restarts.
        // mac may be nullptr or empty: connect the first compatible scale.
        // mac non-empty: still name-scan all advertisements, but only GATT-
        // connect when the address matches (other compatible scales are
        // reported via takeSeenAdvertisement without connecting).
        bool startScan(const char *mac = nullptr, bool forceRestart = false);
        // Poll an active scan. Performs GATT connect only when a scale
        // advertisement matches the connect policy. Idle scans stay enabled
        // until a connect, filter change, or init()'s SCALE_SCAN_TIMEOUT_MS.
        // Returns true if connected after this call.
        bool pollScan();
        bool isScanning() const;

        void disconnect();

        bool tare();
        bool startTimer();
        bool stopTimer();
        bool resetTimer();
        bool tareStartTimer();
        bool supportsTareStartTimer() const;

        // Kept for source compatibility. It is now side-effect-free and only
        // succeeds on protocols with an independent buzzer command.
        bool beep();
        bool supportsIndependentBeep() const;
        // True when normal tare/timer commands are known to provide their own
        // audible confirmation. Eclair does not document this feedback.
        bool supportsCommandFeedback() const;
        bool beepWithoutStateChange();
        // Bookoo/generic only. Opcode 0x02; level 0 mutes, 1–5 set volume.
        bool setBeepLevel(uint8_t level);

        bool heartbeat();
        float getWeight() const;
        bool hasTimer() const;
        uint32_t getTimerMs() const;
        uint32_t lastTimerAgeMs() const;
        bool heartbeatRequired() const;
        bool isConnected();
        bool newWeightAvailable();
        const char* connectedProtocolName() const;
        // Empty when not connected / no remembered peripheral. Pointers are
        // owned by this object and remain valid until the next scan/reset.
        const char* address() const;
        const char* localName() const;
        // True while scanning with a non-empty connect-filter MAC.
        bool isDirectedScan() const;
        // If pollScan observed a compatible advertisement this call, copies
        // MAC/name and returns true (clears the pending flag).
        bool takeSeenAdvertisement(char *macOut, size_t macCapacity,
                                   char *nameOut, size_t nameCapacity);

        AcaiaDisconnectReason lastDisconnectReason() const;
        const char* lastDisconnectReasonName() const;
        uint32_t lastValidPacketAgeMs() const;
        uint32_t rejectedPacketCount() const;
        uint32_t reconnectCount() const;

    private:
        bool isScaleName(const char *name) const;
        bool configureCharacteristics(BLEDevice& peripheral,
                                      scale_type type,
                                      const char* writeUuid,
                                      const char* readUuid);
        bool parseWeightPacket(const byte data[], int length,
                               float& weight) const;
        bool parseTimerPacket(const byte data[], int length,
                              uint32_t& timerMs) const;
        bool parseAcaiaNewPacket(const byte data[], int length,
                                 float& weight) const;
        bool parseAcaiaOldPacket(const byte data[], int length,
                                 float& weight) const;
        bool parseGenericPacket(const byte data[], int length,
                                float& weight) const;
        bool parseFelicitaPacket(const byte data[], int length,
                                 float& weight) const;
        bool parseEclairPacket(const byte data[], int length,
                               float& weight) const;
        bool parseEclairTimerPacket(const byte data[], int length,
                                    uint32_t& timerMs) const;
        bool validAcaiaChecksum(const byte data[], int length) const;
        bool validWeight(float weight) const;
        bool supportedPacketLength(int length) const;
        bool writeCommand(const byte command[], int length);
        bool completeConnection(BLEDevice& peripheral);
        void logVersionOnce();
        void stopIdleScan(AcaiaDisconnectReason reason);

        // These helpers use destruction + copy construction intentionally.
        // ArduinoBLE's BLECharacteristic copy constructor retains ownership;
        // its implicit copy-assignment operator does not.
        void retainCharacteristic(BLECharacteristic& destination,
                                  const BLECharacteristic& source);
        void clearCharacteristic(BLECharacteristic& characteristic);
        void rememberPeripheral(const BLEDevice& peripheral);
        void clearPeripheral();
        void resetConnection(bool disconnectPeer,
                             AcaiaDisconnectReason reason);
        void rejectPacket(const char* reason);

        // Debug functions.
        void exploreService(BLEService service);
        void exploreCharacteristic(BLECharacteristic characteristic);
        void exploreDescriptor(BLEDescriptor descriptor);
        void printData(const unsigned char data[], int length);

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
        bool                _loggedVersion;
        scale_type          _type;
        bool                _debug;
        char                _scanMac[ACAIA_MAC_CAPACITY];
        char                _address[ACAIA_MAC_CAPACITY];
        char                _localName[ACAIA_NAME_CAPACITY];
        char                _seenMac[ACAIA_MAC_CAPACITY];
        char                _seenName[ACAIA_NAME_CAPACITY];
        bool                _seenPending;
        AcaiaDisconnectReason _lastDisconnectReason;
};

#endif
