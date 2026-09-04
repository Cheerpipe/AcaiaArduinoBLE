#include "ScaleProtocol.h"

namespace {

static const uint8_t TARE_FELICITA[1] = {0x54};
static const uint8_t START_TIMER_FELICITA[1] = {0x52};
static const uint8_t STOP_TIMER_FELICITA[1] = {0x53};
static const uint8_t RESET_TIMER_FELICITA[1] = {0x43};
static const uint8_t WEIGHT_TIMER_MODE_FELICITA[1] = {0x32};

static const char *const kFelicitaPrefixes[] = {"FELIC"};

static const ScalePayload kFelicitaInitWrites[] = {
    {WEIGHT_TIMER_MODE_FELICITA,
     static_cast<int>(sizeof(WEIGHT_TIMER_MODE_FELICITA))}
};

static const ScaleFeatureSet kFelicitaFeatures = {
    SCALE_CORE_FEATURES | ScaleFeatureCommandAudibleFeedback,
    0,
    0,
    0,
    5000
};

bool copyPayload(const uint8_t *command, int commandLength, uint8_t *out,
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

bool felicitaSupportedPacketLength(int length) {
    return length == 18;
}

bool parseFelicitaWeight(const uint8_t *data, int length, float *weight) {
    if (length != 18 ||
        (data[2] != '-' && data[2] != '+' && data[2] != ' ' &&
         data[2] != 0x00)) {
        return false;
    }
    for (int i = 3; i <= 8; ++i) {
        if (data[i] < '0' || data[i] > '9') {
            return false;
        }
    }

    const uint32_t hundredths =
        static_cast<uint32_t>(data[3] - '0') * 100000UL +
        static_cast<uint32_t>(data[4] - '0') * 10000UL +
        static_cast<uint32_t>(data[5] - '0') * 1000UL +
        static_cast<uint32_t>(data[6] - '0') * 100UL +
        static_cast<uint32_t>(data[7] - '0') * 10UL +
        static_cast<uint32_t>(data[8] - '0');
    *weight = static_cast<float>(hundredths) / 100.0f;
    if (data[2] == '-') {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool parseFelicitaTimer(const uint8_t *data, int length, uint32_t *timerMs) {
    float ignoredWeight = 0.0f;
    if (!parseFelicitaWeight(data, length, &ignoredWeight)) {
        return false;
    }
    if (scaleFelicitaAsciiTimer(data, timerMs)) {
        return true;
    }
    if (scaleLooksLikeAcaiaTimer(data[9], data[10], data[11])) {
        *timerMs = scaleAcaiaTimerToMs(data[9], data[10], data[11]);
        return true;
    }
    return false;
}

bool encodeFelicitaCommand(ScaleOp op, uint8_t arg, uint8_t *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_FELICITA, sizeof(TARE_FELICITA), out,
                               length);
        case ScaleOp::StartTimer:
            return copyPayload(START_TIMER_FELICITA, sizeof(START_TIMER_FELICITA),
                               out, length);
        case ScaleOp::StopTimer:
            return copyPayload(STOP_TIMER_FELICITA, sizeof(STOP_TIMER_FELICITA),
                               out, length);
        case ScaleOp::ResetTimer:
            return copyPayload(RESET_TIMER_FELICITA, sizeof(RESET_TIMER_FELICITA),
                               out, length);
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolFelicita = {
    "felicita",
    "Felicita Arc detected",
    "ffe1",
    "ffe1",
    kFelicitaPrefixes,
    sizeof(kFelicitaPrefixes) / sizeof(kFelicitaPrefixes[0]),
    kFelicitaFeatures,
    &felicitaSupportedPacketLength,
    &parseFelicitaWeight,
    &parseFelicitaTimer,
    &encodeFelicitaCommand,
    kFelicitaInitWrites,
    sizeof(kFelicitaInitWrites) / sizeof(kFelicitaInitWrites[0]),
    false
};
