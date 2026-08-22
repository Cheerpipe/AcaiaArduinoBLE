#pragma once

// STA / LKG / device password policy on PersistedSettings. Not NVS I/O.

#include "ShotStopperPersistedSettings.h"

namespace shotstopper {

inline bool constantTimeEqual(const uint8_t *left, const uint8_t *right,
                              size_t length) {
  uint8_t difference = 0;
  for (size_t index = 0; index < length; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

inline bool isFactoryDefaultPassword(const char *password) {
  size_t storedLength = 0;
  size_t defaultLength = 0;
  if (!boundedCString(password, WIFI_PASSWORD_CAPACITY, &storedLength) ||
      !boundedCString(DEFAULT_DEVICE_PASSWORD, WIFI_PASSWORD_CAPACITY,
                      &defaultLength) ||
      storedLength != defaultLength) {
    return false;
  }
  return constantTimeEqual(reinterpret_cast<const uint8_t *>(password),
                           reinterpret_cast<const uint8_t *>(DEFAULT_DEVICE_PASSWORD),
                           storedLength);
}

inline bool passwordIsFactoryDefault(const PersistedSettings &settings) {
  return isFactoryDefaultPassword(settings.devicePassword);
}

inline bool setDevicePassword(PersistedSettings &settings,
                              const char *newPassword) {
  if (!validDevicePassword(newPassword)) {
    return false;
  }
  // Refuse re-selecting the published factory credential.
  if (isFactoryDefaultPassword(newPassword)) {
    return false;
  }
  memset(settings.devicePassword, 0, sizeof(settings.devicePassword));
  copyCString(settings.devicePassword, sizeof(settings.devicePassword),
              newPassword);
  return true;
}

inline bool initializeDefaultDevicePassword(PersistedSettings &settings) {
  if (!validDevicePassword(DEFAULT_DEVICE_PASSWORD)) {
    return false;
  }
  memset(settings.devicePassword, 0, sizeof(settings.devicePassword));
  copyCString(settings.devicePassword, sizeof(settings.devicePassword),
              DEFAULT_DEVICE_PASSWORD);
  return true;
}

inline void clearStaAddressFields(PersistedSettings &settings) {
  settings.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  memset(settings.staIp, 0, sizeof(settings.staIp));
  memset(settings.staNetmask, 0, sizeof(settings.staNetmask));
  memset(settings.staGateway, 0, sizeof(settings.staGateway));
  memset(settings.staDns1, 0, sizeof(settings.staDns1));
  memset(settings.staDns2, 0, sizeof(settings.staDns2));
  settings.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
}

inline void clearLkgNetwork(PersistedSettings &settings) {
  settings.lkgValid = false;
  settings.lkgOpen = false;
  memset(settings.lkgSsid, 0, sizeof(settings.lkgSsid));
  memset(settings.lkgPassword, 0, sizeof(settings.lkgPassword));
  settings.lkgIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  memset(settings.lkgIp, 0, sizeof(settings.lkgIp));
  memset(settings.lkgNetmask, 0, sizeof(settings.lkgNetmask));
  memset(settings.lkgGateway, 0, sizeof(settings.lkgGateway));
  memset(settings.lkgDns1, 0, sizeof(settings.lkgDns1));
  memset(settings.lkgDns2, 0, sizeof(settings.lkgDns2));
}

inline void clearStaNetwork(PersistedSettings &settings) {
  settings.staConfigured = false;
  settings.staOpen = false;
  memset(settings.staSsid, 0, sizeof(settings.staSsid));
  memset(settings.staPassword, 0, sizeof(settings.staPassword));
  clearStaAddressFields(settings);
  clearLkgNetwork(settings);
}

inline void copyActiveStaToLkg(PersistedSettings &settings) {
  if (!settings.staConfigured) {
    clearLkgNetwork(settings);
    return;
  }
  settings.lkgValid = true;
  settings.lkgOpen = settings.staOpen;
  memset(settings.lkgSsid, 0, sizeof(settings.lkgSsid));
  memset(settings.lkgPassword, 0, sizeof(settings.lkgPassword));
  copyCString(settings.lkgSsid, sizeof(settings.lkgSsid), settings.staSsid);
  copyCString(settings.lkgPassword, sizeof(settings.lkgPassword),
              settings.staPassword);
  settings.lkgIpMode = settings.staIpMode;
  memcpy(settings.lkgIp, settings.staIp, sizeof(settings.lkgIp));
  memcpy(settings.lkgNetmask, settings.staNetmask, sizeof(settings.lkgNetmask));
  memcpy(settings.lkgGateway, settings.staGateway, sizeof(settings.lkgGateway));
  memcpy(settings.lkgDns1, settings.staDns1, sizeof(settings.lkgDns1));
  memcpy(settings.lkgDns2, settings.staDns2, sizeof(settings.lkgDns2));
}

inline bool restoreLkgToActive(PersistedSettings &settings) {
  if (!settings.lkgValid || !validWifiSsid(settings.lkgSsid) ||
      !validWifiPassword(settings.lkgPassword, settings.lkgOpen) ||
      !validStaAddressConfig(settings.lkgIpMode, settings.lkgIp,
                             settings.lkgNetmask, settings.lkgGateway,
                             settings.lkgDns1, settings.lkgDns2)) {
    return false;
  }
  settings.staConfigured = true;
  settings.staOpen = settings.lkgOpen;
  memset(settings.staSsid, 0, sizeof(settings.staSsid));
  memset(settings.staPassword, 0, sizeof(settings.staPassword));
  copyCString(settings.staSsid, sizeof(settings.staSsid), settings.lkgSsid);
  copyCString(settings.staPassword, sizeof(settings.staPassword),
              settings.lkgPassword);
  settings.staIpMode = settings.lkgIpMode;
  memcpy(settings.staIp, settings.lkgIp, sizeof(settings.staIp));
  memcpy(settings.staNetmask, settings.lkgNetmask, sizeof(settings.staNetmask));
  memcpy(settings.staGateway, settings.lkgGateway, sizeof(settings.staGateway));
  memcpy(settings.staDns1, settings.lkgDns1, sizeof(settings.staDns1));
  memcpy(settings.staDns2, settings.lkgDns2, sizeof(settings.staDns2));
  settings.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  return true;
}

inline bool validPersistedStaNetwork(const PersistedSettings &settings) {
  if (!settings.staConfigured) {
    return !settings.lkgValid &&
           settings.staConfigState ==
               static_cast<uint8_t>(StaConfigState::CONFIRMED) &&
           validStaAddressConfig(settings.staIpMode, settings.staIp,
                                 settings.staNetmask, settings.staGateway,
                                 settings.staDns1, settings.staDns2);
  }
  if (!validWifiSsid(settings.staSsid) ||
      !validWifiPassword(settings.staPassword, settings.staOpen) ||
      !validStaConfigState(settings.staConfigState) ||
      !validStaAddressConfig(settings.staIpMode, settings.staIp,
                             settings.staNetmask, settings.staGateway,
                             settings.staDns1, settings.staDns2)) {
    return false;
  }
  if (!settings.lkgValid) {
    return true;
  }
  return validWifiSsid(settings.lkgSsid) &&
         validWifiPassword(settings.lkgPassword, settings.lkgOpen) &&
         validStaAddressConfig(settings.lkgIpMode, settings.lkgIp,
                               settings.lkgNetmask, settings.lkgGateway,
                               settings.lkgDns1, settings.lkgDns2);
}

}  // namespace shotstopper
