#pragma once

#include <stdint.h>

#if !defined(SHOT_STOPPER_HOST_TEST)
#if __has_include(<esp_coexist.h>)
#include <esp_coexist.h>
#define SHOT_STOPPER_RF_COEX_HAS_IDF 1
#endif
#endif

namespace shotstopper {

// Always PREFER_BT. Scale scan, GATT, and brew share one 2.4 GHz radio with
// STA; a flat BT preference keeps discovery and the weight stream from losing
// airtime to beacons. STA associate may take longer. There is no IDF getter
// for the live preference.

enum class RfCoexPreference : uint8_t {
  BALANCE = 0,
  WIFI = 1,
  BT = 2
};

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

inline RfCoexPreference snapshotRfCoexPreference() {
  return RfCoexPreference::BT;
}

inline void applyRfCoexPreference(RfCoexPreference preference) {
#if defined(SHOT_STOPPER_RF_COEX_HAS_IDF)
  switch (preference) {
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
  (void)preference;
#endif
}

inline void ensureRfCoexBt() {
  applyRfCoexPreference(RfCoexPreference::BT);
}

}  // namespace shotstopper
