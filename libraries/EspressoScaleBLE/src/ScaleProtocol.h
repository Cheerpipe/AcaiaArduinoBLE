#ifndef ScaleProtocol_h
#define ScaleProtocol_h

#include "ScaleFeatures.h"
#include <stddef.h>
#include <stdint.h>

#define SCALE_MAX_PACKET_LENGTH 20
#define SCALE_MAX_COMMAND_LENGTH 20
#define SCALE_MAX_WEIGHT_GRAMS 10000.0f
#define SCALE_CORE_FEATURES                                                    \
    (ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureStartTimer |          \
     ScaleFeatureStopTimer | ScaleFeatureResetTimer)

struct ScalePayload {
    const uint8_t *data;
    int length;
};

struct ScaleProtocol {
    const char *id;
    const char *debugLabel;
    const char *readUuid;
    const char *writeUuid;
    const char *const *namePrefixes;
    size_t namePrefixCount;
    ScaleFeatureSet features;
    bool (*supportedPacketLength)(int length);
    bool (*parseWeight)(const uint8_t *data, int length, float *weight);
    bool (*parseTimer)(const uint8_t *data, int length, uint32_t *timerMs);
    bool (*encodeCommand)(ScaleOp op, uint8_t arg, uint8_t *out, int *length);
    const ScalePayload *initWrites;
    size_t initWriteCount;
    bool requireAdvertisedName;
};

const ScaleProtocol *scaleProtocolAt(size_t index);
size_t scaleProtocolCount();
bool scaleNameIsCompatible(const char *name);
bool scaleNameMatchesProtocol(const char *name, const ScaleProtocol *protocol);
bool scaleParseUuid16(const char *uuid, uint16_t *out);
bool scaleUuid16AllowsNamelessConnect(uint16_t uuid);

bool scaleValidWeight(float weight);
uint8_t scaleXorBytes(const uint8_t *data, int length);
uint32_t scaleReadUint32LittleEndian(const uint8_t *data);
float scaleDecimalDivisor(uint8_t exponent);
bool scaleLooksLikeAcaiaTimer(uint8_t minutes, uint8_t seconds, uint8_t tenths);
uint32_t scaleAcaiaTimerToMs(uint8_t minutes, uint8_t seconds, uint8_t tenths);
bool scaleValidAcaiaChecksum(const uint8_t *data, int length);
bool scaleFelicitaAsciiTimer(const uint8_t *data, uint32_t *timerMs);

extern const ScaleProtocol kScaleProtocolAcaiaLegacy;
extern const ScaleProtocol kScaleProtocolAcaia;
extern const ScaleProtocol kScaleProtocolGenericFf11;
extern const ScaleProtocol kScaleProtocolFelicita;
extern const ScaleProtocol kScaleProtocolEclair;
extern const ScaleProtocol kScaleProtocolDecent;
extern const ScaleProtocol kScaleProtocolDifluid;
extern const ScaleProtocol kScaleProtocolMyscale;
extern const ScaleProtocol kScaleProtocolWeighMyBru;
extern const ScaleProtocol kScaleProtocolVaria;
extern const ScaleProtocol kScaleProtocolEureka;

#endif
