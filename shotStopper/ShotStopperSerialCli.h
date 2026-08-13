#pragma once

#include "ShotStopperDomain.h"

namespace shotstopper {

constexpr size_t SERIAL_CLI_LINE_CAPACITY = 160;
constexpr size_t SERIAL_CLI_MAX_BYTES_PER_LOOP = 8;
constexpr size_t SERIAL_CLI_VERB_CAPACITY = 24;

enum class SerialCliVerb : uint8_t {
  NONE = 0,
  HELLO,
  FACTORY_RESET,
  RESET_AP_PASSWORD,
  SET_AP_PASSWORD,
  SET_WIFI,
  CLEAR_SHOTS,
  CLEAR_WIFI,
  RESET_NETWORK_UI,
  UNKNOWN,
  LINE_TOO_LONG,
  INVALID_ARGS
};

struct SerialCliRequest {
  SerialCliVerb verb = SerialCliVerb::NONE;
  char arg1[WIFI_PASSWORD_CAPACITY] = {};
  char arg2[WIFI_PASSWORD_CAPACITY] = {};
  bool openNetwork = false;
  const char *error = nullptr;
};

struct SerialCliParser {
  char line[SERIAL_CLI_LINE_CAPACITY] = {};
  size_t length = 0;
  bool overflow = false;
};

inline const char *serialCliVerbName(SerialCliVerb verb) {
  switch (verb) {
    case SerialCliVerb::NONE: return "NONE";
    case SerialCliVerb::HELLO: return "HELLO";
    case SerialCliVerb::FACTORY_RESET: return "FACTORY_RESET";
    case SerialCliVerb::RESET_AP_PASSWORD: return "RESET_AP_PASSWORD";
    case SerialCliVerb::SET_AP_PASSWORD: return "SET_AP_PASSWORD";
    case SerialCliVerb::SET_WIFI: return "SET_WIFI";
    case SerialCliVerb::CLEAR_SHOTS: return "CLEAR_SHOTS";
    case SerialCliVerb::CLEAR_WIFI: return "CLEAR_WIFI";
    case SerialCliVerb::RESET_NETWORK_UI: return "RESET_NETWORK_UI";
    case SerialCliVerb::UNKNOWN: return "UNKNOWN";
    case SerialCliVerb::LINE_TOO_LONG: return "LINE_TOO_LONG";
    case SerialCliVerb::INVALID_ARGS: return "INVALID_ARGS";
  }
  return "UNKNOWN";
}

inline void serialCliResetParser(SerialCliParser &parser) {
  parser = SerialCliParser{};
}

inline void serialCliClearRequest(SerialCliRequest &request) {
  request = SerialCliRequest{};
}

inline bool serialCliIsSpace(char c) {
  return c == ' ' || c == '\t';
}

inline bool serialCliEqualsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    const char a = (*left >= 'A' && *left <= 'Z')
                       ? static_cast<char>(*left - 'A' + 'a')
                       : *left;
    const char b = (*right >= 'A' && *right <= 'Z')
                       ? static_cast<char>(*right - 'A' + 'a')
                       : *right;
    if (a != b) {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

enum class SerialCliTokenResult : uint8_t { END, OK, ERROR };

inline SerialCliTokenResult serialCliNextToken(const char *&cursor, char *out,
                                               size_t outSize,
                                               const char **error) {
  if (cursor == nullptr || out == nullptr || outSize < 2) {
    if (error != nullptr) {
      *error = "invalid arguments";
    }
    return SerialCliTokenResult::ERROR;
  }
  while (*cursor != '\0' && serialCliIsSpace(*cursor)) {
    ++cursor;
  }
  if (*cursor == '\0') {
    out[0] = '\0';
    return SerialCliTokenResult::END;
  }

  size_t length = 0;
  if (*cursor == '"') {
    ++cursor;
    while (*cursor != '\0' && *cursor != '"') {
      if (length + 1 >= outSize) {
        out[0] = '\0';
        if (error != nullptr) {
          *error = "argument too long";
        }
        return SerialCliTokenResult::ERROR;
      }
      out[length++] = *cursor++;
    }
    if (*cursor != '"') {
      out[0] = '\0';
      if (error != nullptr) {
        *error = "unclosed quote";
      }
      return SerialCliTokenResult::ERROR;
    }
    ++cursor;
    out[length] = '\0';
    return SerialCliTokenResult::OK;
  }

  while (*cursor != '\0' && !serialCliIsSpace(*cursor) && *cursor != '"') {
    if (length + 1 >= outSize) {
      out[0] = '\0';
      if (error != nullptr) {
        *error = "argument too long";
      }
      return SerialCliTokenResult::ERROR;
    }
    out[length++] = *cursor++;
  }
  out[length] = '\0';
  return SerialCliTokenResult::OK;
}

inline bool serialCliParseLine(const char *line, SerialCliRequest &request) {
  serialCliClearRequest(request);
  if (line == nullptr) {
    request.verb = SerialCliVerb::INVALID_ARGS;
    request.error = "invalid arguments";
    return true;
  }

  const char *cursor = line;
  char verb[SERIAL_CLI_VERB_CAPACITY] = {};
  const char *tokenError = nullptr;
  const SerialCliTokenResult verbResult =
      serialCliNextToken(cursor, verb, sizeof(verb), &tokenError);
  if (verbResult == SerialCliTokenResult::END) {
    return false;
  }
  if (verbResult != SerialCliTokenResult::OK) {
    request.verb = SerialCliVerb::INVALID_ARGS;
    request.error = tokenError != nullptr ? tokenError : "invalid arguments";
    return true;
  }

  char args[2][WIFI_PASSWORD_CAPACITY] = {};
  size_t argCount = 0;
  while (argCount < 2) {
    tokenError = nullptr;
    const SerialCliTokenResult result = serialCliNextToken(
        cursor, args[argCount], sizeof(args[argCount]), &tokenError);
    if (result == SerialCliTokenResult::END) {
      break;
    }
    if (result != SerialCliTokenResult::OK) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = tokenError != nullptr ? tokenError : "invalid arguments";
      return true;
    }
    ++argCount;
  }

  char extra[4] = {};
  tokenError = nullptr;
  if (serialCliNextToken(cursor, extra, sizeof(extra), &tokenError) !=
      SerialCliTokenResult::END) {
    request.verb = SerialCliVerb::INVALID_ARGS;
    request.error = tokenError != nullptr ? tokenError : "too many arguments";
    return true;
  }

  auto requireNoArgs = [&](SerialCliVerb verbId) {
    if (argCount != 0) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = "command takes no arguments";
      return false;
    }
    request.verb = verbId;
    return true;
  };

