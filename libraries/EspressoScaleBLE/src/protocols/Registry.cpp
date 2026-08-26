#include "ScaleProtocol.h"

#include <string.h>

static const ScaleProtocol *const kProtocols[] = {
    &kScaleProtocolAcaiaLegacy,
    &kScaleProtocolAcaia,
    &kScaleProtocolGenericFf11,
    &kScaleProtocolFelicita,
    &kScaleProtocolEclair,
};

const ScaleProtocol *scaleProtocolAt(size_t index) {
    if (index >= scaleProtocolCount()) {
        return 0;
    }
    return kProtocols[index];
}

size_t scaleProtocolCount() {
    return sizeof(kProtocols) / sizeof(kProtocols[0]);
}

bool scaleNameIsCompatible(const char *name) {
    if (name == 0) {
        return false;
    }
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        const ScaleProtocol *protocol = kProtocols[i];
        for (size_t p = 0; p < protocol->namePrefixCount; ++p) {
            const char *prefix = protocol->namePrefixes[p];
            if (strncmp(name, prefix, 5) == 0) {
                return true;
            }
        }
    }
    return false;
}
