#ifndef ScaleBleTypes_h
#define ScaleBleTypes_h

#include <stddef.h>
#include <stdint.h>

static const uint16_t SCALE_BLE_INVALID_CONNECTION_HANDLE = 0xffffU;

enum class ScaleBleAddressType : uint8_t {
    Public = 0,
    Random = 1,
    PublicIdentity = 2,
    RandomIdentity = 3,
    Unknown = 0xff
};

struct ScaleBleAddress {
    uint8_t bytes[6];
    ScaleBleAddressType type;
};

struct ScaleBleConnectionRef {
    uint16_t handle;
    uint32_t generation;

    bool valid() const {
        return handle != SCALE_BLE_INVALID_CONNECTION_HANDLE;
    }
};

enum class ScaleBleEventType : uint8_t {
    StartRequested,
    ScanStarted,
    CompatibleAdvertisement,
    ConnectIssued,
    Connected,
    DiscoveryComplete,
    SubscriptionComplete,
    InitializationComplete,
    Disconnected,
    OperationFailed,
    DeadlineExpired,
    BackoffExpired,
    StopRequested
};

// Backend-neutral event DTO. It owns no buffers and never allocates. Raw status
// preserves the NimBLE/HCI value for diagnosis instead of collapsing errors.
struct ScaleBleEvent {
    ScaleBleEventType type;
    uint32_t generation;
    uint32_t operationId;
    uint16_t connectionHandle;
    int32_t rawStatus;
};

enum ScaleBleTimingFlag : uint8_t {
    ScaleBleTimingScanStarted = 1U << 0,
    ScaleBleTimingFirstCompatibleAdvertisement = 1U << 1,
    ScaleBleTimingConnectIssued = 1U << 2,
    ScaleBleTimingReady = 1U << 3
};

// Millisecond timestamps use unsigned subtraction and therefore remain valid
// across millis() wraparound. recordedFlags distinguishes a real zero timestamp
// from a milestone that has not happened.
struct ScaleBleTimingSnapshot {
    uint32_t scanStartedMs;
    uint32_t firstCompatibleAdvertisementMs;
    uint32_t connectIssuedMs;
    uint32_t readyMs;
    uint8_t recordedFlags;

    bool has(ScaleBleTimingFlag flag) const {
        return (recordedFlags & static_cast<uint8_t>(flag)) != 0;
    }
};

// Backend-neutral, fixed-size diagnostics. ArduinoBLE returns a zeroed
// snapshot; the native NimBLE adapter fills every counter without exposing
// host types to the firmware.
struct ScaleBleBackendHealth {
    uint32_t generation;
    uint32_t operationId;
    uint32_t stateAgeMs;
    uint32_t advertisementsSeen;
    uint32_t compatibleAdvertisements;
    uint32_t discardedAdvertisements;
    uint32_t malformedAdvertisements;
    uint32_t negativeCacheHits;
    uint32_t negativeCacheInsertions;
    uint32_t scanStarts;
    uint32_t scanCancels;
    uint32_t scanRestarts;
    uint32_t connectAttempts;
    uint32_t connectionFailures;
    uint32_t discoveryFailures;
    uint32_t subscriptionFailures;
    uint32_t writeFailures;
    uint32_t staleCallbacks;
    uint32_t criticalEventDrops;
    uint32_t controlEventDrops;
    uint32_t rxDrops;
    uint32_t mbufFailures;
    uint32_t cleanupCount;
    uint32_t duplicateCleanups;
    uint32_t backoffCount;
    uint32_t lastAdvertisementToConnectMs;
    uint32_t lastAdvertisementToReadyMs;
    uint16_t criticalEventHighWater;
    uint16_t controlEventHighWater;
    uint16_t rxHighWater;
    uint8_t negativeCacheEntries;
    uint8_t state;
    uint8_t backoffFailures;
};

#endif
