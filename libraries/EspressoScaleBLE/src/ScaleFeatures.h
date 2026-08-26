#ifndef ScaleFeatures_h
#define ScaleFeatures_h

#include <stdint.h>

enum ScaleFeature {
    ScaleFeatureWeight = 1u << 0,
    ScaleFeatureTare = 1u << 1,
    ScaleFeatureStartTimer = 1u << 2,
    ScaleFeatureStopTimer = 1u << 3,
    ScaleFeatureResetTimer = 1u << 4,
    ScaleFeatureCombinedTareStart = 1u << 5,
    ScaleFeatureIndependentBeep = 1u << 6,
    ScaleFeatureVolume = 1u << 7,
    ScaleFeatureCommandAudibleFeedback = 1u << 8,
    ScaleFeatureHeartbeat = 1u << 9
};

struct ScaleFeatureSet {
    uint32_t flags;
    uint8_t volumeMin;
    uint8_t volumeMax;
    uint16_t heartbeatPeriodMs;
    uint16_t maxPacketSilenceMs;

    bool has(ScaleFeature feature) const {
        return (flags & static_cast<uint32_t>(feature)) != 0;
    }
};

inline ScaleFeatureSet scaleFeatureSetNone() {
    ScaleFeatureSet features;
    features.flags = 0;
    features.volumeMin = 0;
    features.volumeMax = 0;
    features.heartbeatPeriodMs = 0;
    features.maxPacketSilenceMs = 0;
    return features;
}

enum class ScaleCommandResult : uint8_t {
    Ok,
    Unsupported,
    NotConnected,
    InvalidArgument,
    WriteFailed
};

inline bool scaleCommandOk(ScaleCommandResult result) {
    return result == ScaleCommandResult::Ok;
}

enum class ScaleOp : uint8_t {
    Tare,
    StartTimer,
    StopTimer,
    ResetTimer,
    CombinedTareStart,
    Heartbeat,
    SetVolume
};

#endif
