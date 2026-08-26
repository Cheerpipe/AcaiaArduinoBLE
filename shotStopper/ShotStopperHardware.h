#pragma once

#include <stdint.h>

#include "ShotStopperDomain.h"

namespace shotstopper {

// Board hardware — ESP32-S3 only (N8R4 QSPI PSRAM or N16R8 OPI PSRAM).
#if !defined(SHOT_STOPPER_HOST_TEST)
#if !defined(ARDUINO_ESP32S3_DEV)
#error "Unsupported board: Shot Stopper requires ESP32-S3 (esp32:esp32:esp32s3)"
#endif
#if !defined(BOARD_HAS_PSRAM)
#error "Shot Stopper requires PSRAM. Compile n8r4 (QSPI 4MB) or n16r8 (OPI 8MB)"
#endif
#endif

#if defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t ACTIVATOR_GPIO = 21;
constexpr uint8_t RELAY_GPIO = 2;
#ifndef SHOT_STOPPER_SCALE_CONNECTED_LED_GPIO
#define SHOT_STOPPER_SCALE_CONNECTED_LED_GPIO 1
#endif
#ifndef SHOT_STOPPER_BUZZER_GPIO
#define SHOT_STOPPER_BUZZER_GPIO 14
#endif
#else
#error "Unsupported board: Shot Stopper requires ESP32-S3"
#endif

constexpr uint8_t SCALE_CONNECTED_LED_GPIO =
    SHOT_STOPPER_SCALE_CONNECTED_LED_GPIO;
constexpr uint8_t BUZZER_GPIO = SHOT_STOPPER_BUZZER_GPIO;

constexpr uint8_t ACTIVATOR_ACTIVE_LEVEL = LOW;
// Active-HIGH relay: GPIO HIGH energizes the coil and closes NO.
constexpr uint8_t RELAY_CLOSED_LEVEL = HIGH;
constexpr uint8_t RELAY_OPEN_LEVEL = LOW;

#if SHOT_STOPPER_MACHINE_TYPE == 2
#ifndef SHOT_STOPPER_REED_GPIO
#define SHOT_STOPPER_REED_GPIO 13
#endif
constexpr uint8_t REED_GPIO = SHOT_STOPPER_REED_GPIO;
constexpr uint8_t REED_ACTIVE_LEVEL = LOW;
constexpr uint32_t REED_DEBOUNCE_MS = 30;
#endif

constexpr uint32_t ACTIVATOR_DEBOUNCE_MS = 30;
constexpr uint32_t SAFETY_HEARTBEAT_TOGGLE_MS = 50;
constexpr uint32_t CIRCUIT_FEEDBACK_SETTLE_MS = 100;

#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO) != \
    defined(SHOT_STOPPER_CIRCUIT_FEEDBACK_GPIO)
#error "Define both safety heartbeat and circuit feedback GPIOs, or neither"
#endif

#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO)
constexpr bool EXTERNAL_SAFETY_HARDWARE_PRESENT = true;
constexpr uint8_t SAFETY_HEARTBEAT_GPIO =
    SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO;
constexpr uint8_t CIRCUIT_FEEDBACK_GPIO = SHOT_STOPPER_CIRCUIT_FEEDBACK_GPIO;
#ifndef SHOT_STOPPER_CIRCUIT_FEEDBACK_CLOSED_LEVEL
#define SHOT_STOPPER_CIRCUIT_FEEDBACK_CLOSED_LEVEL LOW
#endif
constexpr uint8_t CIRCUIT_FEEDBACK_CLOSED_LEVEL =
    SHOT_STOPPER_CIRCUIT_FEEDBACK_CLOSED_LEVEL;
#else
constexpr bool EXTERNAL_SAFETY_HARDWARE_PRESENT = false;
constexpr uint8_t SAFETY_HEARTBEAT_GPIO = 0;
constexpr uint8_t CIRCUIT_FEEDBACK_GPIO = 0;
constexpr uint8_t CIRCUIT_FEEDBACK_CLOSED_LEVEL = LOW;
#endif

static_assert(ACTIVATOR_GPIO != RELAY_GPIO,
              "Activator and relay must use different GPIOs");
static_assert(ACTIVATOR_ACTIVE_LEVEL == LOW,
              "Activator wiring requires INPUT_PULLUP and active LOW");
static_assert(RELAY_CLOSED_LEVEL != RELAY_OPEN_LEVEL,
              "Relay open and closed levels must differ");
static_assert((RELAY_OPEN_LEVEL == LOW || RELAY_OPEN_LEVEL == HIGH) &&
                  (RELAY_CLOSED_LEVEL == LOW || RELAY_CLOSED_LEVEL == HIGH),
              "Relay levels must be LOW or HIGH");
static_assert(SCALE_CONNECTED_LED_GPIO != ACTIVATOR_GPIO &&
                  SCALE_CONNECTED_LED_GPIO != RELAY_GPIO,
              "Scale-connected LED GPIO must not share activator or relay GPIOs");
static_assert(BUZZER_GPIO != ACTIVATOR_GPIO && BUZZER_GPIO != RELAY_GPIO &&
                  BUZZER_GPIO != SCALE_CONNECTED_LED_GPIO,
              "Buzzer GPIO must be distinct from activator, relay, and LED GPIOs");
#ifndef SHOT_STOPPER_HOST_TEST
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(SCALE_CONNECTED_LED_GPIO),
              "Scale-connected LED must use a valid output-capable GPIO");
