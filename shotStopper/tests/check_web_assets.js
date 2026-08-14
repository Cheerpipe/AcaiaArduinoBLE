'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const webUi = require('../../scripts/gen_web_ui.js');

const sketchDir = path.resolve(__dirname, '..');
const asset = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssets.h'), 'utf8');
const network = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
const firmware = fs.readFileSync(path.join(sketchDir, 'shotStopper.ino'), 'utf8');
const domain = fs.readFileSync(path.join(sketchDir, 'ShotStopperDomain.h'), 'utf8');
const buzzer = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzer.h'), 'utf8');
const bleLibrary = fs.readFileSync(
  path.resolve(sketchDir, '..', 'libraries', 'AcaiaArduinoBLE', 'AcaiaArduinoBLE.cpp'),
  'utf8'
);
const htmlMatch = asset.match(/R"HTML\(([\s\S]*?)\)HTML"/);
if (!htmlMatch) throw new Error('Embedded HTML raw string not found');
const html = htmlMatch[1];
const js = fs.readFileSync(path.join(sketchDir, 'web', 'app.js'), 'utf8');
const css = fs.readFileSync(path.join(sketchDir, 'web', 'app.css'), 'utf8');
const logo = fs.readFileSync(path.join(sketchDir, 'web', 'logo.svg'), 'utf8');
const ui = html + '\n' + js;
if (!logo.includes('<svg') || !logo.includes('viewBox=')) {
  throw new Error('Web UI logo.svg must be a valid SVG asset');
}
if (!css.includes('.brandLogo') ||
    !css.includes('height:1em') ||
    !css.includes('inline-flex') ||
    !css.includes('.brandLogo{filter:invert(1)}')) {
  throw new Error('Brand logo must match heading height and invert in dark mode');
}
if (!html.includes('src="/app.js?v=__FW_VERSION__"') ||
    /<script(?![^>]*\bsrc=)[^>]*>\s*\S/i.test(html)) {
  throw new Error('Web UI must load same-origin /app.js (no inline script body)');
}

// Parse the authored JavaScript source (pre-minify).
new Function(js);

