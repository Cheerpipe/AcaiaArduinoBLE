#ifndef ScaleBleBackend_h
#define ScaleBleBackend_h

#if defined(ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE) && \
    defined(ESPRESSO_SCALE_BLE_BACKEND_NIMBLE)
#error "Exactly one EspressoScaleBLE backend may be compiled"
#endif

// Arduino builds do not consume IDF Kconfig. Preserve the historical backend
// unless the build explicitly opts into another one.
#if !defined(ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE) && \
    !defined(ESPRESSO_SCALE_BLE_BACKEND_NIMBLE)
#define ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE 1
#endif

enum class ScaleBleBackend {
    ArduinoBle,
    NimBle
};

#if defined(ESPRESSO_SCALE_BLE_BACKEND_NIMBLE)
static constexpr ScaleBleBackend kScaleBleBuildBackend = ScaleBleBackend::NimBle;
#else
static constexpr ScaleBleBackend kScaleBleBuildBackend =
    ScaleBleBackend::ArduinoBle;
#endif

#endif
