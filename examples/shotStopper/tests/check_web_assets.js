'use strict';

const fs = require('fs');
const path = require('path');

const sketchDir = path.resolve(__dirname, '..');
const asset = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssets.h'), 'utf8');
const network = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
const firmware = fs.readFileSync(path.join(sketchDir, 'shotStopper.ino'), 'utf8');
const bleLibrary = fs.readFileSync(path.resolve(sketchDir, '..', '..', 'AcaiaArduinoBLE.cpp'), 'utf8');
const htmlMatch = asset.match(/R"HTML\(([\s\S]*?)\)HTML"/);
if (!htmlMatch) throw new Error('Embedded HTML raw string not found');
const html = htmlMatch[1];
const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
if (!scriptMatch) throw new Error('Embedded script not found');

// Parse the exact JavaScript delivered by the controller.
new Function(scriptMatch[1]);

if (Buffer.byteLength(html, 'utf8') > 32768) {
  throw new Error('Web UI exceeds the 32 KiB asset budget');
}
if (!/lang="en"/.test(html) || !html.includes('role="switch"') ||
    !html.includes('brewConfirmationBeep')) {
  throw new Error('Web UI must use English, expose a paddle switch, and expose the brew beep option');
}
if (!network.includes('"brewConfirmationBeep"') ||
    !firmware.includes('session.config.brewConfirmationBeep')) {
  throw new Error('Brew-confirmation beep must be configurable end-to-end');
}
if (/<script\s+src=|<link\s+[^>]*href=/i.test(html)) {
  throw new Error('Web UI must not depend on external assets');
}

const expected = new Map([
  ['GET /', 'rootHandler'],
  ['POST /api/v1/login', 'loginHandler'],
  ['POST /api/v1/logout', 'logoutHandler'],
  ['POST /api/v1/heartbeat', 'heartbeatHandler'],
  ['GET /api/v1/status', 'statusHandler'],
  ['GET /api/v1/log', 'logHandler'],
  ['POST /api/v1/config', 'configHandler'],
  ['POST /api/v1/calibration/reset', 'resetCalibrationHandler'],
  ['POST /api/v1/control/paddle', 'paddleHandler'],
  ['POST /api/v1/control/rinse', 'rinseHandler'],
  ['POST /api/v1/control/stop', 'stopHandler'],
  ['POST /api/v1/control/restart', 'restartHandler'],
  ['POST /api/v1/network', 'networkHandler'],
  ['POST /api/v1/network/scan', 'wifiScanStartHandler'],
  ['GET /api/v1/network/scan', 'wifiScanStatusHandler'],
  ['POST /api/v1/access-point/password', 'apPasswordHandler'],
]);

for (const [route, handler] of expected) {
  const [method, uri] = route.split(' ');
  const escapedUri = uri.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const registration = new RegExp(
    `registerHandler\\(server_,\\s*"${escapedUri}",\\s*HTTP_${method},\\s*${handler}\\)`
  );
  if (!registration.test(network)) {
    throw new Error(`Missing HTTP registration: ${route} -> ${handler}`);
  }
  if (uri !== '/' && !html.includes(uri.split('?')[0])) {
    throw new Error(`Registered API is not referenced by the UI: ${uri}`);
  }
}

const forbiddenResponseFields = ['staPassword', 'apPassword', 'authHash', 'authSalt'];
const statusFormatStart = network.indexOf('{\\"state\\"');
const statusFormatEnd = network.indexOf('debugEventsDropped', statusFormatStart);
if (statusFormatStart < 0 || statusFormatEnd < 0) {
  throw new Error('Status JSON format not found');
}
const statusFormat = network.slice(statusFormatStart, statusFormatEnd);
for (const field of forbiddenResponseFields) {
  if (statusFormat.includes(field)) {
    throw new Error(`Secret field exposed by status JSON: ${field}`);
  }
}

const networkHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.h'), 'utf8');
if (!/AP_WINDOW_MS\s*=\s*180000/.test(networkHeader)) {
  throw new Error('Fallback AP window must be exactly three minutes');
}
if (!network.includes('WiFi.mode(WIFI_STA)') ||
    !network.includes('WiFi.mode(WIFI_AP)') ||
    network.includes('WIFI_AP_STA')) {
  throw new Error('Network startup must use exclusive STA-first or fallback AP modes');
}
if (!network.includes('WiFi.scanNetworks(true, false, false, 120)') ||
    !network.includes('esp_wifi_scan_stop()')) {
  throw new Error('WiFi scan must be asynchronous and cancelable during active control');
}
if (!network.includes('if (!network.apActive)')) {
  throw new Error('STA-only mode must bypass AP/session shutdown policy');
}

const safeBeepStart = bleLibrary.indexOf('bool AcaiaArduinoBLE::beepWithoutStateChange()');
const safeBeepEnd = bleLibrary.indexOf('bool AcaiaArduinoBLE::heartbeat()', safeBeepStart);
if (safeBeepStart < 0 || safeBeepEnd < 0) {
  throw new Error('State-safe BLE beep implementation not found');
}
const safeBeep = bleLibrary.slice(safeBeepStart, safeBeepEnd);
if (!safeBeep.includes('BEEP_LEVEL_1_BOOKOO') ||
    safeBeep.includes('TARE_ACAIA') || safeBeep.includes('TARE_GENERIC') ||
    safeBeep.includes('_connected = false')) {
  throw new Error('Brew-confirmation beep must not tare or mutate scale connection state');
}
if (!firmware.includes('requestScaleBrewBeep(session.id)') ||
    !firmware.includes('cancelScaleBrewBeep(session.id)') ||
    /enum class ScaleCommandType[\s\S]*BEEP/.test(
      firmware.slice(firmware.indexOf('enum class ScaleCommandType'),
                     firmware.indexOf('enum class ScaleEventType')))) {
  throw new Error('Best-effort beep must stay outside the critical BLE command queue');
}

console.log(`Embedded Web UI: JavaScript valid, ${Buffer.byteLength(html, 'utf8')} bytes, ${expected.size} routes checked`);