static_assert(!BUZZER_SUPPORT_ENABLED ||
                  GPIO_IS_VALID_OUTPUT_GPIO(BUZZER_GPIO),
              "Buzzer must use a valid output-capable GPIO");
#endif
#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO)
static_assert(SAFETY_HEARTBEAT_GPIO != RELAY_GPIO &&
                  SAFETY_HEARTBEAT_GPIO != ACTIVATOR_GPIO &&
                  CIRCUIT_FEEDBACK_GPIO != RELAY_GPIO &&
                  CIRCUIT_FEEDBACK_GPIO != ACTIVATOR_GPIO &&
                  CIRCUIT_FEEDBACK_GPIO != SAFETY_HEARTBEAT_GPIO,
              "Safety GPIOs must be unique");
static_assert(SAFETY_HEARTBEAT_GPIO != SCALE_CONNECTED_LED_GPIO &&
                  CIRCUIT_FEEDBACK_GPIO != SCALE_CONNECTED_LED_GPIO,
              "Safety GPIOs must not share the scale-connected LED pin");
static_assert(BUZZER_GPIO != SAFETY_HEARTBEAT_GPIO &&
                  BUZZER_GPIO != CIRCUIT_FEEDBACK_GPIO,
              "Buzzer GPIO must be distinct from safety GPIOs");
#ifndef SHOT_STOPPER_HOST_TEST
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(SAFETY_HEARTBEAT_GPIO),
              "Heartbeat must use a valid output-capable GPIO");
static_assert(GPIO_IS_VALID_GPIO(CIRCUIT_FEEDBACK_GPIO),
              "circuit feedback must use a valid input GPIO");
#endif
static_assert(CIRCUIT_FEEDBACK_CLOSED_LEVEL == LOW ||
                  CIRCUIT_FEEDBACK_CLOSED_LEVEL == HIGH,
              "circuit feedback level must be LOW or HIGH");
#endif
static_assert(ACTIVATOR_DEBOUNCE_MS > 0,
              "Activator debounce must be greater than zero");
static_assert(ACTIVATOR_DEBOUNCE_MS < 100,
              "Activator debounce must fit every valid rinse gesture");
#if SHOT_STOPPER_MACHINE_TYPE == 2
static_assert(REED_GPIO != ACTIVATOR_GPIO && REED_GPIO != RELAY_GPIO &&
                  REED_GPIO != SCALE_CONNECTED_LED_GPIO &&
                  REED_GPIO != BUZZER_GPIO,
              "Reed GPIO must be distinct from activator, relay, LED, and buzzer");
#if defined(SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO)
static_assert(REED_GPIO != SAFETY_HEARTBEAT_GPIO &&
                  REED_GPIO != CIRCUIT_FEEDBACK_GPIO,
              "Reed GPIO must be distinct from safety GPIOs");
#endif
#ifndef SHOT_STOPPER_HOST_TEST
static_assert(GPIO_IS_VALID_GPIO(REED_GPIO),
              "Reed must use a valid input GPIO");
#endif
static_assert(REED_ACTIVE_LEVEL == LOW,
              "Reed wiring requires INPUT_PULLUP and active LOW");
static_assert(REED_DEBOUNCE_MS > 0 && REED_DEBOUNCE_MS < 100,
              "Reed debounce must match the activator window");
#endif

}  // namespace shotstopper
