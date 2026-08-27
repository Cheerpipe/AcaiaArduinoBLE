#pragma once

#include <stdint.h>

#if !defined(SHOT_STOPPER_HOST_TEST)
#if __has_include(<esp_coexist.h>)
#include <esp_coexist.h>
#define SHOT_STOPPER_RF_COEX_HAS_IDF 1
#endif
#endif

namespace shotstopper {

// Single writer for esp_coex_preference_set. BLE and STA associate run on
// different cores; claims are mux-protected. Winner is BLE > WIFI_ASSOCIATE
// > BALANCE so a finishing STA connect cannot drop BT during a live GATT
// link or a closed machine circuit.

enum class RfCoexClaim : uint8_t {
  BLE = 1 << 0,
  WIFI_ASSOCIATE = 1 << 1
};

enum class RfCoexPreference : uint8_t {
  BALANCE = 0,
  WIFI = 1,
  BT = 2
};

inline RfCoexPreference rfCoexWinner(bool bleHeld, bool wifiAssociateHeld) {
  if (bleHeld) {
    return RfCoexPreference::BT;
  }
  if (wifiAssociateHeld) {
    return RfCoexPreference::WIFI;
  }
  return RfCoexPreference::BALANCE;
}

inline portMUX_TYPE rfCoexMux = portMUX_INITIALIZER_UNLOCKED;
inline uint8_t rfCoexClaims = 0;

inline void applyRfCoexClaimsLocked() {
#if defined(SHOT_STOPPER_RF_COEX_HAS_IDF)
  const RfCoexPreference winner = rfCoexWinner(
      (rfCoexClaims & static_cast<uint8_t>(RfCoexClaim::BLE)) != 0,
      (rfCoexClaims & static_cast<uint8_t>(RfCoexClaim::WIFI_ASSOCIATE)) != 0);
  switch (winner) {
    case RfCoexPreference::BT:
      (void)esp_coex_preference_set(ESP_COEX_PREFER_BT);
      break;
    case RfCoexPreference::WIFI:
      (void)esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
      break;
    case RfCoexPreference::BALANCE:
      (void)esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
      break;
  }
#else
  (void)rfCoexClaims;
#endif
}

inline void setRfCoexClaim(RfCoexClaim claim, bool held) {
  portENTER_CRITICAL(&rfCoexMux);
  if (held) {
    rfCoexClaims =
        static_cast<uint8_t>(rfCoexClaims | static_cast<uint8_t>(claim));
  } else {
    rfCoexClaims = static_cast<uint8_t>(
        rfCoexClaims & static_cast<uint8_t>(~static_cast<uint8_t>(claim)));
  }
  applyRfCoexClaimsLocked();
  portEXIT_CRITICAL(&rfCoexMux);
}

inline RfCoexPreference currentRfCoexPreference() {
  portENTER_CRITICAL(&rfCoexMux);
  const uint8_t claims = rfCoexClaims;
  portEXIT_CRITICAL(&rfCoexMux);
  return rfCoexWinner(
      (claims & static_cast<uint8_t>(RfCoexClaim::BLE)) != 0,
      (claims & static_cast<uint8_t>(RfCoexClaim::WIFI_ASSOCIATE)) != 0);
}

}  // namespace shotstopper
