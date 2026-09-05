#include "ScaleProtocol.h"

namespace {

static const uint8_t TARE_VARIA[5] = {0xfa, 0x82, 0x01, 0x01, 0x82};
static const uint8_t START_TIMER_VARIA[5] = {0xfa, 0x88, 0x01, 0x01, 0x88};
static const uint8_t STOP_TIMER_VARIA[5] = {0xfa, 0x89, 0x01, 0x02, 0x8a};
static const uint8_t RESET_TIMER_VARIA[5] = {0xfa, 0x8a, 0x01, 0x03, 0x88};

static const char *const kVariaPrefixes[] = {
    "AKU MINI SCALE",
    "VARIA AKU",
    "Varia AKU",
    "AKU SCALE"
};

static const ScaleFeatureSet kVariaFeatures = {
    ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureStartTimer |
        ScaleFeatureStopTimer | ScaleFeatureResetTimer,
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

bool variaXorValid(const uint8_t *data, int length) {
    if (length < 3) {
        return false;
    }
    return scaleXorBytes(data + 1, length - 2) == data[length - 1];
}

bool variaSupportedPacketLength(int length) {
    return length == 6 || length == 7;
}

bool parseVariaWeight(const uint8_t *data, int length, float *weight) {
    if (length != 7 || data[0] != 0xfa || data[1] != 0x01) {
        return false;
    }
    if (!variaXorValid(data, length)) {
        return false;
    }
    const int sign = (data[3] & 0x10) == 0 ? 1 : -1;
    const int value = ((data[3] & 0x0f) << 16) | (data[4] << 8) | data[5];
    *weight = static_cast<float>(sign * value) * 0.01f;
    return scaleValidWeight(*weight);
}

bool parseVariaTimer(const uint8_t *data, int length, uint32_t *timerMs) {
    if (length != 6 || data[0] != 0xfa || data[1] != 0x87) {
        return false;
    }
    if (!variaXorValid(data, length)) {
        return false;
    }
    const uint16_t seconds =
        static_cast<uint16_t>((static_cast<uint16_t>(data[3]) << 8) | data[4]);
    *timerMs = static_cast<uint32_t>(seconds) * 1000UL;
    return true;
}

bool encodeVariaCommand(ScaleOp op, uint8_t arg, uint8_t *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_VARIA, sizeof(TARE_VARIA), out, length);
        case ScaleOp::StartTimer:
            return copyPayload(START_TIMER_VARIA, sizeof(START_TIMER_VARIA),
                               out, length);
        case ScaleOp::StopTimer:
            return copyPayload(STOP_TIMER_VARIA, sizeof(STOP_TIMER_VARIA), out,
                               length);
        case ScaleOp::ResetTimer:
            return copyPayload(RESET_TIMER_VARIA, sizeof(RESET_TIMER_VARIA),
                               out, length);
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolVaria = {
    "varia",
    "Varia AKU detected",
    "fff1",
    "fff2",
    kVariaPrefixes,
    sizeof(kVariaPrefixes) / sizeof(kVariaPrefixes[0]),
    kVariaFeatures,
    &variaSupportedPacketLength,
    &parseVariaWeight,
    &parseVariaTimer,
    &encodeVariaCommand,
    nullptr,
    0,
    true
};
