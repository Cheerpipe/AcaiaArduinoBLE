#include "ScaleProtocol.h"

namespace {

static const byte TARE_EUREKA[6] =
    {0xaa, 0x02, 0x31, 0x31, 0x00, 0x00};
static const byte START_TIMER_EUREKA[6] =
    {0xaa, 0x02, 0x33, 0x33, 0x00, 0x00};
static const byte STOP_TIMER_EUREKA[6] =
    {0xaa, 0x02, 0x34, 0x34, 0x00, 0x00};
static const byte RESET_TIMER_EUREKA[6] =
    {0xaa, 0x02, 0x35, 0x35, 0x00, 0x00};

static const char *const kEurekaPrefixes[] = {"CFS-9002", "LSJ-001"};

static const ScaleFeatureSet kEurekaFeatures = {
    ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureStartTimer |
        ScaleFeatureStopTimer | ScaleFeatureResetTimer,
    0,
    0,
    0,
    5000
};

bool copyPayload(const byte *command, int commandLength, byte *out,
                 int *length) {
    if (out == 0 || length == 0 || commandLength <= 0 ||
        commandLength > SCALE_MAX_COMMAND_LENGTH) {
        return false;
    }
    for (int i = 0; i < commandLength; ++i) {
        out[i] = command[i];
    }
    *length = commandLength;
    return true;
}

bool eurekaSupportedPacketLength(int length) {
    return length == 11;
}

bool parseEurekaWeight(const byte *data, int length, float *weight) {
    if (length != 11) {
        return false;
    }
    uint16_t raw = static_cast<uint16_t>(
        (static_cast<uint16_t>(data[8]) << 8) | data[7]);
    *weight = static_cast<float>(raw) * 0.1f;
    if (data[6] != 0) {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool encodeEurekaCommand(ScaleOp op, uint8_t arg, byte *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_EUREKA, sizeof(TARE_EUREKA), out, length);
        case ScaleOp::StartTimer:
            return copyPayload(START_TIMER_EUREKA, sizeof(START_TIMER_EUREKA),
                               out, length);
        case ScaleOp::StopTimer:
            return copyPayload(STOP_TIMER_EUREKA, sizeof(STOP_TIMER_EUREKA),
                               out, length);
        case ScaleOp::ResetTimer:
            return copyPayload(RESET_TIMER_EUREKA, sizeof(RESET_TIMER_EUREKA),
                               out, length);
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolEureka = {
    "eureka",
    "Eureka Precisa detected",
    "fff1",
    "fff2",
    kEurekaPrefixes,
    sizeof(kEurekaPrefixes) / sizeof(kEurekaPrefixes[0]),
    kEurekaFeatures,
    &eurekaSupportedPacketLength,
    &parseEurekaWeight,
    0,
    &encodeEurekaCommand,
    0,
    0,
    true
};
