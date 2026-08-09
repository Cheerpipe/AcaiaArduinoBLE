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

#define LIBRARY_VERSION                 "3.4.0"
#define WRITE_CHAR_OLD_VERSION          "2a80"
#define READ_CHAR_OLD_VERSION           "2a80"
#define WRITE_CHAR_NEW_VERSION          "49535343-8841-43f4-a8d4-ecbe34729bb3"
#define READ_CHAR_NEW_VERSION           "49535343-1e4d-4bd9-ba61-23c647249616"
#define WRITE_CHAR_GENERIC              "ff12"
#define READ_CHAR_GENERIC               "ff11"
#define WRITE_CHAR_FELICITA             "ffe1"
#define READ_CHAR_FELICITA              "ffe1"
#define HEARTBEAT_PERIOD_MS              2750UL
#define FIRST_PACKET_TIMEOUT_MS          5000UL
#define MAX_PACKET_PERIOD_MS             5000UL
#define SCALE_SCAN_TIMEOUT_MS            3000UL
#define MAX_SUPPORTED_WEIGHT_GRAMS      10000.0f

#include "Arduino.h"
#include <ArduinoBLE.h>

enum scale_type {
    OLD,     // Lunar (pre-2021)
    NEW,     // Lunar (2021), Pyxis
    GENERIC, // Bookoo Themis, Decent, etc. (ff11/ff12)
    FELICITA // Felicita Arc (ffe1)
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

        bool init(String mac = "");
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
        bool beepWithoutStateChange();

        bool heartbeat();
        float getWeight() const;
        bool heartbeatRequired() const;
        bool isConnected();
        bool newWeightAvailable();

        AcaiaDisconnectReason lastDisconnectReason() const;
        const char* lastDisconnectReasonName() const;
        uint32_t lastValidPacketAgeMs() const;
        uint32_t rejectedPacketCount() const;
        uint32_t reconnectCount() const;

    private:
        bool isScaleName(const String& name) const;
        bool configureCharacteristics(BLEDevice& peripheral,
                                      scale_type type,
                                      const char* writeUuid,
                                      const char* readUuid);
        bool parseWeightPacket(const byte data[], int length,
                               float& weight) const;
        bool parseAcaiaNewPacket(const byte data[], int length,
                                 float& weight) const;
        bool parseAcaiaOldPacket(const byte data[], int length,
                                 float& weight) const;
        bool parseGenericPacket(const byte data[], int length,
                                float& weight) const;
        bool parseFelicitaPacket(const byte data[], int length,
                                 float& weight) const;
        bool validAcaiaChecksum(const byte data[], int length) const;
        bool validWeight(float weight) const;
        bool supportedPacketLength(int length) const;
        bool writeCommand(const byte command[], int length);

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
        BLECharacteristic   _write;
        BLECharacteristic   _read;
        BLEDevice           _peripheral;
        uint32_t            _lastHeartBeat;
        uint32_t            _connectedAt;
        uint32_t            _lastPacket;
        uint32_t            _packetPeriod;
        uint32_t            _rejectedPackets;
        uint32_t            _reconnects;
        uint32_t            _successfulConnections;
        bool                _hasPeripheral;
        bool                _hasValidPacket;
        bool                _scanning;
        bool                _connected;
        scale_type          _type;
        bool                _debug;
        AcaiaDisconnectReason _lastDisconnectReason;
};

#endif
