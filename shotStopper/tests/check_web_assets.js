'use strict';

const fs = require('fs');
const path = require('path');

const sketchDir = path.resolve(__dirname, '..');
const asset = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssets.h'), 'utf8');
const network = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
const firmware = fs.readFileSync(path.join(sketchDir, 'shotStopper.ino'), 'utf8');
const bleLibrary = fs.readFileSync(
  path.resolve(sketchDir, '..', 'libraries', 'AcaiaArduinoBLE', 'AcaiaArduinoBLE.cpp'),
  'utf8'
);
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
    !html.includes('Paddle State') || !html.includes('brewConfirmationBeep') ||
    !html.includes('paddleReturnReminderBeep')) {
  throw new Error('Web UI must show the physical paddle state and expose both scale beep options');
}
if (!html.includes('id="operationalWallS" type="number" min="5" max="60"') ||
    !html.includes('CN9 limit ≤ 60 s') ||
    !html.includes('sToMs(') ||
    !html.includes('rinseGestureMs:sToMs') ||
    !network.includes('CN9 limit must be from 5,000 to 60,000 ms.')) {
  throw new Error('CN9 operational limit must be capped at 60 s in the UI and 60,000 ms in the API');
}
if (!network.includes('"brewConfirmationBeep"') ||
    !network.includes('"paddleReturnReminderBeep"') ||
    !network.includes('"paddleReturnReminderIntervalMs"') ||
    !network.includes('"paddleReturnReminderMaxDurationMs"') ||
    !network.includes('"timezoneOffsetMinutes"') ||
    !network.includes('"ntpServerPreset"') ||
    !network.includes('"ntpServerCustom"') ||
    !network.includes('\\"time\\":{') ||
    !html.includes('id="currentTime"') ||
    !html.includes('id="ntpStatus"') ||
    !html.includes('id="ntpServerPreset"') ||
    !html.includes('id="ntpServerCustom"') ||
    !html.includes('id="syncTimeButton"') ||
    !html.includes('/api/v1/time/sync') ||
    !firmware.includes('session.config.brewConfirmationBeep') ||
    !firmware.includes('servicePaddleReturnReminder')) {
  throw new Error('Scale beep settings must be configurable end-to-end');
}
if (!html.includes('authenticatedOnly') || !html.includes('Read-only view') ||
    !html.includes('sessionStorage.setItem') || !html.includes('window.location.reload()') ||
    !network.includes('Status intentionally has no authentication requirement')) {
  throw new Error('Web UI must expose a public read-only mode and reload after authenticated sign-in');
}
const statusSection = html.match(/<fieldset><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statusSection || !statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric"/g) || []).length !== 13 ||
    !html.includes("s.relayClosed?'CLOSED (ON)':'OPEN (OFF)'")) {
  throw new Error('Status must use one metric per row and homologate Paddle/CN9 OPEN/OFF and CLOSED/ON labels');
}
if (!html.includes('remoteReady&&authenticated()') ||
    !html.includes('Remote CN9 actuation is disabled by firmware policy') ||
    !network.includes('\\"remoteControlEnabled\\"') ||
    !network.includes('\\"lastCommand\\"') ||
    !network.includes('\\"maintenance\\"')) {
  throw new Error('Web UI must enforce and display remote policy, maintenance, and durable command state');
}
if (!html.includes('id="shotTable"') ||
    !html.includes('id="exportShotsButton"') ||
    !html.includes('id="clearShotsButton"') ||
    !html.includes("confirm:'CLEAR_SHOT_LOG'") ||
    !html.includes('refreshShots()') ||
    !html.includes('formatShotTime(r)') ||
    !html.includes('sin hora') ||
    !html.includes('id="timezoneOffsetMinutes"') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (!/<fieldset><legend>Log<\/legend>/.test(html) ||
    /authenticatedOnly[^>]*><legend>Log<\/legend>/.test(html) ||
    !html.includes('refreshLog();') ||
    !html.includes('setInterval(()=>refreshLog(),3000)')) {
  throw new Error('Diagnostic log must remain visible and refresh in public read-only mode');
}
if (!html.includes('id="factoryResetButton"') ||
    !html.includes("confirm('Restore every stopper setting") ||
    !html.includes("confirm:'ERASE_ALL_SETTINGS'") ||
    !network.includes('FACTORY_RESET_NOT_CONFIRMED') ||
    !network.includes('resetPersistedSettingsToFactory(next)')) {
  throw new Error('Factory reset must require UI and server-side confirmation');
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
  ['POST /api/v1/factory-reset', 'factoryResetHandler'],
  ['GET /api/v1/shots', 'shotsHandler'],
  ['POST /api/v1/shots/clear', 'shotsClearHandler'],
  ['POST /api/v1/shots/delete', 'shotsDeleteHandler'],
  ['POST /api/v1/time/sync', 'timeSyncHandler'],
  ['POST /api/v1/network', 'networkHandler'],
  ['POST /api/v1/network/scan', 'wifiScanStartHandler'],
  ['GET /api/v1/network/scan', 'wifiScanStatusHandler'],
  ['POST /api/v1/access-point/password', 'apPasswordHandler'],
]);

const maxHandlersMatch = network.match(/max_uri_handlers\s*=\s*(\d+)/);
if (!maxHandlersMatch) {
  throw new Error('HTTP server max_uri_handlers not found');
}
const maxUriHandlers = Number(maxHandlersMatch[1]);
if (maxUriHandlers < expected.size) {
  throw new Error(
    `HTTP server max_uri_handlers (${maxUriHandlers}) is below registered route count (${expected.size})`
  );
}

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
const logHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::logHandler');
const logHandlerEnd = network.indexOf('esp_err_t ShotStopperNetwork::shotsHandler', logHandlerStart);
if (logHandlerStart < 0 || logHandlerEnd < 0) {
  throw new Error('Log handler not found');
}
const logHandler = network.slice(logHandlerStart, logHandlerEnd);
if (logHandler.includes('authenticate(request')) {
  throw new Error('Read-only diagnostic log must not require authentication');
}
for (const field of forbiddenResponseFields) {
  if (logHandler.includes(field)) {
    throw new Error(`Secret field exposed by diagnostic log: ${field}`);
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
    !firmware.includes('scale.supportsTareStartTimer()') ||
    /enum class ScaleCommandType[\s\S]*BEEP/.test(
      firmware.slice(firmware.indexOf('enum class ScaleCommandType'),
                     firmware.indexOf('enum class ScaleEventType')))) {
  throw new Error('Best-effort beep must stay outside the critical BLE command queue');
}

console.log(`Embedded Web UI: JavaScript valid, ${Buffer.byteLength(html, 'utf8')} bytes, ${expected.size} routes checked`);
