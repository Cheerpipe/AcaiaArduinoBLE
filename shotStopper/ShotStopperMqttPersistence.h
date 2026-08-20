#pragma once

#include "ShotStopperPersistence.h"

#include <string.h>

namespace shotstopper {

constexpr uint32_t MQTT_SETTINGS_MAGIC = 0x4D515454U;  // "MQTT"
constexpr uint16_t MQTT_SETTINGS_VERSION = 1;
constexpr const char *MQTT_SLOT_A = "mqttCfgA";
constexpr const char *MQTT_SLOT_B = "mqttCfgB";

constexpr size_t MQTT_HOST_CAPACITY = 64;
constexpr size_t MQTT_USERNAME_CAPACITY = 32;
constexpr size_t MQTT_PASSWORD_CAPACITY = 64;
constexpr size_t MQTT_CLIENT_ID_CAPACITY = 32;
constexpr size_t MQTT_DISCOVERY_PREFIX_CAPACITY = 32;
constexpr size_t MQTT_BASE_TOPIC_CAPACITY = 48;

struct MqttPersistedSettings {
  uint32_t magic = MQTT_SETTINGS_MAGIC;
  uint16_t version = MQTT_SETTINGS_VERSION;
  uint16_t structureSize = sizeof(MqttPersistedSettings);
  uint32_t revision = 0;
  uint8_t enabled = 0;
  uint8_t reserved0 = 0;
  uint16_t port = 1883;
  char host[MQTT_HOST_CAPACITY] = {};
  char username[MQTT_USERNAME_CAPACITY] = {};
  char password[MQTT_PASSWORD_CAPACITY] = {};
  char clientId[MQTT_CLIENT_ID_CAPACITY] = {};
  char discoveryPrefix[MQTT_DISCOVERY_PREFIX_CAPACITY] = "homeassistant";
  char baseTopic[MQTT_BASE_TOPIC_CAPACITY] = "micra_shot_stopper";
  uint32_t checksum = 0;
};

inline uint32_t mqttSettingsChecksum(const MqttPersistedSettings &settings) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&settings);
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < offsetof(MqttPersistedSettings, checksum);
       ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline void normalizeMqttSettings(MqttPersistedSettings &settings) {
  settings.enabled = settings.enabled != 0 ? 1 : 0;
  if (settings.port == 0) {
    settings.port = 1883;
  }
  settings.host[MQTT_HOST_CAPACITY - 1] = '\0';
  settings.username[MQTT_USERNAME_CAPACITY - 1] = '\0';
  settings.password[MQTT_PASSWORD_CAPACITY - 1] = '\0';
  settings.clientId[MQTT_CLIENT_ID_CAPACITY - 1] = '\0';
  settings.discoveryPrefix[MQTT_DISCOVERY_PREFIX_CAPACITY - 1] = '\0';
  settings.baseTopic[MQTT_BASE_TOPIC_CAPACITY - 1] = '\0';
  if (settings.discoveryPrefix[0] == '\0') {
    strncpy(settings.discoveryPrefix, "homeassistant",
            MQTT_DISCOVERY_PREFIX_CAPACITY - 1);
  }
  if (settings.baseTopic[0] == '\0') {
    strncpy(settings.baseTopic, "micra_shot_stopper",
            MQTT_BASE_TOPIC_CAPACITY - 1);
  }
}

inline void finalizeMqttSettings(MqttPersistedSettings &settings) {
  settings.magic = MQTT_SETTINGS_MAGIC;
  settings.version = MQTT_SETTINGS_VERSION;
  settings.structureSize = sizeof(MqttPersistedSettings);
  normalizeMqttSettings(settings);
  settings.checksum = 0;
  settings.checksum = mqttSettingsChecksum(settings);
}

inline bool validMqttSettings(const MqttPersistedSettings &settings) {
  return settings.magic == MQTT_SETTINGS_MAGIC &&
         settings.version == MQTT_SETTINGS_VERSION &&
         settings.structureSize == sizeof(MqttPersistedSettings) &&
         settings.enabled <= 1 && settings.port > 0 &&
         settings.checksum == mqttSettingsChecksum(settings);
}

inline bool readMqttSlot(Preferences &preferences, const char *key,
                         MqttPersistedSettings &settings) {
  if (preferences.getBytesLength(key) != sizeof(settings) ||
      preferences.getBytes(key, &settings, sizeof(settings)) !=
          sizeof(settings)) {
    return false;
  }
  return validMqttSettings(settings);
}

inline bool loadMqttSettings(MqttPersistedSettings &settings) {
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    return false;
  }
  MqttPersistedSettings first;
  MqttPersistedSettings second;
  const bool firstValid = readMqttSlot(preferences, MQTT_SLOT_A, first);
  const bool secondValid = readMqttSlot(preferences, MQTT_SLOT_B, second);
  preferences.end();
  if (!firstValid && !secondValid) {
    return false;
  }
  if (!firstValid) {
    settings = second;
  } else if (!secondValid) {
    settings = first;
  } else if (static_cast<int32_t>(second.revision - first.revision) > 0) {
    settings = second;
  } else {
    settings = first;
  }
  return true;
}

inline bool saveMqttSettings(MqttPersistedSettings &settings) {
  lockSettingsNvs();
  MqttPersistedSettings current;
  if (loadMqttSettings(current)) {
    settings.revision = current.revision;
  }
  ++settings.revision;
  if (settings.revision == 0) {
    settings.revision = 1;
  }
  finalizeMqttSettings(settings);
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  const char *target =
      (settings.revision & 1U) != 0 ? MQTT_SLOT_A : MQTT_SLOT_B;
  const bool written =
      preferences.putBytes(target, &settings, sizeof(settings)) ==
      sizeof(settings);
  MqttPersistedSettings verified;
  const bool saved = written && readMqttSlot(preferences, target, verified) &&
                     verified.revision == settings.revision;
  preferences.end();
  unlockSettingsNvs();
  return saved;
}

inline bool resetMqttSettings(MqttPersistedSettings &settings) {
  settings = MqttPersistedSettings{};
  return saveMqttSettings(settings);
}

}  // namespace shotstopper
