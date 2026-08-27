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
inline RfCoexPreference rfCoexLastApplied = RfCoexPreference::BALANCE;
inline bool rfCoexLastAppliedValid = false;

inline RfCoexPreference rfCoexWinnerFromClaims(uint8_t claims) {
  return rfCoexWinner(
      (claims & static_cast<uint8_t>(RfCoexClaim::BLE)) != 0,
      (claims & static_cast<uint8_t>(RfCoexClaim::WIFI_ASSOCIATE)) != 0);
}

inline const char *rfCoexPreferenceName(RfCoexPreference preference) {
  switch (preference) {
    case RfCoexPreference::BT:
      return "BT";
    case RfCoexPreference::WIFI:
      return "WIFI";
    case RfCoexPreference::BALANCE:
      return "BALANCE";
  }
  return "UNKNOWN";
}

// Last value published to IDF, or the claim winner if nothing has been applied
// yet. There is no IDF getter for the live preference.
inline RfCoexPreference snapshotRfCoexPreference() {
  portENTER_CRITICAL(&rfCoexMux);
  const RfCoexPreference out =
      rfCoexLastAppliedValid ? rfCoexLastApplied
                             : rfCoexWinnerFromClaims(rfCoexClaims);
  portEXIT_CRITICAL(&rfCoexMux);
  return out;
}

inline void applyRfCoexPreference(RfCoexPreference winner) {
#if defined(SHOT_STOPPER_RF_COEX_HAS_IDF)
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
  (void)winner;
#endif
}

// IDF coex must not run inside portENTER_CRITICAL. Publish outside the mux
// and retry if another core changed claims during the call.
inline void publishRfCoexPreference() {
  for (;;) {
    RfCoexPreference current;
    portENTER_CRITICAL(&rfCoexMux);
    current = rfCoexWinnerFromClaims(rfCoexClaims);
    const bool skip =
        rfCoexLastAppliedValid && rfCoexLastApplied == current;
    portEXIT_CRITICAL(&rfCoexMux);
    if (skip) {
      return;
    }
    applyRfCoexPreference(current);
    bool settled = false;
    portENTER_CRITICAL(&rfCoexMux);
    const RfCoexPreference again = rfCoexWinnerFromClaims(rfCoexClaims);
    settled = again == current;
    if (settled) {
      rfCoexLastApplied = current;
      rfCoexLastAppliedValid = true;
    }
    portEXIT_CRITICAL(&rfCoexMux);
    if (settled) {
      return;
    }
  }
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
  portEXIT_CRITICAL(&rfCoexMux);
  publishRfCoexPreference();
}

inline RfCoexPreference currentRfCoexPreference() {
  portENTER_CRITICAL(&rfCoexMux);
  const uint8_t claims = rfCoexClaims;
  portEXIT_CRITICAL(&rfCoexMux);
  return rfCoexWinnerFromClaims(claims);
}

}  // namespace shotstopper
