#include "ScaleProtocol.h"

namespace {

static const uint8_t TARE_ECLAIR[3] = {0x54, 0x01, 0x01};
static const uint8_t START_TIMER_ECLAIR[3] = {0x53, 0x01, 0x01};
static const uint8_t STOP_TIMER_ECLAIR[3] = {0x45, 0x01, 0x01};
static const uint8_t RESET_TIMER_ECLAIR[3] = {0x52, 0x01, 0x01};

static const char *const kEclairPrefixes[] = {"ECLAI"};

static const ScaleFeatureSet kEclairFeatures = {
    SCALE_CORE_FEATURES,
    0,
    0,
    0,
    5000
};

bool copyPayload(const uint8_t *command, int commandLength, uint8_t *out,
                 int *length) {
    if (out == nullptr || length == nullptr || commandLength <= 0 ||
        commandLength > SCALE_MAX_COMMAND_LENGTH) {
        return false;
    }
    for (int i = 0; i < commandLength; ++i) {
        out[i] = command[i];
    }
    *length = commandLength;
    return true;
}

bool eclairSupportedPacketLength(int length) {
    return length == 10;
}

bool parseEclairWeight(const uint8_t *data, int length, float *weight) {
    if (length != 10 || data[0] != 'W' ||
        scaleXorBytes(data + 1, 8) != data[9]) {
        return false;
    }
    const int32_t milligrams = static_cast<int32_t>(
        scaleReadUint32LittleEndian(data + 1));
    *weight = static_cast<float>(milligrams) / 1000.0f;
    return scaleValidWeight(*weight);
}

bool parseEclairTimer(const uint8_t *data, int length, uint32_t *timerMs) {
    float ignoredWeight = 0.0f;
    if (!parseEclairWeight(data, length, &ignoredWeight)) {
        return false;
    }
    *timerMs = scaleReadUint32LittleEndian(data + 5);
    return true;
}

bool encodeEclairCommand(ScaleOp op, uint8_t arg, uint8_t *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_ECLAIR, sizeof(TARE_ECLAIR), out, length);
        case ScaleOp::StartTimer:
            return copyPayload(START_TIMER_ECLAIR, sizeof(START_TIMER_ECLAIR),
                               out, length);
        case ScaleOp::StopTimer:
            return copyPayload(STOP_TIMER_ECLAIR, sizeof(STOP_TIMER_ECLAIR),
                               out, length);
        case ScaleOp::ResetTimer:
            return copyPayload(RESET_TIMER_ECLAIR, sizeof(RESET_TIMER_ECLAIR),
                               out, length);
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolEclair = {
    "atomheart_eclair",
    "AtomHeart Eclair detected",
    "AD736C5F-BBC9-1F96-D304-CB5D5F41E160",
    "4F9A45BA-8E1B-4E07-E157-0814D393B968",
    kEclairPrefixes,
    sizeof(kEclairPrefixes) / sizeof(kEclairPrefixes[0]),
    kEclairFeatures,
    &eclairSupportedPacketLength,
    &parseEclairWeight,
    &parseEclairTimer,
    &encodeEclairCommand,
    nullptr,
    0,
    false
};