const htmlBytes = Buffer.byteLength(html, 'utf8');
const jsBytes = Buffer.byteLength(js, 'utf8');
if (htmlBytes > 40960) {
  throw new Error('Web UI HTML source exceeds the 40 KiB authoring budget');
}
if (jsBytes > 61440) {
  throw new Error('Web UI JS source exceeds the 60 KiB authoring budget');
}
if (htmlBytes + jsBytes > 86016) {
  throw new Error('Web UI HTML+JS source exceeds the 84 KiB combined authoring budget');
}
if (!/lang="en"/.test(html) || !ui.includes('role="switch"') ||
    !ui.includes('Paddle State') || !ui.includes('firstDropBeep') ||
    !ui.includes('paddleReturnReminderBeep') ||
    !ui.includes('buzzerScaleLostBeep') ||
    !ui.includes('buzzerAutoToManualGuardEndBeep') ||
    !ui.includes('buzzerManualNoScaleBeep') ||
    !ui.includes('alertOutputChannel') ||
    !ui.includes('buzzerSupported') ||
    !ui.includes('Output channel') ||
    !ui.includes('scale_priority') ||
    !ui.includes('Buzzer only') ||
    !ui.includes('class="fieldHint"')) {
  throw new Error('Web UI must show paddle state, scale beep options, and buzzer alerts');
}
if (!ui.includes('id="operationalWallS" type="number" min="5" max="60"') ||
    !ui.includes('hard-caps at 60 s') ||
    !ui.includes('sToMs(') ||
    !ui.includes('rinseGestureMs:sToMs') ||
    !network.includes('CN9 limit must be from 5 to 60 s.')) {
  throw new Error('CN9 operational limit must be capped at 60 s in the UI and API messages');
}
if (!ui.includes('function rangeCheck(') ||
    !ui.includes('function showFieldError(') ||
    !ui.includes('samples × sample gap') ||
    !ui.includes('aria-live') ||
    !ui.includes('fieldError') ||
    !ui.includes('id="goalWeightG" type="number" min="10" max="200" step="1"') ||
    !ui.includes('validateNetworkClient') ||
    !ui.includes('validateApClient') ||
    !network.includes('Max recovery must be from 10 to 200 g.') ||
    !network.includes('Fast guard requires max recovery') ||
    !network.includes('SSID must be 1–32 characters.') ||
    !network.includes('Current password is incorrect.') ||
    !network.includes('configValidationErrorName(error)')) {
  throw new Error('UI/API must expose specific validation ranges, inline errors, and field-aware config errors');
}
if (!network.includes('"firstDropBeep"') ||
    !network.includes('"paddleReturnReminderBeep"') ||
    !network.includes('"buzzerScaleLostBeep"') ||
    !network.includes('"buzzerAutoToManualGuardEndBeep"') ||
    !network.includes('"buzzerManualNoScaleBeep"') ||
    !network.includes('"alertOutputChannel"') ||
    !network.includes('allowedCount > 64') ||
    !network.includes('uint64_t seen') ||
    !network.includes('WEB_UI_ASSET_TAG') ||
    !network.includes('\\"buzzerSupported\\"') ||
    !network.includes('BUZZER_SUPPORT_ENABLED') ||
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
    !ui.includes('id="currentTime"') ||
    !ui.includes('id="ntpStatus"') ||
    !ui.includes('id="ntpServerPreset"') ||
    !ui.includes('id="ntpServerCustom"') ||
    !ui.includes('id="syncTimeButton"') ||
    !ui.includes('id="autoRetare"') ||
    !ui.includes('id="fastExtractionGuardEnabled"') ||
    !ui.includes('id="maxRecoveryWeightG"') ||
    !ui.includes('id="minBrewTimeS"') ||
    !ui.includes('Fast extraction guard') ||
    !ui.includes('id="retareWindowS"') ||
    !ui.includes('id="minimumCupWeightG"') ||
    !ui.includes('id="retareStabilitySamples"') ||
    !ui.includes('id="retareStabilityToleranceG"') ||
    !ui.includes('id="retareStabilityMaxGapS"') ||
    !ui.includes('id="retareStabilityMinDurationS"') ||
    !ui.includes('BBW protection (s)') ||
    !ui.includes('id="bbwProtectionS"') ||
    !ui.includes('Paddle reminder limit (min)') ||
    !ui.includes('id="paddleReturnReminderMaxDurationMin"') ||
    !ui.includes("number('paddleReturnReminderMaxDurationMin')*60000") ||
    !ui.includes('id="sessionBar"') ||
    !ui.includes('id="logoutButton"') ||
    ui.includes('Up to 120 shots') ||
    !ui.includes('/api/v1/time/sync') ||
    !firmware.includes('session.config.firstDropBeep') ||
    !firmware.includes('localBuzzer') ||
    !firmware.includes('BUZZER_SUPPORT_ENABLED') ||
    !firmware.includes('BUZZER_GPIO') ||
    !firmware.includes('servicePaddleReturnReminder')) {
  throw new Error('Scale beep settings must be configurable end-to-end');
}
if (!domain.includes('BUZZER_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_BUZZER != 0') ||
    !domain.includes('BUZZER_ACTIVE_DRIVE = SHOT_STOPPER_ENABLE_BUZZER == 2') ||
    !domain.includes('SHOT_STOPPER_ENABLE_BUZZER must be 0, 1, or 2') ||
    !buzzer.includes('BUZZER_ACTIVE_DRIVE') ||
    !buzzer.includes('ledcAttach') ||
    !buzzer.includes('digitalWrite(pin, HIGH)') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)')) {
  throw new Error('Local buzzer must support compile-time passive (1) and active (2) drives');
}
if (!ui.includes('authenticatedOnly') ||
    !ui.includes('sessionStorage.setItem') || !ui.includes('window.location.reload()') ||
    !ui.includes('pageNav authenticatedOnly') ||
    !ui.includes('function knownPath(') ||
    !ui.includes("authenticated()&&known") ||
    !ui.includes('class="brand"') ||
    !ui.includes('class="brandLogo"') ||
    !ui.includes('src="/logo.svg?v=__FW_VERSION__"') ||
    !ui.includes('>Micra Shot Stopper</a>') ||
    !ui.includes('href="/" data-route="/"') ||
    !ui.includes("querySelectorAll('a[data-route]')") ||
    !network.includes('Status intentionally has no authentication requirement') ||
    !network.includes('HTTPD_404_NOT_FOUND') ||
    !network.includes('notFoundHandler')) {
  throw new Error('Web UI must expose a public read-only Home, hide other tabs until sign-in, and redirect unknown routes to /');
}
const statusSection = html.match(/<fieldset[^>]*><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statusSection || !statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric"/g) || []).length !== 22 ||
    !ui.includes("s.relayClosed?'CLOSED (ON)':'OPEN (OFF)'") ||
    !ui.includes('id="scaleWeight"') ||
    !ui.includes('id="scaleTimer"') ||
    !ui.includes('Weight (scale)') ||
    !ui.includes('Timer (scale)') ||
    !ui.includes('function formatScaleWeight(') ||
    !ui.includes('function formatScaleTimer(') ||
    !ui.includes('id="preferredScale"') ||
    !ui.includes('id="preferredScaleSettings"') ||
    !ui.includes('id="scaleMacCacheMode"') ||
    !ui.includes('id="clearPreferredScale"') ||
    !ui.includes('id="scaleMacCacheFullWarn"') ||
    !ui.includes('scaleMacCacheMode') ||
    !ui.includes('/api/v1/scale/preferred/clear') ||
    !ui.includes('function formatPreferredScale(') ||
    !network.includes('preferredScaleClearHandler') ||
    !network.includes('/api/v1/scale/preferred/clear') ||
    !network.includes('\\"timerMs\\"')) {
  throw new Error('Status must use one metric per row and homologate Paddle/CN9 OPEN/OFF and CLOSED/ON labels');
}
if (!ui.includes('id="shotPanel"') ||
    !ui.includes('id="shotBar"') ||
    !ui.includes('id="shotElapsed"') ||
    !ui.includes('id="shotFirstDrop"') ||
    !ui.includes('id="shotRetare"') ||
    !ui.includes('id="shotCurrentWeight"') ||
    !ui.includes('id="shotGoalWeight"') ||
    !ui.includes('id="shotType"') ||
    !ui.includes('id="shotScale"') ||
    !ui.includes('function updateShot(') ||
    !network.includes('elapsedMs') ||
    !network.includes('retarePerformed') ||
    !network.includes('shotType') ||
    !network.includes('scaleProtocol') ||
    !network.includes('safeScaleProtocol') ||
    !ui.includes('remoteReady&&authenticated()') ||
    !ui.includes('Remote CN9 disabled by policy') ||
    !network.includes('\\"remoteControlEnabled\\"') ||
    !network.includes('\\"lastCommand\\"') ||
    !network.includes('\\"maintenance\\"') ||
    !network.includes('\\"cycle\\"') ||
    !network.includes('flowDuringRetare') ||
    !ui.includes('updateShot(s)')) {
  throw new Error('Web UI must enforce remote policy, maintenance, durable command state, and live shot status');
}
if (!ui.includes('id="autoToManualGuardEnabled"') ||
    !ui.includes('id="autoToManualGuardLimitMode"') ||
    !ui.includes('id="autoToManualGuardBaselineS"') ||
    !ui.includes('id="scaleTimerStopExtraDelayMs"') ||
    !ui.includes('id="autoToManualGuardManualLimitS"') ||
    !ui.includes('id="autoToManualGuardTrendS"') ||
    !ui.includes('id="resetGuardSamplesButton"') ||
    html.indexOf('id="autoToManualGuardLimitMode"') >
        html.indexOf('id="autoToManualGuardManualLimitS"') ||
    html.indexOf('id="autoToManualGuardManualLimitS"') >
        html.indexOf('id="autoToManualGuardTrendS"') ||
    html.indexOf('id="autoToManualGuardTrendS"') >
        html.indexOf('id="autoToManualGuardBaselineS"') ||
    html.indexOf('id="autoToManualGuardBaselineS"') >
        html.indexOf('id="resetGuardSamplesButton"') ||
    !ui.includes('Reset A→M samples to baseline') ||
    !ui.includes('id="shotAtmGuard"') ||
    !ui.includes('A→M ·') ||
    !ui.includes('actual_weight_source') ||
    !network.includes('autoToManualGuardEnabled') ||
    !network.includes('autoToManualGuardBaselineMs') ||
    !network.includes('scaleTimerStopExtraDelayMs') ||
    !network.includes('autoToManualGuardTrendMs') ||
    !network.includes('autoToManualGuardEnforced') ||
    !network.includes('autoToManualGuardArmed') ||
    !network.includes('actualWeightSource') ||
    !network.includes('reset-guard-samples') ||
    !network.includes('AUTO_TO_MANUAL_GUARD')) {
  throw new Error('Auto-to-manual time guard must be wired in config UI, live panel, shots API, and routes');
}
if (!ui.includes('id="learnedOffsetG"') ||
    !ui.includes('id="weightOffsetBaselineG"') ||
    !ui.includes('id="resetCalibrationButton"') ||
    html.indexOf('id="learnedOffsetG"') >
        html.indexOf('id="weightOffsetBaselineG"') ||
    html.indexOf('id="weightOffsetBaselineG"') >
        html.indexOf('id="resetCalibrationButton"') ||
    !ui.includes('Reset learned stop offset to baseline') ||
    !ui.includes('Seed for Reset learned stop offset') ||
    !network.includes('weightOffsetBaselineG') ||
    !ui.includes('weightOffsetBaselineG')) {
  throw new Error('Learned stop offset baseline must be wired like A→M baseline reset');
}
if (!ui.includes('<legend>Brew</legend>') ||
    !ui.includes('<legend>Machine and scale</legend>') ||
    !ui.includes('<legend>Security and connectivity</legend>') ||
    !ui.includes('id="presetCards"') ||
    !ui.includes('id="presetNewBtn"') ||
    !ui.includes('id="presetDupBtn"') ||
    ui.includes('id="presetLoadBtn"') ||
    ui.includes('id="presetSaveBtn"') ||
    !ui.includes('id="saveBrewPresetButton"') ||
    !ui.includes('Save brew settings') ||
    !ui.includes('id="activeBrewProfileHint"') ||
    !ui.includes('Current profile: ') ||
    !ui.includes('function updateActiveBrewProfileHint(') ||
    html.indexOf('id="activeBrewProfileHint"') <
        html.indexOf('id="saveBrewPresetButton"') ||
    html.indexOf('id="saveBrewPresetButton"') <
        html.indexOf('id="resetGuardSamplesButton"') ||
    html.indexOf('id="saveBrewPresetButton"') >
        html.indexOf('<legend>Machine and scale</legend>') ||
    html.indexOf('id="saveConfigButton"') <
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="saveConfigButton"') >
        html.indexOf('<legend>Security and connectivity</legend>') ||
    !ui.includes('id="presetResetBtn"') ||
    !ui.includes('id="presetDeleteBtn"') ||
    !ui.includes('id="presetRenameDialog"') ||
    !ui.includes('id="homePresetCards"') ||
    !ui.includes('id="homeBrewByWeight"') ||
    !ui.includes('id="quickSettingsPanel"') ||
    html.indexOf('id="quickSettingsPanel"') > html.indexOf('id="shotPanel"') ||
    html.indexOf('id="homeBrewByWeight"') > html.indexOf('id="shotPanel"') ||
    !ui.includes('id="clearLastShotButton"') ||
    html.indexOf('id="shotPanel"') > html.indexOf('id="clearLastShotButton"') ||
    html.includes('id="lastCycle"') ||
    !ui.includes('function renderShotPanel(') ||
    !ui.includes("confirm:'CLEAR_LAST_SHOT'") ||
    !ui.includes('/api/v1/last-shot/clear') ||
    !network.includes('\\"lastShot\\"') ||
    !network.includes('lastShotClearHandler') ||
    !network.includes('LAST_SHOT_CLEAR_NOT_CONFIRMED') ||
    !firmware.includes('persistLastShotFromEndedCycle') ||
    !firmware.includes('clearLastShot') ||
    ui.includes('id="view-presets"') ||
    ui.includes('data-route="/presets"') ||
    ui.includes('id="presetsPageCards"') ||
    ui.includes('id="homePresetChips"') ||
    !ui.includes("action:'new'") ||
    !ui.includes("action:'duplicate'") ||
    !ui.includes("action:'rename'") ||
    !ui.includes("action:'restore_factory_values'") ||
    !ui.includes('function startRenamePreset(') ||
    !ui.includes('function updatePresetActionButtons(') ||
    !ui.includes('Discard them and switch presets') ||
    !ui.includes('saveBrewPreset') ||
    !ui.includes('/api/v1/presets') ||
    ui.includes('id="presetNameInput"') ||
    !network.includes('/api/v1/presets') ||
    !network.includes('presetsHandler') ||
    !network.includes('restore_factory_values') ||
    !network.includes('\\"presets\\"') ||
    network.includes('"/presets"') ||
    !css.includes('.presetCard') ||
    !css.includes('#homePresetCards') ||
    !css.includes('.btnGlyph') ||
    !css.includes('.btnGlyph .g') ||
    !css.includes('.btnGlyph .t')) {
  throw new Error('Brew presets CRUD UI must block factory delete, support reset, click-to-load, and unsaved switch confirm');
}
if (!ui.includes('id="hCpu"') ||
    !ui.includes('id="hUptime"') ||
    !ui.includes('id="hResetReason"') ||
    !ui.includes('id="hTemp"') ||
    !ui.includes('id="hTPeak"') ||
    !ui.includes('id="hRamT"') ||
    !ui.includes('id="hRamU"') ||
    !ui.includes('id="hRamF"') ||
    !ui.includes('function updH(') ||
    !ui.includes('updH(s.health,s.safety)') ||
    !ui.includes('h.uptimeMs') ||
    !ui.includes('resetReasonCode') ||
    !ui.includes("RR[s.resetReasonCode]") ||
    !network.includes('\\"hwmon\\"') ||
    !network.includes('cpuUsagePct') ||
    !network.includes('tempPeakC') ||
    !network.includes('ramTotalBytes') ||
    !network.includes('\\"uptimeMs\\"') ||
    !network.includes('\\"resetReasonCode\\"')) {
  throw new Error('Diagnostics must expose basic hwmon metrics in UI and status API');
}
if (!ui.includes('id="shotTable"') ||
    !ui.includes('id="exportShotsButton"') ||
    !ui.includes('id="clearShotsButton"') ||
    !ui.includes("confirm:'CLEAR_SHOT_LOG'") ||
    !ui.includes('refreshShots()') ||
    !ui.includes('formatShotTime(r)') ||
    !ui.includes('no time') ||
    !ui.includes('id="timezoneOffsetMinutes"') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (!ui.includes('id="firmwareFooter"') ||
    !ui.includes('firmwareVersion') ||
    !ui.includes('updateFirmwareFooter()') ||
    !network.includes('\\"firmwareVersion\\"') ||
    !network.includes('FW_VERSION')) {
  throw new Error('Firmware version must be exposed in status API and web footer');
}
if (!/<fieldset[^>]*><legend>Log<\/legend>/.test(html) ||
    /authenticatedOnly[^>]*><legend>Log<\/legend>/.test(html) ||
    !ui.includes('loadLog()') ||
    !ui.includes('setInterval(()=>refreshLog(),2500)') ||
    !ui.includes('id="view-log"') ||
    !ui.includes("name==='log'") ||
    !ui.includes('id="logLevelFilter"') ||
    !ui.includes('e.level') ||
    !ui.includes('value="boot"')) {
  throw new Error('Diagnostic log must remain a public view with view-scoped refresh');
}
if (!ui.includes('id="factoryResetButton"') ||
    !ui.includes("confirm('Restore all factory settings?") ||
    !ui.includes("confirm:'ERASE_ALL_SETTINGS'") ||
    !network.includes('FACTORY_RESET_NOT_CONFIRMED') ||
    !network.includes('resetPersistedSettingsToFactory(next)') ||
    !ui.includes('id="restartPanel"') ||
    html.indexOf('id="saveDateTimeButton"') > html.indexOf('id="restartPanel"') ||
    html.indexOf('id="restartPanel"') > html.indexOf('id="factoryResetButton"') ||
    html.slice(html.indexOf('id="actionsPanel"'), html.indexOf('id="view-history"'))
        .includes('restartButton')) {
  throw new Error('Factory reset must require UI and server-side confirmation');
}
if (!ui.includes('id="debugPanel"') ||
    !html.includes('id="debugPanel" class="buzzerOpt hidden"') ||
    !ui.includes('id="beepShortButton"') ||
    !ui.includes('id="beepLongButton"') ||
    !ui.includes('id="beepDoubleButton"') ||
    !ui.includes('id="beepTripleButton"') ||
    !ui.includes('/api/v1/control/buzzer') ||
    !ui.includes("['beepShortButton','short']") ||
    !ui.includes("['beepLongButton','long']") ||
    !ui.includes("['beepDoubleButton','double']") ||
    !ui.includes("['beepTripleButton','triple']") ||
    !ui.includes('function debugBuzzer(') ||
    !ui.includes('Shown with SHOT_STOPPER_ENABLE_BUZZER') ||
    html.indexOf('id="saveDateTimeButton"') > html.indexOf('id="debugPanel"') ||
    html.indexOf('id="debugPanel"') > html.indexOf('id="restartPanel"') ||
    !network.includes('buzzerHandler') ||
    !network.includes('BUZZER_UNSUPPORTED') ||
    !network.includes('WebCommandType::BUZZER_TEST') ||
    !network.includes('parseBuzzerPatternId') ||
    !firmware.includes('WebCommandType::BUZZER_TEST') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)')) {
  throw new Error('Admin debug must expose buzzer test buttons only when firmware has buzzer support');
}
if (!ui.includes('id="staIpMode"') ||
    !ui.includes('id="staStaticIp"') ||
    !ui.includes('id="staNetmask"') ||
    !ui.includes('id="staGateway"') ||
    !ui.includes('id="staDns1"') ||
    !ui.includes('function networkSavePayload(') ||
    !ui.includes("ipMode:$('staIpMode').value") ||
    !ui.includes('staticIpOpt') ||
    !ui.includes('pending confirm') ||
    !ui.includes('savedStaSsid') ||
    !ui.includes('Leave empty to keep the saved password') ||
    !ui.includes('keep=!!savedStaSsid') ||
    !ui.includes("savedStaSsid=n.wifiConfigured&&n.ssid?n.ssid:''") ||
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
if (!/<script\s+src="\/app\.js\?v=/.test(html) &&
    !html.includes('src="/app.js?v=__FW_VERSION__"')) {
  throw new Error('Web UI must load same-origin /app.js with a firmware version query');
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
  ['GET /app.js', 'jsHandler'],
  ['GET /app.css', 'cssHandler'],
  ['GET /logo.svg', 'logoHandler'],
  ['POST /api/v1/login', 'loginHandler'],
  ['POST /api/v1/logout', 'logoutHandler'],
  ['POST /api/v1/heartbeat', 'heartbeatHandler'],
  ['GET /api/v1/status', 'statusHandler'],
  ['GET /api/v1/log', 'logHandler'],
  ['POST /api/v1/config', 'configHandler'],
  ['POST /api/v1/scale/preferred/clear', 'preferredScaleClearHandler'],
  ['POST /api/v1/presets', 'presetsHandler'],
  ['POST /api/v1/calibration/reset', 'resetCalibrationHandler'],
  ['POST /api/v1/calibration/reset-guard-samples', 'resetGuardSamplesHandler'],
  ['POST /api/v1/control/paddle', 'paddleHandler'],
  ['POST /api/v1/control/rinse', 'rinseHandler'],
  ['POST /api/v1/control/stop', 'stopHandler'],
  ['POST /api/v1/control/restart', 'restartHandler'],
  ['POST /api/v1/control/buzzer', 'buzzerHandler'],
  ['POST /api/v1/factory-reset', 'factoryResetHandler'],
  ['GET /api/v1/shots', 'shotsHandler'],
  ['POST /api/v1/shots/clear', 'shotsClearHandler'],
  ['POST /api/v1/shots/delete', 'shotsDeleteHandler'],
  ['POST /api/v1/last-shot/clear', 'lastShotClearHandler'],
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
if (!ui.includes('function withPollGate(') ||
    !ui.includes('noteReachFail(') ||
    !ui.includes('function startView(') ||
    !ui.includes('function stopViewPolls(') ||
    !ui.includes('function renderRoute(') ||
    !ui.includes('setInterval(()=>refreshStatus(),2500)')) {
  throw new Error('Web UI must serialize view-scoped polls and soft-fail unreachable bursts');
}
if (!ui.includes('async function loadStatus(){') ||
    !ui.includes('async function loadShots(){') ||
    !ui.includes('async function loadLog(){') ||
    !ui.includes('function refreshStatus(){return withPollGate(loadStatus)}') ||
    !ui.includes('function refreshShots(){return withPollGate(loadShots)}') ||
    !ui.includes('function refreshLog(){return withPollGate(loadLog)}') ||
    !ui.includes("name==='home'||name==='settings'||name==='admin'") ||
    ui.includes("name==='presets'") ||
    !ui.includes("name==='history'") ||
    !ui.includes('renderRoute(location.pathname)') ||
    ui.includes('Promise.all([loadShots(),loadLog()])')) {
  throw new Error('Web UI must lazy-load status/shots/log per active SPA view; background polls stay gated');
}
if (!ui.includes('id="view-home"') ||
    !ui.includes('id="view-history"') ||
    !ui.includes('id="view-settings"') ||
    ui.includes('id="view-presets"') ||
    !ui.includes('id="view-admin"') ||
    !ui.includes('data-route="/settings"') ||
    ui.includes('data-route="/presets"') ||
    !ui.includes('data-route="/admin"') ||
    !ui.includes('history.pushState')) {
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
  if (uri !== '/' && !ui.includes(uri.split('?')[0])) {
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
    ui.includes('passwordChangeRequired') ||
    ui.includes('factory AP/UI password') ||
    ui.includes('Change the factory AP/UI password')) {
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
if (!firmware.includes('emitAlert(AlertEvent::FIRST_DROP') ||
    !firmware.includes('requestScaleBrewBeep(') ||
    !firmware.includes('cancelScaleBrewBeep(session.id)') ||
    !firmware.includes('onFirstDropsDetected') ||
    !firmware.includes('notifyRetareFlowDetected') ||
    !firmware.includes('retareFlowFirstDetectedAtMs') ||
    !firmware.includes('bbwProtectionActive') ||
    !firmware.includes('retareWindowOpen') ||
    !firmware.includes('scale.supportsTareStartTimer()') ||
    !firmware.includes('alertOutputChannel') ||
    !firmware.includes('emitCommandAlert') ||
    /enum class ScaleCommandType[\s\S]*BEEP/.test(
      firmware.slice(firmware.indexOf('enum class ScaleCommandType'),
                     firmware.indexOf('enum class ScaleEventType')))) {
  throw new Error('Best-effort beep must stay outside the critical BLE command queue');
}

(async () => {
const generated = await webUi.generate();
if (!generated.assetTag || !generated.cacheVersion ||
    !generated.html.includes(`v=${generated.cacheVersion}`) ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssetsGzip.h'), 'utf8')
         .includes(`WEB_UI_ASSET_TAG[] = "${generated.assetTag}"`)) {
  throw new Error('Web UI cache-buster must embed FW version + asset content tag');
}
const roundTrip = zlib.gunzipSync(generated.gzip).toString('utf8');
if (roundTrip !== generated.html) {
  throw new Error('Generated gzip Web UI does not round-trip to the minified HTML');
}
const jsRoundTrip = zlib.gunzipSync(generated.jsGzip).toString('utf8');
if (jsRoundTrip !== generated.js) {
  throw new Error('Generated gzip Web JS does not round-trip to the minified JS');
}
new Function(generated.js);
const cssRoundTrip = zlib.gunzipSync(generated.cssGzip).toString('utf8');
if (cssRoundTrip !== generated.css) {
  throw new Error('Generated gzip Web CSS does not round-trip to the minified CSS');
}
const logoRoundTrip = zlib.gunzipSync(generated.logoGzip).toString('utf8');
if (logoRoundTrip !== generated.logo) {
  throw new Error('Generated gzip Web logo does not round-trip to the minified SVG');
}
if (generated.gzip.length > 8192) {
  throw new Error('Compressed Web UI HTML exceeds the 8 KiB gzip budget');
}
if (generated.jsGzip.length > 16384) {
  throw new Error('Compressed Web UI JS exceeds the 16 KiB gzip budget');
}
if (generated.cssGzip.length > 6144) {
  throw new Error('Compressed Web CSS exceeds the 6 KiB gzip budget');
}
if (generated.logoGzip.length > 4096) {
  throw new Error('Compressed Web logo exceeds the 4 KiB gzip budget');
}
if (generated.gzip.length + generated.jsGzip.length + generated.cssGzip.length +
        generated.logoGzip.length >
    28672) {
  throw new Error('Combined HTML+JS+CSS+logo gzip exceeds the 28 KiB flash budget');
}
if (!network.includes('#include "ShotStopperWebAssetsGzip.h"') ||
    network.includes('#include "ShotStopperWebAssets.h"')) {
  throw new Error('Firmware must embed the gzip Web UI, not the HTML source string');
}
if (!network.includes('SHOT_STOPPER_WEB_UI_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_UI_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_LOGO_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_LOGO_GZIP_LEN') ||
    !network.includes('"Content-Encoding"') ||
    !network.includes('"gzip"')) {
  throw new Error('GET /, GET /app.js, GET /app.css, and GET /logo.svg must send precompressed gzip bodies');
}
if (network.includes('zlib.h') || network.includes('miniz.h') ||
    /mz_compress|deflateInit|gzipCompress/.test(network)) {
  throw new Error('Firmware must not compress the Web UI at runtime');
}
if (!network.includes('If-None-Match')) {
  throw new Error('GET / must honor If-None-Match for cached Web UI revalidation');
}
const rootHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::rootHandler');
const jsHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::jsHandler');
const cssHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::cssHandler');
const logoHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::logoHandler');
const notFoundHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::notFoundHandler');
const loginHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::loginHandler');
if (rootHandlerStart < 0 || jsHandlerStart < 0 || cssHandlerStart < 0 ||
    logoHandlerStart < 0 || notFoundHandlerStart < 0 || loginHandlerStart < 0 ||
    !(rootHandlerStart < jsHandlerStart && jsHandlerStart < cssHandlerStart &&
      cssHandlerStart < logoHandlerStart &&
      logoHandlerStart < notFoundHandlerStart &&
      notFoundHandlerStart < loginHandlerStart)) {
  throw new Error('rootHandler/jsHandler/cssHandler/logoHandler/notFoundHandler order not found');
}
const rootHandler = network.slice(rootHandlerStart, jsHandlerStart);
const jsHandler = network.slice(jsHandlerStart, cssHandlerStart);
const cssHandler = network.slice(cssHandlerStart, logoHandlerStart);
const logoHandler = network.slice(logoHandlerStart, notFoundHandlerStart);
const notFoundHandler = network.slice(notFoundHandlerStart, loginHandlerStart);
if (rootHandler.includes('no-store') || !rootHandler.includes('no-cache') ||
    !rootHandler.includes('STATUS_NOT_MODIFIED') ||
    !rootHandler.includes('ifNoneMatchEquals') ||
    !rootHandler.includes('ETag') ||
    !rootHandler.includes("style-src 'self'") ||
    !rootHandler.includes("script-src 'self'") ||
    rootHandler.includes("script-src 'unsafe-inline'") ||
    rootHandler.includes('HTTPD_RESP_USE_STRLEN')) {
  throw new Error('GET / must revalidate with ETag/304, CSP script/style self, and gzip by length');
}
if (jsHandler.includes('no-store') ||
    !jsHandler.includes('max-age=31536000') ||
    !jsHandler.includes('immutable') ||
    !jsHandler.includes('STATUS_NOT_MODIFIED') ||
    !jsHandler.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !jsHandler.includes('application/javascript')) {
  throw new Error('GET /app.js must serve immutable gzip JS with ETag/304');
}
if (cssHandler.includes('no-store') ||
    !cssHandler.includes('max-age=31536000') ||
    !cssHandler.includes('immutable') ||
    !cssHandler.includes('STATUS_NOT_MODIFIED') ||
    !cssHandler.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !cssHandler.includes('text/css')) {
  throw new Error('GET /app.css must serve immutable gzip CSS with ETag/304');
}
if (logoHandler.includes('no-store') ||
    !logoHandler.includes('max-age=31536000') ||
    !logoHandler.includes('immutable') ||
    !logoHandler.includes('STATUS_NOT_MODIFIED') ||
    !logoHandler.includes('SHOT_STOPPER_WEB_LOGO_GZIP') ||
    !logoHandler.includes('image/svg+xml')) {
  throw new Error('GET /logo.svg must serve immutable gzip SVG with ETag/304');
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
  `Embedded Web UI: JavaScript valid, ${htmlBytes} bytes HTML / ${jsBytes} bytes JS source, ` +
  `${generated.gzip.length} bytes HTML gzip, ${generated.jsGzip.length} bytes JS gzip, ` +
  `${generated.cssGzip.length} bytes CSS gzip, ${generated.logoGzip.length} bytes logo gzip, ` +
  `${expected.size} routes checked`
);
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