  if (serialCliEqualsIgnoreCase(verb, "hello")) {
    return requireNoArgs(SerialCliVerb::HELLO);
  }
  if (serialCliEqualsIgnoreCase(verb, "factory_reset")) {
    return requireNoArgs(SerialCliVerb::FACTORY_RESET);
  }
  if (serialCliEqualsIgnoreCase(verb, "reset_ap_password")) {
    return requireNoArgs(SerialCliVerb::RESET_AP_PASSWORD);
  }
  if (serialCliEqualsIgnoreCase(verb, "clear_shots")) {
    return requireNoArgs(SerialCliVerb::CLEAR_SHOTS);
  }
  if (serialCliEqualsIgnoreCase(verb, "clear_wifi")) {
    return requireNoArgs(SerialCliVerb::CLEAR_WIFI);
  }
  if (serialCliEqualsIgnoreCase(verb, "reset_network_ui")) {
    return requireNoArgs(SerialCliVerb::RESET_NETWORK_UI);
  }

  if (serialCliEqualsIgnoreCase(verb, "set_ap_password")) {
    if (argCount != 1) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = "SET_AP_PASSWORD requires a password";
      return true;
    }
    if (!validAccessPointPassword(args[0])) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = "AP/UI password must be 8-63 characters";
      return true;
    }
    if (strcmp(args[0], "Micra1234") == 0) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error =
          "password cannot be the factory default; use RESET_AP_PASSWORD";
      return true;
    }
    request.verb = SerialCliVerb::SET_AP_PASSWORD;
    strncpy(request.arg1, args[0], sizeof(request.arg1) - 1);
    return true;
  }

  if (serialCliEqualsIgnoreCase(verb, "set_wifi")) {
    if (argCount < 1 || argCount > 2) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = "SET_WIFI requires SSID and optional password";
      return true;
    }
    if (!validWifiSsid(args[0])) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = "Wi-Fi SSID must be 1-32 characters";
      return true;
    }
    const bool openNetwork = argCount == 1 || args[1][0] == '\0';
    if (!validWifiPassword(openNetwork ? "" : args[1], openNetwork)) {
      request.verb = SerialCliVerb::INVALID_ARGS;
      request.error = "Wi-Fi password must be 8-63 characters";
      return true;
    }
    request.verb = SerialCliVerb::SET_WIFI;
    strncpy(request.arg1, args[0], sizeof(request.arg1) - 1);
    if (!openNetwork) {
      strncpy(request.arg2, args[1], sizeof(request.arg2) - 1);
    }
    request.openNetwork = openNetwork;
    return true;
  }

  request.verb = SerialCliVerb::UNKNOWN;
  request.error = "unknown command";
  return true;
}

inline bool serialCliFeed(SerialCliParser &parser, char received,
                          SerialCliRequest &request) {
  serialCliClearRequest(request);
  if (received == '\r' || received == '\n') {
    if (parser.overflow) {
      serialCliResetParser(parser);
      request.verb = SerialCliVerb::LINE_TOO_LONG;
      request.error = "line too long";
      return true;
    }
    if (parser.length == 0) {
      return false;
    }
    parser.line[parser.length] = '\0';
    const bool ready = serialCliParseLine(parser.line, request);
    serialCliResetParser(parser);
    return ready;
  }
  if (parser.overflow) {
    return false;
  }
  if (parser.length + 1 >= SERIAL_CLI_LINE_CAPACITY) {
    parser.overflow = true;
    parser.length = 0;
    return false;
  }
  parser.line[parser.length++] = received;
  return false;
}

}  // namespace shotstopper
