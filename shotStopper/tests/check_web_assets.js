'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const webUi = require('../../scripts/gen_web_ui.js');

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

if (Buffer.byteLength(html, 'utf8') > 65536) {
  throw new Error('Web UI source exceeds the 64 KiB authoring budget');
}
if (!/lang="en"/.test(html) || !html.includes('role="switch"') ||
    !html.includes('Paddle State') || !html.includes('firstDropBeep') ||
    !html.includes('paddleReturnReminderBeep') ||
    !html.includes('class="fieldHint"')) {
  throw new Error('Web UI must show the physical paddle state and expose both scale beep options');
}
if (!html.includes('id="operationalWallS" type="number" min="5" max="60"') ||
    !html.includes('hard-caps at 60 s') ||
    !html.includes('sToMs(') ||
    !html.includes('rinseGestureMs:sToMs') ||
    !network.includes('CN9 limit must be from 5 to 60 s.')) {
  throw new Error('CN9 operational limit must be capped at 60 s in the UI and API messages');
}
if (!html.includes('function rangeCheck(') ||
    !html.includes('function showFieldError(') ||
    !html.includes('samples × sample gap') ||
    !html.includes('aria-live') ||
    !html.includes('fieldError') ||
    !html.includes('id="goalWeightG" type="number" min="10" max="200" step="1"') ||
    !html.includes('validateNetworkClient') ||
    !html.includes('validateApClient') ||
    !network.includes('Max recovery must be from 10 to 200 g.') ||
    !network.includes('Fast guard requires max recovery') ||
    !network.includes('SSID must be 1–32 characters.') ||
    !network.includes('Current password is incorrect.') ||
    !network.includes('configValidationErrorName(error)')) {
  throw new Error('UI/API must expose specific validation ranges, inline errors, and field-aware config errors');
}
if (!network.includes('"firstDropBeep"') ||
    !network.includes('"paddleReturnReminderBeep"') ||
    !network.includes('"autoRetare"') ||
    !network.includes('"retareWindowMs"') ||
    !network.includes('"minimumCupWeightG"') ||
    !network.includes('"retareStabilitySamples"') ||
    !network.includes('"retareStabilityToleranceG"') ||
    !network.includes('"retareStabilityMaxGapMs"') ||
    !network.includes('"retareStabilityMinDurationMs"') ||
    !network.includes('"bbwProtectionMs"') ||
    !network.includes('"fastExtractionGuardEnabled"') ||
    !network.includes('"maxRecoveryWeightG"') ||
    !network.includes('"minBrewTimeMs"') ||
    !network.includes('\\"extractionExtended\\"') ||
    !network.includes('\\"stopDetail\\"') ||
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
    !html.includes('id="autoRetare"') ||
    !html.includes('id="fastExtractionGuardEnabled"') ||
    !html.includes('id="maxRecoveryWeightG"') ||
    !html.includes('id="minBrewTimeS"') ||
    !html.includes('Fast extraction guard') ||
    !html.includes('id="retareWindowS"') ||
    !html.includes('id="minimumCupWeightG"') ||
    !html.includes('id="retareStabilitySamples"') ||
    !html.includes('id="retareStabilityToleranceG"') ||
    !html.includes('id="retareStabilityMaxGapS"') ||
    !html.includes('id="retareStabilityMinDurationS"') ||
    !html.includes('BBW protection (s)') ||
    !html.includes('id="bbwProtectionS"') ||
    !html.includes('Paddle reminder limit (min)') ||
    !html.includes('id="paddleReturnReminderMaxDurationMin"') ||
    !html.includes("number('paddleReturnReminderMaxDurationMin')*60000") ||
    !html.includes('id="sessionBar"') ||
    !html.includes('id="logoutButton"') ||
    html.includes('Up to 120 shots') ||
    !html.includes('/api/v1/time/sync') ||
    !firmware.includes('session.config.firstDropBeep') ||
    !firmware.includes('servicePaddleReturnReminder')) {
  throw new Error('Scale beep settings must be configurable end-to-end');
}
if (!html.includes('authenticatedOnly') ||
    !html.includes('sessionStorage.setItem') || !html.includes('window.location.reload()') ||
    !html.includes('pageNav authenticatedOnly') ||
    !html.includes('function knownPath(') ||
    !html.includes("authenticated()&&known") ||
    !network.includes('Status intentionally has no authentication requirement') ||
    !network.includes('HTTPD_404_NOT_FOUND') ||
    !network.includes('notFoundHandler')) {
  throw new Error('Web UI must expose a public read-only Home, hide other tabs until sign-in, and redirect unknown routes to /');
}
const statusSection = html.match(/<fieldset[^>]*><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statusSection || !statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric"/g) || []).length !== 21 ||
    !html.includes("s.relayClosed?'CLOSED (ON)':'OPEN (OFF)'") ||
    !html.includes('id="scaleWeight"') ||
    !html.includes('id="scaleTimer"') ||
    !html.includes('Weight (scale)') ||
    !html.includes('Timer (scale)') ||
    !html.includes('function formatScaleWeight(') ||
    !html.includes('function formatScaleTimer(') ||
    !network.includes('\\"timerMs\\"')) {
  throw new Error('Status must use one metric per row and homologate Paddle/CN9 OPEN/OFF and CLOSED/ON labels');
}
if (!html.includes('id="shotPanel"') ||
    !html.includes('id="shotBar"') ||
    !html.includes('id="shotElapsed"') ||
    !html.includes('id="shotFirstDrop"') ||
    !html.includes('id="shotRetare"') ||
    !html.includes('id="shotCurrentWeight"') ||
    !html.includes('id="shotGoalWeight"') ||
    !html.includes('id="shotType"') ||
    !html.includes('id="shotScale"') ||
    !html.includes('function updateShot(') ||
    !network.includes('elapsedMs') ||
    !network.includes('retarePerformed') ||
    !network.includes('shotType') ||
    !network.includes('scaleProtocol') ||
    !network.includes('safeScaleProtocol') ||
    !html.includes('remoteReady&&authenticated()') ||
    !html.includes('Remote CN9 disabled by policy') ||
    !network.includes('\\"remoteControlEnabled\\"') ||
    !network.includes('\\"lastCommand\\"') ||
    !network.includes('\\"maintenance\\"') ||
    !network.includes('\\"cycle\\"') ||
    !network.includes('flowDuringRetare') ||
    !html.includes('updateShot(s)')) {
  throw new Error('Web UI must enforce remote policy, maintenance, durable command state, and live shot status');
}
if (!html.includes('id="autoToManualGuardEnabled"') ||
    !html.includes('id="autoToManualGuardLimitMode"') ||
    !html.includes('id="autoToManualGuardBaselineS"') ||
    !html.includes('id="shotTimerStartDelayMs"') ||
    !html.includes('id="autoToManualGuardManualLimitS"') ||
    !html.includes('id="autoToManualGuardTrendS"') ||
    !html.includes('id="resetGuardSamplesButton"') ||
    html.indexOf('id="autoToManualGuardLimitMode"') >
        html.indexOf('id="autoToManualGuardManualLimitS"') ||
    html.indexOf('id="autoToManualGuardManualLimitS"') >
        html.indexOf('id="autoToManualGuardTrendS"') ||
    html.indexOf('id="autoToManualGuardTrendS"') >
        html.indexOf('id="autoToManualGuardBaselineS"') ||
    html.indexOf('id="autoToManualGuardBaselineS"') >
        html.indexOf('id="resetGuardSamplesButton"') ||
    !html.includes('Reset A→M samples to baseline') ||
    !html.includes('id="shotAtmGuard"') ||
    !html.includes('A→M ·') ||
    !html.includes('actual_weight_source') ||
    !network.includes('autoToManualGuardEnabled') ||
    !network.includes('autoToManualGuardBaselineMs') ||
    !network.includes('shotTimerStartDelayMs') ||
    !network.includes('autoToManualGuardTrendMs') ||
    !network.includes('autoToManualGuardEnforced') ||
    !network.includes('autoToManualGuardArmed') ||
    !network.includes('actualWeightSource') ||
    !network.includes('reset-guard-samples') ||
    !network.includes('AUTO_TO_MANUAL_GUARD')) {
  throw new Error('Auto-to-manual time guard must be wired in config UI, live panel, shots API, and routes');
}
if (!html.includes('id="hCpu"') ||
    !html.includes('id="hUptime"') ||
    !html.includes('id="hResetReason"') ||
    !html.includes('id="hTemp"') ||
    !html.includes('id="hTPeak"') ||
    !html.includes('id="hRamT"') ||
    !html.includes('id="hRamU"') ||
    !html.includes('id="hRamF"') ||
    !html.includes('function updH(') ||
    !html.includes('updH(s.health,s.safety)') ||
    !html.includes('h.uptimeMs') ||
    !html.includes('resetReasonCode') ||
    !html.includes("RR[s.resetReasonCode]") ||
    !network.includes('\\"hwmon\\"') ||
    !network.includes('cpuUsagePct') ||
    !network.includes('tempPeakC') ||
    !network.includes('ramTotalBytes') ||
    !network.includes('\\"uptimeMs\\"') ||
    !network.includes('\\"resetReasonCode\\"')) {
  throw new Error('Diagnostics must expose basic hwmon metrics in UI and status API');
}
if (!html.includes('id="shotTable"') ||
    !html.includes('id="exportShotsButton"') ||
    !html.includes('id="clearShotsButton"') ||
    !html.includes("confirm:'CLEAR_SHOT_LOG'") ||
    !html.includes('refreshShots()') ||
    !html.includes('formatShotTime(r)') ||
    !html.includes('no time') ||
    !html.includes('id="timezoneOffsetMinutes"') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (!html.includes('id="firmwareFooter"') ||
    !html.includes('firmwareVersion') ||
    !html.includes('updateFirmwareFooter()') ||
    !network.includes('\\"firmwareVersion\\"') ||
    !network.includes('FW_VERSION')) {
  throw new Error('Firmware version must be exposed in status API and web footer');
}
if (!/<fieldset[^>]*><legend>Log<\/legend>/.test(html) ||
    /authenticatedOnly[^>]*><legend>Log<\/legend>/.test(html) ||
    !html.includes('loadLog()') ||
    !html.includes('setInterval(()=>refreshLog(),2500)') ||
    !html.includes('id="view-log"') ||
    !html.includes("name==='log'") ||
    !html.includes('id="logLevelFilter"') ||
    !html.includes('e.level') ||
    !html.includes('value="boot"')) {
  throw new Error('Diagnostic log must remain a public view with view-scoped refresh');
}
if (!html.includes('id="factoryResetButton"') ||
    !html.includes("confirm('Restore all factory settings?") ||
    !html.includes("confirm:'ERASE_ALL_SETTINGS'") ||
    !network.includes('FACTORY_RESET_NOT_CONFIRMED') ||
    !network.includes('resetPersistedSettingsToFactory(next)')) {
  throw new Error('Factory reset must require UI and server-side confirmation');
}
if (!html.includes('id="staIpMode"') ||
    !html.includes('id="staStaticIp"') ||
    !html.includes('id="staNetmask"') ||
    !html.includes('id="staGateway"') ||
    !html.includes('id="staDns1"') ||
    !html.includes('function networkSavePayload(') ||
    !html.includes("ipMode:$('staIpMode').value") ||
    !html.includes('staticIpOpt') ||
    !html.includes('pending confirm') ||
    !html.includes('savedStaSsid') ||
    !html.includes('Leave empty to keep the saved password') ||
    !html.includes('keep=!!savedStaSsid') ||
    !html.includes("savedStaSsid=n.wifiConfigured&&n.ssid?n.ssid:''") ||
    !network.includes('WiFi.config(') ||
    !network.includes('confirmPendingNetwork') ||
    !network.includes('revertPendingNetwork') ||
    !network.includes('\\"ipMode\\"') ||
    !network.includes('\\"configState\\"') ||
    !network.includes('\\"ssid\\"') ||
    !network.includes('shouldReuseSavedWifiCredentials') ||
    !network.includes('or empty to keep the saved password.') ||
    !network.includes('StaIpMode::STATIC') ||
    !network.includes('STA_CONFIRM_TIMEOUT_MS') ||
    !network.includes('action must be \\"save\\", \\"forget\\", or \\"confirm\\".') ||
    !network.includes('No pending network configuration to confirm.')) {
  throw new Error('DHCP/static IP mode must be wired in UI, status, WiFi.config, and confirm/revert path');
}
if (/<script\s+src=/i.test(html)) {
  throw new Error('Web UI must keep JavaScript embedded (no external script src)');
}
if (!/<link\s+rel="stylesheet"\s+href="\/app\.css\?v=/.test(html) &&
    !html.includes('href="/app.css?v=__FW_VERSION__"')) {
  throw new Error('Web UI must load same-origin /app.css with a firmware version query');
}
if (/<link\s+[^>]*href=["']https?:\/\//i.test(html) ||
    /cdn\.|unpkg\.|jsdelivr\./i.test(html)) {
  throw new Error('Web UI must not depend on CDN or third-party assets');
}

const expected = new Map([
  ['GET /', 'rootHandler'],
  ['GET /log', 'rootHandler'],
  ['GET /history', 'rootHandler'],
  ['GET /admin', 'rootHandler'],
  ['GET /settings', 'rootHandler'],
  ['GET /app.css', 'cssHandler'],
  ['POST /api/v1/login', 'loginHandler'],
  ['POST /api/v1/logout', 'logoutHandler'],
  ['POST /api/v1/heartbeat', 'heartbeatHandler'],
  ['GET /api/v1/status', 'statusHandler'],
  ['GET /api/v1/log', 'logHandler'],
  ['POST /api/v1/config', 'configHandler'],
  ['POST /api/v1/calibration/reset', 'resetCalibrationHandler'],
  ['POST /api/v1/calibration/reset-guard-samples', 'resetGuardSamplesHandler'],
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

const maxSocketsMatch = network.match(/max_open_sockets\s*=\s*(\d+)/);
if (!maxSocketsMatch || Number(maxSocketsMatch[1]) < 10) {
  throw new Error('HTTP server must allow at least 10 open sockets for Web UI polling');
}
const maxRespHeadersMatch = network.match(/max_resp_headers\s*=\s*(\d+)/);
if (!maxRespHeadersMatch || Number(maxRespHeadersMatch[1]) < 12) {
  throw new Error('HTTP server must allow at least 12 response headers for gzip and ETag');
}
if (!html.includes('function withPollGate(') ||
    !html.includes('noteReachFail(') ||
    !html.includes('function startView(') ||
    !html.includes('function stopViewPolls(') ||
    !html.includes('function renderRoute(') ||
    !html.includes('setInterval(()=>refreshStatus(),2500)')) {
  throw new Error('Web UI must serialize view-scoped polls and soft-fail unreachable bursts');
}
if (!html.includes('async function loadStatus(){') ||
    !html.includes('async function loadShots(){') ||
    !html.includes('async function loadLog(){') ||
    !html.includes('function refreshStatus(){return withPollGate(loadStatus)}') ||
    !html.includes('function refreshShots(){return withPollGate(loadShots)}') ||
    !html.includes('function refreshLog(){return withPollGate(loadLog)}') ||
    !html.includes("name==='home'||name==='settings'||name==='admin'") ||
    !html.includes("name==='history'") ||
    !html.includes('renderRoute(location.pathname)') ||
    html.includes('Promise.all([loadShots(),loadLog()])')) {
  throw new Error('Web UI must lazy-load status/shots/log per active SPA view; background polls stay gated');
}
if (!html.includes('id="view-home"') ||
    !html.includes('id="view-history"') ||
    !html.includes('id="view-settings"') ||
    !html.includes('id="view-admin"') ||
    !html.includes('data-route="/settings"') ||
    !html.includes('data-route="/admin"') ||
    !html.includes('history.pushState')) {
  throw new Error('Web UI must expose Home/History/Admin/Log/Settings routes as an SPA');
}
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
if (network.includes('\\"passwordChangeRequired\\"') ||
    network.includes('PASSWORD_CHANGE_REQUIRED') ||
    html.includes('passwordChangeRequired') ||
    html.includes('factory AP/UI password') ||
    html.includes('Change the factory AP/UI password')) {
  throw new Error('Factory password change gate must remain removed from status/UI/API');
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
  throw new Error('First-drop beep must not tare or mutate scale connection state');
}
if (!firmware.includes('requestScaleBrewBeep(session.id)') ||
    !firmware.includes('cancelScaleBrewBeep(session.id)') ||
    !firmware.includes('onFirstDropsDetected') ||
    !firmware.includes('notifyRetareFlowDetected') ||
    !firmware.includes('retareFlowFirstDetectedAtMs') ||
    !firmware.includes('bbwProtectionActive') ||
    !firmware.includes('retareWindowOpen') ||
    !firmware.includes('scale.supportsTareStartTimer()') ||
    /enum class ScaleCommandType[\s\S]*BEEP/.test(
      firmware.slice(firmware.indexOf('enum class ScaleCommandType'),
                     firmware.indexOf('enum class ScaleEventType')))) {
  throw new Error('Best-effort beep must stay outside the critical BLE command queue');
}

const generated = webUi.generate();
const roundTrip = zlib.gunzipSync(generated.gzip).toString('utf8');
if (roundTrip !== generated.html) {
  throw new Error('Generated gzip Web UI does not round-trip to the minified HTML');
}
const cssRoundTrip = zlib.gunzipSync(generated.cssGzip).toString('utf8');
if (cssRoundTrip !== generated.css) {
  throw new Error('Generated gzip Web CSS does not round-trip to the minified CSS');
}
if (generated.gzip.length > 18432) {
  throw new Error('Compressed Web UI HTML exceeds the 18 KiB gzip budget');
}
if (generated.cssGzip.length > 6144) {
  throw new Error('Compressed Web CSS exceeds the 6 KiB gzip budget');
}
if (generated.gzip.length + generated.cssGzip.length > 22528) {
  throw new Error('Combined HTML+CSS gzip exceeds the 22 KiB flash budget');
}
if (!network.includes('#include "ShotStopperWebAssetsGzip.h"') ||
    network.includes('#include "ShotStopperWebAssets.h"')) {
  throw new Error('Firmware must embed the gzip Web UI, not the HTML source string');
}
if (!network.includes('SHOT_STOPPER_WEB_UI_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_UI_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP_LEN') ||
    !network.includes('"Content-Encoding"') ||
    !network.includes('"gzip"')) {
  throw new Error('GET / and GET /app.css must send precompressed gzip bodies');
}
if (network.includes('zlib.h') || network.includes('miniz.h') ||
    /mz_compress|deflateInit|gzipCompress/.test(network)) {
  throw new Error('Firmware must not compress the Web UI at runtime');
}
if (!network.includes('If-None-Match')) {
  throw new Error('GET / must honor If-None-Match for cached Web UI revalidation');
}
const rootHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::rootHandler');
const cssHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::cssHandler');
const notFoundHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::notFoundHandler');
const loginHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::loginHandler');
if (rootHandlerStart < 0 || cssHandlerStart < 0 || notFoundHandlerStart < 0 ||
    loginHandlerStart < 0 ||
    !(rootHandlerStart < cssHandlerStart && cssHandlerStart < notFoundHandlerStart &&
      notFoundHandlerStart < loginHandlerStart)) {
  throw new Error('rootHandler/cssHandler/notFoundHandler order not found');
}
const rootHandler = network.slice(rootHandlerStart, cssHandlerStart);
const cssHandler = network.slice(cssHandlerStart, notFoundHandlerStart);
const notFoundHandler = network.slice(notFoundHandlerStart, loginHandlerStart);
if (rootHandler.includes('no-store') || !rootHandler.includes('no-cache') ||
    !rootHandler.includes('STATUS_NOT_MODIFIED') ||
    !rootHandler.includes('ifNoneMatchEquals') ||
    !rootHandler.includes('ETag') ||
    !rootHandler.includes("style-src 'self'") ||
    rootHandler.includes('HTTPD_RESP_USE_STRLEN')) {
  throw new Error('GET / must revalidate with ETag/304, CSP style-src self, and gzip by length');
}
if (cssHandler.includes('no-store') ||
    !cssHandler.includes('max-age=31536000') ||
    !cssHandler.includes('immutable') ||
    !cssHandler.includes('STATUS_NOT_MODIFIED') ||
    !cssHandler.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !cssHandler.includes('text/css')) {
  throw new Error('GET /app.css must serve immutable gzip CSS with ETag/304');
}
if (!notFoundHandler.includes('302 Found') ||
    !notFoundHandler.includes('Location') ||
    !notFoundHandler.includes('"/api/"') ||
    !notFoundHandler.includes('STATUS_NOT_FOUND') ||
    notFoundHandler.includes('!= nullptr')) {
  throw new Error('Unknown non-API routes must 302 to /, while unknown /api/* stay JSON 404');
}
if (network.includes('sendJson') &&
    !network.slice(network.indexOf('esp_err_t ShotStopperNetwork::sendJson'),
                   network.indexOf('esp_err_t ShotStopperNetwork::sendError'))
        .includes('no-store')) {
  throw new Error('JSON API responses must remain Cache-Control: no-store');
}

console.log(
  `Embedded Web UI: JavaScript valid, ${Buffer.byteLength(html, 'utf8')} bytes HTML source, ` +
  `${generated.gzip.length} bytes HTML gzip, ${generated.cssGzip.length} bytes CSS gzip, ` +
  `${expected.size} routes checked`
);
