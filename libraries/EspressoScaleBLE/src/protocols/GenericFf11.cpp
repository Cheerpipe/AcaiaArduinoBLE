#include "ScaleProtocol.h"

namespace {

static const byte TARE_GENERIC[6] =
    {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};
static const byte START_TIMER_GENERIC[6] =
    {0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a};
static const byte STOP_TIMER_GENERIC[6] =
    {0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d};
static const byte RESET_TIMER_GENERIC[6] =
    {0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c};
static const byte TARE_START_TIMER_BOOKOO[6] =
    {0x03, 0x0a, 0x07, 0x00, 0x00, 0x00};

static const byte GENERIC_PRODUCT = 0x03;
static const byte GENERIC_TYPE = 0x0a;
static const byte GENERIC_BEEP_LEVEL_CMD = 0x02;

static const char *const kGenericPrefixes[] = {"BOOKO"};

static const ScaleFeatureSet kGenericFeatures = {
    SCALE_CORE_FEATURES | ScaleFeatureCombinedTareStart |
        ScaleFeatureIndependentBeep | ScaleFeatureVolume |
        ScaleFeatureCommandAudibleFeedback,
    0,
    5,
    0,
    8000
};

void fillGenericCommand(byte out[6], byte data1, byte data2, byte data3) {
    out[0] = GENERIC_PRODUCT;
    out[1] = GENERIC_TYPE;
    out[2] = data1;
    out[3] = data2;
    out[4] = data3;
    out[5] = static_cast<byte>(out[0] ^ out[1] ^ out[2] ^ out[3] ^ out[4]);
}

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

bool genericSupportedPacketLength(int length) {
    return length == 20;
}

bool parseGenericWeight(const byte *data, int length, float *weight) {
    if (length != 20 || data[0] != 0x03 ||
        (data[6] != '-' && data[6] != '+' && data[6] != ' ' &&
         data[6] != 0x00)) {
        return false;
    }

    const uint32_t raw = (static_cast<uint32_t>(data[7]) << 16) |
                         (static_cast<uint32_t>(data[8]) << 8) |
                         data[9];
    *weight = static_cast<float>(raw) / 100.0f;
    if (data[6] == '-') {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool parseGenericTimer(const byte *data, int length, uint32_t *timerMs) {
    float ignoredWeight = 0.0f;
    if (!parseGenericWeight(data, length, &ignoredWeight)) {
        return false;
    }
    *timerMs = (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 8) | data[4];
    return true;
}

bool encodeGenericCommand(ScaleOp op, uint8_t arg, byte *out, int *length) {
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_GENERIC, sizeof(TARE_GENERIC), out, length);
        case ScaleOp::StartTimer:
            return copyPayload(START_TIMER_GENERIC, sizeof(START_TIMER_GENERIC),
                               out, length);
        case ScaleOp::StopTimer:
            return copyPayload(STOP_TIMER_GENERIC, sizeof(STOP_TIMER_GENERIC),
                               out, length);
        case ScaleOp::ResetTimer:
            return copyPayload(RESET_TIMER_GENERIC, sizeof(RESET_TIMER_GENERIC),
                               out, length);
        case ScaleOp::CombinedTareStart:
            return copyPayload(TARE_START_TIMER_BOOKOO,
                               sizeof(TARE_START_TIMER_BOOKOO), out, length);
        case ScaleOp::SetVolume: {
            byte command[6];
            fillGenericCommand(command, GENERIC_BEEP_LEVEL_CMD, 0x00, arg);
            return copyPayload(command, sizeof(command), out, length);
        }
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolGenericFf11 = {
    "bookoo_generic",
    "Generic scale detected",
    "ff11",
    "ff12",
    kGenericPrefixes,
    sizeof(kGenericPrefixes) / sizeof(kGenericPrefixes[0]),
    kGenericFeatures,
    &genericSupportedPacketLength,
    &parseGenericWeight,
    &parseGenericTimer,
    &encodeGenericCommand,
    0,
    0
};
