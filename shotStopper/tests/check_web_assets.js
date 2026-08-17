'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const webUi = require('../../scripts/gen_web_ui.js');

const sketchDir = path.resolve(__dirname, '..');
const asset = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssets.h'), 'utf8');
const network = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
const networkHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.h'), 'utf8');
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
const ui = html + '\n' + js;
if (/<details\b[^>]*\bopen\b/i.test(html)) {
  throw new Error('All collapsible <details> groups must start collapsed (no open attribute)');
}
if (css.includes('.brandLogo') || html.includes('logo.svg') || html.includes('brandLogo')) {
  throw new Error('Web UI must not embed a logo asset or .brandLogo styles');
}
if (!css.includes('.brand') || !css.includes('inline-flex')) {
  throw new Error('Brand heading styles must remain for text-only branding');
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
if (jsBytes > 81920) {
  throw new Error('Web UI JS source exceeds the 80 KiB authoring budget');
}
if (htmlBytes + jsBytes > 116736) {
  throw new Error('Web UI HTML+JS source exceeds the 114 KiB combined authoring budget');
}
if (!/lang="en"/.test(html) || !ui.includes('role="switch"') ||
    !ui.includes('Paddle State') || !ui.includes('firstDropBeep') ||
    !ui.includes('paddleReturnReminderBeep') ||
    !ui.includes('buzzerScaleLostBeep') ||
    !ui.includes('buzzerAutoToManualGuardEndBeep') ||
    !ui.includes('buzzerManualNoScaleBeep') ||
    !ui.includes('buzzerScaleConnectedBeep') ||
    !ui.includes('buzzerExtendedPulseRate') ||
    !ui.includes('buzzerSlowExtendedPulseRate') ||
    !html.includes('id="buzzerExtendedPulseRate"') ||
    !html.includes('id="buzzerSlowExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Extended shot pulse<select id="buzzerExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Slow extended pulse<select id="buzzerSlowExtendedPulseRate"') ||
    !html.includes('> Scale lost<small class="fieldHint">Echo inverted when the scale disconnects') ||
    html.includes('Scale lost (BBW)') ||
    !html.includes('option value="fast" selected') ||
    !html.includes('option value="rapid">Rapid') ||
    html.includes('id="buzzerExtendedPulseBeep"') ||
    html.includes('20ms') ||
    html.includes('x segundo') ||
    ui.includes('querySelectorAll(\'.scaleIncapableOpt\').forEach(e=>{e.classList.toggle(\'fieldOff\',scaleOnly);e.querySelectorAll(\'input\').forEach') ||
    !ui.includes('alertOutputChannel') ||
    !ui.includes('buzzerSupported') ||
    !ui.includes('Output channel') ||
    !ui.includes('scale_priority') ||
    !ui.includes('Buzzer only') ||
    !ui.includes('class="fieldHint"') ||
    !ui.includes('id="bookooMuteOnBuzzerOnly"') ||
    !ui.includes('id="bookooConnectBeepLevel"') ||
    !html.includes('id="bookooMuteOnBuzzerOnly" type="checkbox" checked') ||
    !html.includes('id="buzzerScaleConnectedBeep" type="checkbox" checked') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt"><input id="buzzerScaleConnectedBeep"') ||
    html.includes('buzzerOnlyOpt') ||
    !html.includes('option value="4" selected') ||
    !ui.includes('when Buzzer only is saved') ||
    !html.includes('Volume on connect/reconnect. <strong>Scale only or Scale priority.</strong>') ||
    !html.includes('<strong>Requires automatic tare.</strong>') ||
    !html.includes('when this option is enabled. <strong>Buzzer only.</strong>')) {
  throw new Error('Web UI must show paddle state, scale beep options, and buzzer alerts');
}
if (!ui.includes('id="operationalWallS" type="number" min="5" max="60"') ||
    !ui.includes('Max BBW time (s)') ||
    !ui.includes('hard-caps at 60 s') ||
    !ui.includes('sToMs(') ||
    !ui.includes('rinseGestureMs:sToMs') ||
    !network.includes('Max BBW time must be from 5 to 60 s.')) {
  throw new Error('Max BBW time must be capped at 60 s in the UI and API messages');
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
    !network.includes('configValidationErrorName(error)')) {
  throw new Error('UI/API must expose specific validation ranges, inline errors, and field-aware config errors');
}
if (!network.includes('"firstDropBeep"') ||
    !network.includes('"soundAlertsEnabled"') ||
    !network.includes('"paddleReturnReminderBeep"') ||
    !network.includes('"buzzerScaleLostBeep"') ||
    !network.includes('"buzzerAutoToManualGuardEndBeep"') ||
    !network.includes('"buzzerManualNoScaleBeep"') ||
    !network.includes('"buzzerScaleConnectedBeep"') ||
    !network.includes('"buzzerExtendedPulseRate"') ||
    !network.includes('"buzzerSlowExtendedPulseRate"') ||
    !network.includes('"alertOutputChannel"') ||
    !network.includes('"bookooMuteOnBuzzerOnly"') ||
    !network.includes('"bookooConnectBeepLevel"') ||
    !network.includes('"baseRevision"') ||
    !network.includes('jsonFieldPresent') ||
    !network.includes('CONFIG_REVISION_STALE') ||
    !network.includes('settingFieldCount') ||
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
    !network.includes('"slowExtractionGuardEnabled"') ||
    !network.includes('"minRecoveryWeightG"') ||
    !network.includes('"maxBrewTimeMs"') ||
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
    !ui.includes('id="slowExtractionGuardEnabled"') ||
    !ui.includes('id="minRecoveryWeightG"') ||
    !ui.includes('id="maxBrewTimeS"') ||
    !ui.includes('Slow extraction guard') ||
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
    !firmware.includes('candidate.buzzerScaleConnectedBeep') ||
    !firmware.includes('candidate.buzzerSlowExtendedPulseRate') ||
    !firmware.includes('localBuzzer') ||
    !firmware.includes('BUZZER_SUPPORT_ENABLED') ||
    !firmware.includes('BUZZER_GPIO') ||
    !firmware.includes('servicePaddleReturnReminder')) {
  throw new Error('Scale beep settings must be configurable end-to-end');
}

if (!ui.includes('id="soundAlertsEnabled"') ||
    !ui.includes('id="homeSoundAlertsEnabled"') ||
    !ui.includes('soundAlertsEnabled:$(\'soundAlertsEnabled\').checked') ||
    !ui.includes('p.soundAlertsEnabled=$(\'homeSoundAlertsEnabled\').checked') ||
    !ui.includes("k!=='soundAlertsEnabled'") ||
    !ui.includes("typeof c.soundAlertsEnabled==='boolean'")) {
  throw new Error('Sound alerts must be mirrored by Settings and Home patch controls');
}

if (!ui.includes('bleCompanionEnabled') ||
    !ui.includes('/api/v1/admin/ble-compat') ||
    !ui.includes("method:'PUT'") ||
    !ui.includes('active this boot') ||
    !ui.includes('restart required') ||
    !network.includes('bleCompanion') ||
    !network.includes('restartRequired') ||
    !network.includes('WebCommandType::BLE_COMPAT_ENABLE') ||
    !networkHeader.includes('bleCompatHandler')) {
  throw new Error('BLE Companion Admin controls must be wired end-to-end');
}
if (!domain.includes('BUZZER_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_BUZZER != 0') ||
    !domain.includes('BUZZER_ACTIVE_DRIVE = SHOT_STOPPER_ENABLE_BUZZER == 2') ||
    !domain.includes('SHOT_STOPPER_ENABLE_BUZZER must be 0, 1, or 2') ||
    !domain.includes('DEFAULT_ALERT_OUTPUT_CHANNEL') ||
    !domain.includes(
        'BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY') ||
    !domain.includes('static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL)') ||
    !buzzer.includes('BUZZER_ACTIVE_DRIVE') ||
    !buzzer.includes('ledcAttach') ||
    !buzzer.includes('digitalWrite(pin, HIGH)') ||
    !buzzer.includes('BUZZER_ECHO_INVERTED_NOTES') ||
    !domain.includes('ECHO_INVERTED') ||
    !firmware.includes('startExtendedPulseTrain') ||
    !firmware.includes('startPulseTrain') ||
    !firmware.includes('stopPulseTrains') ||
    !firmware.includes('serviceExtendedPulseAlert') ||
    !firmware.includes('buzzerPatternForExtendedPulseRate') ||
    !domain.includes('DEFAULT_EXTENDED_PULSE_RATE') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)')) {
  throw new Error('Local buzzer must support compile-time passive (1) and active (2) drives');
}
if (!ui.includes('authenticatedOnly') ||
    !ui.includes("s.setItem('shotStopperToken'") || !ui.includes('window.location.reload()') ||
    !ui.includes('pageNav authenticatedOnly') ||
    !ui.includes('function knownPath(') ||
    !ui.includes("authenticated()&&known") ||
    !ui.includes('class="brand"') ||
    !ui.includes('>Micra Shot Stopper</a>') ||
    !ui.includes('href="/" data-route="/"') ||
    !ui.includes("querySelectorAll('a[data-route]')") ||
    !network.includes('Status intentionally has no authentication requirement') ||
    !network.includes('HTTPD_404_NOT_FOUND') ||
    !network.includes('notFoundHandler')) {
  throw new Error('Web UI must expose a public read-only Home, hide other tabs until sign-in, and redirect unknown routes to /');
}
if (!html.includes('id="rememberMe"') ||
    !/id="rememberMe"[^>]*\bchecked\b/.test(html) ||
    html.includes('Stay signed in for 7 days on this browser.') ||
    !js.includes('rememberMe:r') ||
    !js.includes('r?localStorage:sessionStorage') ||
    !js.includes("s.setItem('shotStopperToken'") ||
    !js.includes('function clearAuth()') ||
    !js.includes("s.removeItem('shotStopperToken')") ||
    !js.includes("localStorage.getItem(k)") ||
    !js.includes("sessionStorage.getItem(k)") ||
    !networkHeader.includes(
        'SESSION_REMEMBER_MS = 7UL * 24UL * 60UL * 60UL * 1000UL') ||
    !network.includes('SESSION_REMEMBER_MS') ||
    !network.includes('jsonBoolean(root, "rememberMe", rememberMe)') ||
    !network.includes('createSession(token, csrf, rememberMe)') ||
    !network.includes('session.rememberMe') ||
    !network.includes('session.createdAtMs') ||
    !network.includes('uiAuthenticated')) {
  throw new Error(
      'Remember me must persist a 7-day session without 3-minute idle expiry');
}
const statusSection = html.match(/<fieldset[^>]*><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statusSection || !statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric"/g) || []).length !== 13 ||
    !(ui.includes("s.relayClosed?'CLOSED (ON)':'OPEN (OFF)'") || ui.includes("onOff(s.relayClosed,'CLOSED (ON)','OPEN (OFF)')")) ||
    !ui.includes('id="scaleWeight"') ||
    !ui.includes('id="scaleTimer"') ||
    !ui.includes('Weight (scale)') ||
    !ui.includes('Timer (scale)') ||
    !statusSection[1].includes('id="statusExtractionGuard"') ||
    !statusSection[1].includes('id="statusSlowExtractionGuard"') ||
    !statusSection[1].includes('id="statusAtmGuard"') ||
    !statusSection[1].includes('id="statusNoScaleGuard"') ||
    !ui.includes('function formatScaleWeight(') ||
    !ui.includes('function formatScaleLink(') ||
    !ui.includes('function formatScaleTimer(') ||
    !ui.includes("BLE up (no weight)") ||
    !ui.includes('formatScaleLink(s)') ||
    !ui.includes('id="preferredScale"') ||
    !ui.includes('id="preferredScaleSelect"') ||
    !ui.includes('id="preferredScalePauseHint"') ||
    !ui.includes('id="alwaysUseThisScale"') ||
    !ui.includes('id="forgetPairedScale"') ||
    !ui.includes('Always use this scale') ||
    !ui.includes('Preferred scale') ||
    !ui.includes('Clear preferred') ||
    !ui.includes('scaleMacCacheMode') ||
    !ui.includes('/api/v1/scale/preferred/clear') ||
    !ui.includes('/api/v1/scale/preferred/select') ||
    !ui.includes('function formatPreferredScale(') ||
    !ui.includes('function updatePreferredScaleSelect(') ||
    !ui.includes('function selectPreferredScale(') ||
    !ui.includes('function forgetPairedScale(') ||
    !ui.includes('Saved scale history is kept') ||
    !ui.includes(' (not locked)') ||
    !ui.includes('macCachePauseRemainingMs>0') ||
    ui.includes('id="preferredScaleSettings"') ||
    ui.includes('id="scaleMacCacheMode"') ||
    ui.includes('id="clearPreferredScale"') ||
    ui.includes('id="scaleMacCacheFullWarn"') ||
    ui.includes('Use scale MAC cache') ||
    ui.includes('Paired scale') ||
    ui.includes('Forget this scale') ||
    !network.includes('preferredScaleClearHandler') ||
    !network.includes('preferredScaleSelectHandler') ||
    !network.includes('/api/v1/scale/preferred/clear') ||
    !network.includes('/api/v1/scale/preferred/select') ||
    !network.includes('Always use this scale must be on or off.') ||
    !network.includes('scaleMacCacheMode must be disabled or full.') ||
    !network.includes('The paired scale cannot be forgotten while a cycle') ||
    !network.includes('\\"history\\"') ||
    network.includes('Preferred scale cache cannot be cleared') ||
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
    !network.includes('firstDropElapsedMs') ||
    !network.includes('retarePerformed') ||
    !network.includes('shotType') ||
    !network.includes('scaleProtocol') ||
    !network.includes('safeScaleProtocol') ||
    !ui.includes('remoteReady&&authenticated()') ||
    !ui.includes('Remote CN9 disabled by policy') ||
    !network.includes('\\"remoteControlEnabled\\"') ||
    !network.includes('\\"lastCommand\\"') ||
    !network.includes('\\"maintenance\\"') ||
    !network.includes('\\"persistPending\\"') ||
    !network.includes('\\"persistFailed\\"') ||
    !ui.includes('persistFailed') ||
    !ui.includes('Saving...') ||
    !network.includes('\\"cycle\\"') ||
    !network.includes('extractionExtended') ||
    !ui.includes('updateShot(s)')) {
  throw new Error('Web UI must enforce remote policy, maintenance, durable command state, and live shot status');
}
if (!ui.includes('id="autoToManualGuardEnabled"') ||
    !ui.includes('id="autoToManualGuardLimitMode"') ||
    !ui.includes('id="autoToManualGuardBaselineS"') ||
    !ui.includes('id="scaleTimerStopExtraDelayMs"') ||
    !ui.includes('id="dripDelayS" type="number" min="0" max="10" step="0.1"') ||
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
    !ui.includes('id="shotNoScaleGuard"') ||
    !ui.includes('id="statusExtractionGuard"') ||
    !ui.includes('id="statusSlowExtractionGuard"') ||
    !ui.includes('id="statusAtmGuard"') ||
    !ui.includes('id="statusNoScaleGuard"') ||
    !ui.includes('No-scale guard') ||
    !ui.includes('id="avoidBbwShotWithoutScale"') ||
    !ui.includes('id="lastShotCooldownMin"') ||
    !ui.includes('Avoid BBW shot without scale') ||
    !ui.includes('Last shot cooldown') ||
    !ui.includes('function formatNoScaleGuard(') ||
    !ui.includes('function formatSlowExtractionGuard(') ||
    !ui.includes('d.slowExtended?formatSlowExtractionGuard(') ||
    !ui.includes('function updateStatusGuards(') ||
    ui.includes('function updateNoScaleGuard(') ||
    html.indexOf('id="shotNoScaleGuard"') <
        html.indexOf('id="shotAtmGuard"') ||
    html.indexOf('id="scaleTimer"') >
        html.indexOf('id="statusExtractionGuard"') ||
    html.indexOf('id="statusExtractionGuard"') >
        html.indexOf('id="statusSlowExtractionGuard"') ||
    html.indexOf('id="statusSlowExtractionGuard"') >
        html.indexOf('id="statusAtmGuard"') ||
    html.indexOf('id="statusAtmGuard"') >
        html.indexOf('id="statusNoScaleGuard"') ||
    html.indexOf('id="shotNoScaleGuard"') >
        html.indexOf('id="statusExtractionGuard"') ||
    html.indexOf('<legend>Machine and scale</legend>') >
        html.indexOf('<summary>Paddle</summary>') ||
    html.indexOf('<summary>Paddle</summary>') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('<summary>No-scale BBW</summary>') >
        html.indexOf('<summary>Quick rinse</summary>') ||
    html.indexOf('id="avoidBbwShotWithoutScale"') <
        html.indexOf('<legend>Machine and scale</legend>') ||
    html.indexOf('id="avoidBbwShotWithoutScale"') >
        html.indexOf('id="lastShotCooldownMin"') ||
    html.indexOf('id="lastShotCooldownMin"') <
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="lastShotCooldownMin"') >
        html.indexOf('<summary>Quick rinse</summary>') ||
    html.indexOf('id="paddleMode"') <
        html.indexOf('<summary>Paddle</summary>') ||
    html.indexOf('id="paddleMode"') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="avoidBbwShotWithoutScale"') <
        html.indexOf('id="saveBrewPresetButton"') ||
    !network.includes('avoidBbwShotWithoutScale') ||
    !network.includes('lastShotCooldownMs') ||
    !network.includes('serialDebugOutput') ||
    !network.includes('ringRetainLogLevel') ||
    !network.includes('noScaleShotGuard') ||
    !network.includes('noScaleShotGuardEnabled') ||
    !network.includes('noScaleShotGuardArmed') ||
    !firmware.includes('last.noScaleShotGuardEnabled') ||
    !firmware.includes('last.noScaleShotGuardArmed') ||
    !ui.includes('A→M ·') ||
    !ui.includes('function updateHomeGuardSubs(') ||
    !ui.includes('updateHomeGuardSubs(s,live)') ||
    !ui.includes("setHomeSub('homeBbwSub'") ||
    !css.includes('.swS') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('.homeGuardGrid .swL{padding-right:3ch}') ||
    !css.includes('grid-template-columns:subgrid') ||
    !css.includes('#brewModeRow{width:auto') ||
    !ui.includes('actual_weight_source') ||
    !network.includes('autoToManualGuardEnabled') ||
    !network.includes('autoToManualGuardBaselineMs') ||
    !network.includes('scaleTimerStopExtraDelayMs') ||
    !network.includes('dripDelayMs') ||
    !network.includes('autoToManualGuardTrendMs') ||
    !network.includes('autoToManualGuardEnforced') ||
    !network.includes('autoToManualGuardArmed') ||
    !network.includes('actualWeightSource') ||
    !network.includes('reset-guard-samples') ||
    !network.includes('AUTO_TO_MANUAL_GUARD')) {
  throw new Error('Auto-to-manual time guard must be wired in config UI, live panel, shots API, and routes');
}
if (!html.includes('<summary>Paddle</summary>') ||
    !html.includes('id="paddleMode"') ||
    !html.includes('<option value="natural">Natural</option>') ||
    !html.includes('<option value="original">Original</option>') ||
    !html.includes('<strong>Natural:</strong>') ||
    !html.includes('<strong>Original:</strong>') ||
    !html.includes('like a normal brew switch') ||
    !html.includes('original Tater Mazer Shot Stopper') ||
    !html.includes('move the paddle ON during the shot') ||
    !html.includes('Do not press the scale') ||
    !ui.includes("paddleMode:['natural','original']") ||
    !ui.includes("if($('paddleMode'))$('paddleMode').value=") ||
    !network.includes('"paddleMode"') ||
    !network.includes('paddleMode must be natural or original.') ||
    !network.includes('jsonPaddleMode') ||
    !firmware.includes('candidate.paddleMode = command.config.paddleMode') ||
    !domain.includes('enum class PaddleMode') ||
    !domain.includes('NATURAL = 0') ||
    !domain.includes('ORIGINAL = 1')) {
  throw new Error('Machine Paddle mode must expose Natural/Original in UI, API, and APPLY_CONFIG');
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
if (!html.includes('<summary>Tare</summary>') ||
    !html.includes('<summary>Scales</summary>') ||
    html.includes('<summary>Scale & retare</summary>') ||
    html.indexOf('<summary>Tare</summary>') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Scales</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="autoTare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="autoRetare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareWindowS"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="minimumCupWeightG"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilitySamples"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityToleranceG"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityMaxGapS"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityMinDurationS"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="scaleTimerStopExtraDelayMs"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="dripDelayS"') <
        html.indexOf('id="scaleTimerStopExtraDelayMs"') ||
    html.indexOf('id="alwaysUseThisScale"') <
        html.indexOf('id="dripDelayS"') ||
    html.indexOf('id="alwaysUseThisScale"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="preferredScaleSelect"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="forgetPairedScale"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Bookoo</summary>') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="canTareStartTimer"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="canTareStartTimer"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('id="bookooMuteOnBuzzerOnly"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="bookooMuteOnBuzzerOnly"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('id="bookooConnectBeepLevel"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="bookooConnectBeepLevel"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<strong>Requires automatic tare.</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<strong>Requires automatic tare.</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('when this option is enabled. <strong>Buzzer only.</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('when this option is enabled. <strong>Buzzer only.</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<strong>Scale only or Scale priority.</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<strong>Scale only or Scale priority.</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<summary>Acaia</summary>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<summary>Felicita</summary>') <
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<summary>Felicita</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    !ui.includes("d.parentElement.closest('details')")) {
  throw new Error('Machine settings must split Tare and Scales, with Bookoo/Acaia/Felicita subgroups');
}
if (!ui.includes("rangeCheck('dripDelayS',0,10,'Drip delay',{unit:'s'})") ||
    !ui.includes("dripDelayMs:sToMs('dripDelayS')") ||
    !ui.includes("$('dripDelayS').value=String((c.dripDelayMs??3000)/1000)") ||
    !ui.includes('Wait after the shot ends before capturing the final post-drip weight for history and offset learning.') ||
    !network.includes('\\"dripDelayMs\\":%lu') ||
    !network.includes('Drip delay must be from 0 to 10 s.') ||
    !network.includes('candidate.dripDelayMs')) {
  throw new Error('Drip delay must be wired through Settings, status/settings, and config validation');
}
if (!html.includes('<summary>AtomHeart Eclair</summary>') ||
    !html.includes('Eclair does not expose configurable volume, beep, mode, or combined tare-and-start commands.') ||
    html.indexOf('<summary>AtomHeart Eclair</summary>') <
        html.indexOf('<summary>Felicita</summary>') ||
    html.indexOf('<summary>AtomHeart Eclair</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.includes('id="eclair')) {
  throw new Error('Machine settings must include an informational AtomHeart Eclair subgroup without settings');
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
    !html.includes('class="homeSwitchGrid"') ||
    !html.includes('id="homeBbwSub"') ||
    !html.includes('id="homeNoScaleSub"') ||
    !html.includes('id="homeFastSub"') ||
    !html.includes('id="homeSlowSub"') ||
    !html.includes('id="homeAtmSub"') ||
    !html.includes('class="swS"') ||
    html.indexOf('class="homeSwitchGrid"') > html.indexOf('id="homeBrewByWeight"') ||
    html.indexOf('id="homeBrewByWeight"') > html.indexOf('id="homeAvoidBbwShotWithoutScale"') ||
    html.indexOf('id="homeAvoidBbwShotWithoutScale"') >
        html.indexOf('id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="homeAutoToManualGuardEnabled"') >
        html.indexOf('id="homeSlowExtractionGuardEnabled"') ||
    html.indexOf('id="homeSlowExtractionGuardEnabled"') >
        html.indexOf('id="homeFastExtractionGuardEnabled"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') > html.indexOf('id="homePresetBlock"') ||
    !html.includes('class="homeGuardGrid"') ||
    html.indexOf('class="homeGuardGrid"') > html.indexOf('id="homeAvoidBbwShotWithoutScale"') ||
    !ui.includes('id="homeAvoidBbwShotWithoutScale"') ||
    !ui.includes('id="homeFastExtractionGuardEnabled"') ||
    !ui.includes('id="homeSlowExtractionGuardEnabled"') ||
    !ui.includes('id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="quickSettingsPanel"') > html.indexOf('id="shotPanel"') ||
    html.indexOf('id="homeBrewByWeight"') > html.indexOf('id="homeAvoidBbwShotWithoutScale"') ||
    html.indexOf('id="homeAvoidBbwShotWithoutScale"') >
        html.indexOf('id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="homeAutoToManualGuardEnabled"') >
        html.indexOf('id="homeSlowExtractionGuardEnabled"') ||
    html.indexOf('id="homeSlowExtractionGuardEnabled"') >
        html.indexOf('id="homeFastExtractionGuardEnabled"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') >
        html.indexOf('id="homeSoundAlertsEnabled"') ||
    html.indexOf('id="homeSoundAlertsEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') > html.indexOf('id="shotPanel"') ||
    !html.includes('>No-scale BBW<span') ||
    !html.includes('>Fast extraction guard<span') ||
    !html.includes('>Slow extraction guard<span') ||
    !html.includes('>A→M time guard<span') ||
    html.indexOf('<summary>No-scale BBW</summary>') < 0 ||
    html.indexOf('<summary>Fast extraction guard</summary>') < 0 ||
    html.indexOf('<summary>Slow extraction guard</summary>') < 0 ||
    html.indexOf('<summary>A→M time guard</summary>') < 0 ||
    html.indexOf('<summary>Fast extraction guard</summary>') >
        html.indexOf('<summary>Slow extraction guard</summary>') ||
    html.indexOf('<summary>Slow extraction guard</summary>') >
        html.indexOf('<summary>A→M time guard</summary>') ||
    !ui.includes('function updateHomeGuardSwitchesLock(') ||
    !ui.includes('function persistHomeGuard(') ||
    !ui.includes('function flushHomeGuards(') ||
    !ui.includes('function scheduleHomeGuardFlush(') ||
    !ui.includes('function withCommandGate(') ||
    !ui.includes('function beginHomeSwitchPending(') ||
    !ui.includes('function applyPolledHomeSwitch(') ||
    !ui.includes('function applyHomeSwitchesFromConfig(') ||
    !ui.includes('Date.now()+5e3') ||
    !ui.includes('pollAt<p.until') ||
    !ui.includes("classList.toggle('switchPending',!!on)") ||
    !ui.includes("persistHomeGuard('homeAvoidBbwShotWithoutScale'") ||
    !ui.includes("persistHomeGuard('homeFastExtractionGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeSlowExtractionGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeAutoToManualGuardEnabled'") ||
    !ui.includes("'avoidBbwShotWithoutScale',0)") ||
    !ui.includes("'fastExtractionGuardEnabled',1)") ||
    !ui.includes("'slowExtractionGuardEnabled',1)") ||
    !ui.includes("'autoToManualGuardEnabled',1)") ||
    !ui.includes('el.disabled=!controlsMutable||off') ||
    ui.includes('el.disabled=!controlsMutable||off||pend') ||
    !ui.includes('homeFlushBusy') ||
    !ui.includes('scheduleHomeGuardFlush()') ||
    !ui.includes("classList.toggle('fieldOff',off)") ||
    !ui.includes('$(\'homeBrewByWeight\').disabled=!controlsMutable') ||
    !css.includes('.switchRow.switchPending') ||
    !css.includes('.homeSwitchGrid') ||
    !css.includes('justify-content:flex-start') ||
    !css.includes('.homeSwitchGrid{') ||
    !css.includes('border-bottom:1px solid') ||
    !css.includes('.homeSwitchGrid .switchState{display:none}') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('.homeGuardGrid .swL{padding-right:3ch}') ||
    !css.includes('grid-template-columns:subgrid') ||
    !css.includes('#brewModeRow{width:auto') ||
    !css.includes('#brewModeRow .switch{width:5.7rem') ||
    !css.includes('.ruleChartHead{') ||
    !css.includes('.ruleChartHead strong,.ruleChartMode{display:none}') ||
    !css.includes('background:#c9a227') ||
    !ui.includes("beginHomeSwitchPending('homeBrewByWeight'") ||
    !ui.includes('beginHomeSwitchPending(h,on)') ||
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
{
  const diagHtml = html.slice(html.indexOf('id="view-diagnostic"'),
                              html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) + 10);
  const adminHtml = html.slice(html.indexOf('id="view-admin"'),
                               html.indexOf('id="firmwareFooter"'));
  const statusHtml = html.slice(html.indexOf('id="statusPanel"'),
                                html.indexOf('id="actionsPanel"'));
  if (!ui.includes('id="hCpu"') ||
      !ui.includes('id="hWifi"') ||
      !ui.includes('id="hSsid"') ||
      !ui.includes('id="hAp"') ||
      !ui.includes('id="hUptime"') ||
      !ui.includes('id="hResetReason"') ||
      !ui.includes('id="hTemp"') ||
      !ui.includes('id="hTPeak"') ||
      !ui.includes('id="hRamT"') ||
      !ui.includes('id="hRamU"') ||
      !ui.includes('id="hRamF"') ||
      !ui.includes('id="hHeapMin"') ||
      !ui.includes('id="hHeapLargest"') ||
      !ui.includes('function updH(') ||
      !ui.includes('updH(s.health,s.safety)') ||
      !ui.includes('function applyDiagnosticStatus(') ||
      !ui.includes('h.uptimeMs') ||
      !ui.includes('h.minimumFreeHeapBytes') ||
      !ui.includes('h.largestFreeHeapBlockBytes') ||
      !ui.includes("toFixed(1)+' KB'") ||
      !ui.includes('resetReasonCode') ||
      !ui.includes("RR[s.resetReasonCode]") ||
      !network.includes('\\"hwmon\\"') ||
      !network.includes('cpuLoad5s') ||
      !network.includes('cpuLoad1m') ||
      !network.includes('cpuLoad5m') ||
      !network.includes('cpu0Busy') ||
      !network.includes('cpu1Busy') ||
      !network.includes('cpuLoadValid') ||
      !ui.includes('cpuLoad5s') ||
      !ui.includes('cpuLoad1m') ||
      !ui.includes('cpuLoad5m') ||
      !ui.includes('cpuLoadValid') ||
      !ui.includes('0–2 · 5s 1m 5m') ||
      !network.includes('tempPeakC') ||
      !network.includes('ramTotalBytes') ||
      !network.includes('\\"uptimeMs\\"') ||
      !network.includes('\\"minimumFreeHeapBytes\\"') ||
      !network.includes('\\"largestFreeHeapBlockBytes\\"') ||
      !network.includes('\\"resetReasonCode\\"') ||
      !diagHtml.includes('id="diagnosticsPanel"') ||
      !diagHtml.includes('<legend>Diagnostics</legend>') ||
      !diagHtml.includes('<strong>Heap min</strong>') ||
      !diagHtml.includes('<strong>Heap largest</strong>') ||
      diagHtml.includes('<details') ||
      diagHtml.includes('<summary>Diagnostics</summary>') ||
      !diagHtml.includes('id="currentTime"') ||
      !diagHtml.includes('id="ntpStatus"') ||
      diagHtml.indexOf('id="diagnosticsPanel"') > diagHtml.indexOf('id="logPanel"') ||
      diagHtml.indexOf('id="currentTime"') > diagHtml.indexOf('id="ntpStatus"') ||
      diagHtml.indexOf('id="ntpStatus"') > diagHtml.indexOf('id="hWifi"') ||
      diagHtml.indexOf('id="hRamF"') > diagHtml.indexOf('id="hHeapMin"') ||
      diagHtml.indexOf('id="hHeapMin"') > diagHtml.indexOf('id="hHeapLargest"') ||
      adminHtml.includes('id="diagnosticsPanel"') ||
      adminHtml.includes('id="currentTime"') ||
      adminHtml.includes('<summary>Diagnostics</summary>') ||
      statusHtml.includes('id="currentTime"') ||
      statusHtml.includes('id="ntpStatus"') ||
      css.includes('diagGroup')) {
    throw new Error(
        'Diagnostics must be a non-collapsible fieldset at the top of Diagnostic, above Log, with Current time/NTP first');
  }
}
if (!ui.includes('id="shotTable"') ||
    !ui.includes('id="exportShotsButton"') ||
    !ui.includes('id="clearShotsButton"') ||
    !ui.includes("confirm:'CLEAR_SHOT_LOG'") ||
    !ui.includes('refreshShots()') ||
    !ui.includes('formatShotTime(r)') ||
    !ui.includes('no time') ||
    !ui.includes('id="timezoneOffsetMinutes"') ||
    !js.includes('m+=15') ||
    js.includes('Request accepted.') ||
    !js.includes('Request queued.') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (!ui.includes('id="firmwareFooter"') ||
    !ui.includes('firmwareVersion') ||
    !ui.includes('updateFirmwareFooter()') ||
    !network.includes('\\"firmwareVersion\\"') ||
    !network.includes('\\"bootId\\":%lu') ||
    !network.includes('FW_VERSION')) {
  throw new Error('Firmware version must be exposed in status API and web footer');
}
if (!/<fieldset[^>]*><legend>Log<\/legend>/.test(html) ||
    /authenticatedOnly[^>]*><legend>Log<\/legend>/.test(html) ||
    !ui.includes('loadLog()') ||
    !ui.includes('refreshLog()') ||
    !ui.includes("name==='diagnostic'") ||
    !ui.includes('id="view-diagnostic"') ||
    !ui.includes('data-route="/diagnostic"') ||
    !html.includes('>Diagnostic</a>') ||
    !ui.includes('id="logLevelFilter"') ||
    !ui.includes('e.level') ||
    !ui.includes('value="boot"') ||
    html.indexOf('id="ringRetainLogLevel"') < html.indexOf('id="view-diagnostic"') ||
    html.indexOf('id="ringRetainLogLevel"') >
        html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) ||
    html.indexOf('id="serialDebugOutput"') < html.indexOf('id="view-diagnostic"') ||
    html.indexOf('id="serialDebugOutput"') >
        html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) ||
    html.indexOf('id="ringRetainLogLevel"') > html.indexOf('id="serialDebugOutput"') ||
    !html.includes('id="serialDebugOutput" class="mutable"') ||
    !html.includes('id="ringRetainLogLevel" class="mutable"') ||
    html.includes('id="navLogWrap"') ||
    ui.includes('function ringLogEnabled(') ||
    ui.includes('function updateLogNavVisibility(') ||
    !html.includes('<hr class="logSep">') ||
    html.indexOf('<hr class="logSep">') < html.indexOf('id="serialDebugOutput"') ||
    html.indexOf('<hr class="logSep">') > html.indexOf('id="logLevelFilter"') ||
    !css.includes('.logSep') ||
    html.includes('data-route="/debug"') ||
    html.includes('id="view-debug"') ||
    html.includes('id="view-log"')) {
  throw new Error('Diagnostic tab must host always-visible Log with ring/serial controls and separator');
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
if (html.includes('id="debugPanel"') ||
    html.includes('id="view-debug"') ||
    html.includes('data-route="/debug"') ||
    ui.includes('function debugBuzzer(') ||
    ui.includes('function debugBookoo(') ||
    ui.includes('/api/v1/control/buzzer') ||
    ui.includes('/api/v1/control/bookoo') ||
    network.includes('buzzerHandler') ||
    network.includes('bookooHandler') ||
    network.includes('/api/v1/status/debug') ||
    network.includes('"/debug"') ||
    network.includes('StatusPage::Debug')) {
  throw new Error('Debug tab/API must be removed from Web UI and network handlers');
}
if (!(ui.includes("if($('serialDebugOutput'))$('serialDebugOutput').checked=!!c.serialDebugOutput") ||
         ui.includes("$('serialDebugOutput').checked=!!c.serialDebugOutput")) ||
    !(ui.includes("if($('ringRetainLogLevel'))$('ringRetainLogLevel').value=c.ringRetainLogLevel||'none'") ||
         ui.includes("$('ringRetainLogLevel').value=c.ringRetainLogLevel||'none'")) ||
    firmware.indexOf('serialLogLevel = serialLogLevelFromRuntime',
                     firmware.indexOf('persistenceReady = EEPROM.begin')) < 0 ||
    firmware.indexOf('serialLogLevel = serialLogLevelFromRuntime',
                     firmware.indexOf('persistenceReady = EEPROM.begin')) >
        firmware.indexOf('BOOT_RESET_REASON') ||
    firmware.indexOf('ringRetainLogLevel =',
                     firmware.indexOf('persistenceReady = EEPROM.begin')) < 0 ||
    !ui.includes("serialDebugOutput:!!($('serialDebugOutput')") ||
    !ui.includes("ringRetainLogLevel:($('ringRetainLogLevel')") ||
    !ui.includes("serialDebugOutput').onchange") ||
    !ui.includes("ringRetainLogLevel').onchange") ||
    !ui.includes('baseRevision') ||
    !network.includes('ringRetainLogLevel') ||
    !html.includes('id="ruleChart"') ||
    !html.includes('id="ruleChartTimeTrack"') ||
    !html.includes('id="ruleChartWeightTrack"') ||
    html.indexOf('id="ruleChart"') < html.indexOf('id="homePresetCards"') ||
    html.indexOf('id="ruleChart"') > html.indexOf('id="shotPanel"') ||
    html.includes('Extraction rules') ||
    !css.includes('.ruleChart') ||
    !css.includes('.ruleSeg-fast') ||
    !css.includes('.ruleSeg-bbw') ||
    !css.includes('.ruleSeg-slow') ||
    !css.includes('.ruleChartIdle') ||
    !ui.includes('function buildRuleChartModel(') ||
    !ui.includes('function renderRuleChart(') ||
    !ui.includes('function updateRuleChartFromStatus(') ||
    !ui.includes('updateRuleChartFromStatus(s)') ||
    !ui.includes('bbw&&!!c.fastExtractionGuardEnabled') ||
    !ui.includes('bbw&&!!c.slowExtractionGuardEnabled') ||
    !ui.includes("mode:'timerOnly'") ||
    !ui.includes("['fast'") ||
    !ui.includes("['bbw'") ||
    !ui.includes("['slow'") ||
    !ui.includes("['idle'") ||
    !ui.includes("if($('ruleChartPreset'))$('ruleChartPreset').textContent=''") ||
    !ui.includes("if($('ruleChartMode'))$('ruleChartMode').textContent=''") ||
    !firmware.includes('SERIAL_DEBUG_ON') ||
    !firmware.includes('SERIAL_DEBUG_OFF') ||
    !firmware.includes('DEBUG_FULL') ||
    !firmware.includes('DEBUG_OFF') ||
    !firmware.includes('DEBUG_STATUS') ||
    !firmware.includes('WIFI_CONNECT') ||
    !firmware.includes('WIFI_DISCONNECT') ||
    !firmware.includes('WEBUI_STOP') ||
    !firmware.includes('LOG_DUMP') ||
    !firmware.includes('SCALE_STATUS') ||
    !firmware.includes('NTP_STATUS') ||
    !firmware.includes('NET_STATUS') ||
    !firmware.includes('WebCommandType::BUZZER_TEST') ||
    !firmware.includes('WebCommandType::BOOKOO_DEBUG') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)') ||
    !firmware.includes('enqueueScaleDebugCommand') ||
    !firmware.includes('executeScaleDebugCommand')) {
  throw new Error('Ring/serial config, rule chart, CLI, and scale/buzzer command paths must remain');
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
    !ui.includes('function formatNetworkStatus(n)') ||
    !ui.includes("n.staState==='CONNECTED'&&typeof n.channel==='number'&&n.channel>0") ||
    !ui.includes("' — channel '+n.channel") ||
    !ui.includes("signalQualityPct") ||
    !ui.includes("n.rssi") ||
    !ui.includes('signal ') ||
    !ui.includes(' dBm)') ||
    !ui.includes("$('hWifi').textContent=formatNetworkStatus(s.network)") ||
    !ui.includes("$('hSsid').textContent=s.network.ssid||'—'") ||
    !ui.includes("$('hAp').textContent='AP: '+(s.network.apActive?'active':'inactive')") ||
    !html.includes('<strong>WiFi</strong><div id="hWifi">') ||
    !html.includes('<strong>SSID</strong><div id="hSsid">') ||
    !html.includes('<strong>AP</strong><div id="hAp">') ||
    !network.includes('WiFi.config(') ||
    !network.includes('confirmPendingNetwork') ||
    !network.includes('revertPendingNetwork') ||
    !network.includes('\\"ipMode\\"') ||
    !network.includes('\\"configState\\"') ||
    !network.includes('\\"ssid\\"') ||
    !network.includes('\\"rssi\\"') ||
    !network.includes('\\"signalQualityPct\\"') ||
    !network.includes('\\"channel\\"') ||
    !network.includes('wifiRssiToSignalQualityPct') ||
    !network.includes('staLinkMetricsValid') ||
    !network.includes('WiFi.RSSI()') ||
    !network.includes('shouldReuseSavedWifiCredentials') ||
    !network.includes('or empty to keep the saved password.') ||
    !network.includes('StaIpMode::STATIC') ||
    !network.includes('STA_CONFIRM_TIMEOUT_MS') ||
    !network.includes('action must be \\"save\\", \\"forget\\", or \\"confirm\\".') ||
    !network.includes('No pending network configuration to confirm.')) {
  throw new Error('DHCP/static IP mode must be wired in UI, status, WiFi.config, and confirm/revert path');
}
if (!network.includes('startStation(next, now)')) {
  throw new Error('STA confirm timeout must reassociate last-known-good before SoftAP fallback');
}
{
  const loginFnStart = network.indexOf('ShotStopperNetwork::loginHandler');
  const loginFnEnd = network.indexOf('ShotStopperNetwork::logoutHandler', loginFnStart);
  const loginFn = loginFnStart >= 0 && loginFnEnd > loginFnStart
      ? network.slice(loginFnStart, loginFnEnd)
      : '';
  if (!loginFn.includes('cJSON_Parse') ||
      loginFn.indexOf('loginRateLimited') < loginFn.indexOf('cJSON_Parse') ||
      !loginFn.includes('recordFailedLoginAttempt')) {
    throw new Error('Login rate limit must apply only after a parseable password verify failure');
  }
}
{
  const oldParseStart = bleLibrary.indexOf('AcaiaArduinoBLE::parseAcaiaOldPacket');
  const oldParseEnd = bleLibrary.indexOf('AcaiaArduinoBLE::parseGenericPacket', oldParseStart);
  const oldParse = oldParseStart >= 0 && oldParseEnd > oldParseStart
      ? bleLibrary.slice(oldParseStart, oldParseEnd)
      : '';
  if (!oldParse.includes('length == 14') ||
      !oldParse.includes('validAcaiaChecksum')) {
    throw new Error('OLD 14-byte Acaia frames must validate checksum');
  }
}
{
  const startCmd = firmware.slice(
      firmware.indexOf('void executeScaleStartCommand'),
      firmware.indexOf('void executeScaleStopCommand'));
  if (!startCmd.includes('tareStartTimer()') ||
      !startCmd.includes('resetTimer()') ||
      !startCmd.includes('if (!event.writeSucceeded)')) {
    throw new Error('Combined tare/start must fall back to reset/start/tare');
  }
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
  ['GET /diagnostic', 'rootHandler'],
  ['GET /log', 'rootHandler'],
  ['GET /history', 'rootHandler'],
  ['GET /admin', 'rootHandler'],
  ['GET /settings', 'rootHandler'],
  ['GET /app.js', 'jsHandler'],
  ['GET /app.css', 'cssHandler'],
  ['POST /api/v1/ui/claim', 'claimHandler'],
  ['GET /api/v1/status/home', 'ownedApiHandler'],
  ['GET /api/v1/status/settings', 'ownedApiHandler'],
  ['GET /api/v1/status/admin', 'ownedApiHandler'],
  ['GET /api/v1/status/diagnostic', 'ownedApiHandler'],
  ['GET /api/v1/log', 'ownedApiHandler'],
  ['POST /api/v1/config', 'ownedApiHandler'],
  ['POST /api/v1/scale/preferred/clear', 'ownedApiHandler'],
  ['POST /api/v1/scale/preferred/select', 'ownedApiHandler'],
  ['POST /api/v1/presets', 'ownedApiHandler'],
  ['POST /api/v1/calibration/reset', 'ownedApiHandler'],
  ['POST /api/v1/calibration/reset-guard-samples', 'ownedApiHandler'],
  ['POST /api/v1/control/paddle', 'ownedApiHandler'],
  ['POST /api/v1/control/rinse', 'ownedApiHandler'],
  ['POST /api/v1/control/stop', 'ownedApiHandler'],
  ['POST /api/v1/control/restart', 'ownedApiHandler'],
  ['POST /api/v1/factory-reset', 'ownedApiHandler'],
  ['GET /api/v1/shots', 'ownedApiHandler'],
  ['POST /api/v1/shots/clear', 'ownedApiHandler'],
  ['POST /api/v1/shots/delete', 'ownedApiHandler'],
  ['POST /api/v1/last-shot/clear', 'ownedApiHandler'],
  ['POST /api/v1/time/sync', 'ownedApiHandler'],
  ['POST /api/v1/network', 'ownedApiHandler'],
  ['POST /api/v1/network/scan', 'ownedApiHandler'],
  ['GET /api/v1/network/scan', 'ownedApiHandler'],
  ['POST /api/v1/access-point/password', 'ownedApiHandler'],
]);

const maxSocketsMatch = network.match(/max_open_sockets\s*=\s*(\d+)/);
if (!maxSocketsMatch || Number(maxSocketsMatch[1]) !== 6) {
  throw new Error('HTTP server must reserve exactly 6 open sockets for the single-owner WebUI');
}
if (!network.includes('backlog_conn = 6')) {
  throw new Error('HTTP server backlog must be limited to 6 for the single-owner WebUI');
}
if (!network.includes('/api/v1/ui/claim') ||
    !network.includes('X-WebUI-Client') ||
    !network.includes('requireActiveWebUiClient') ||
    !network.includes('UI_CLAIM_REQUIRED') ||
    !network.includes('UI_TAKEN_OVER')) {
  throw new Error('WebUI APIs must enforce the exclusive client claim');
}
if (!ui.includes('function claimWebUiOwnership()') ||
    !ui.includes('function deactivateWebUi()') ||
    !ui.includes('Reactivate') ||
    !ui.includes('X-WebUI-Client')) {
  throw new Error('Inactive WebUI windows must become passive and offer Reactivate');
}
if (!ui.includes('const WEB_UI_INACTIVITY_MS=15*60*1000') ||
    !ui.includes('function resetWebUiInactivity()') ||
    !ui.includes('function webUiPollingActive()') ||
    !ui.includes('function noteWebUiInteraction(event)') ||
    !ui.includes("document.addEventListener('pointerdown',noteWebUiInteraction,true)") ||
    !ui.includes("document.addEventListener('keydown',noteWebUiInteraction,true)") ||
    ui.includes("addEventListener('scroll',noteWebUiInteraction")) {
  throw new Error('WebUI inactivity must expire after 15 minutes of direct control interaction, never scrolling');
}
if (!ui.includes('This WebUI window is inactive. Reactivate to continue.') ||
    !network.includes('This WebUI window is inactive. Reactivate to continue.') ||
    ui.includes('Another WebUI window controls this device.') ||
    network.includes('Another WebUI window has taken control.')) {
  throw new Error('WebUI inactive notice must be neutral and offer Reactivate');
}
if (!ui.includes('clearTimeout(webUiInactivityTimer)') ||
    !ui.includes('clearTimeout(scanTimer)') ||
    !ui.includes('stopViewPolls()') ||
    !ui.includes('if(!webUiPollingActive())throw new Error')) {
  throw new Error('WebUI inactivity must cancel poll timers and block further API calls');
}
if (!ui.includes('setMutable(!!s.configMutable||!!s.webUiOverrideActive)') ||
    !ui.includes('configLockReason') || !ui.includes('webUiOverrideActive') ||
    !ui.includes('/api/v1/ui/unlock') || !ui.includes('UNSAFE_WEBUI_OVERRIDE') ||
    !ui.includes('Unsafe WebUI override active')) {
  throw new Error('Web UI must expose the confirmed configuration-lock override flow');
}
if (network.includes('return "safety_recovery"') ||
    network.includes('return "safety_lockout"') ||
    ui.includes("safety_recovery:'safety recovery'") ||
    ui.includes("safety_lockout:'safety lockout'")) {
  throw new Error('Safety recovery must not lock the WebUI');
}
if (!ui.includes("webShot=s.controlSource==='web'&&s.virtualPaddleOn") ||
    !ui.includes("s.safety.recoveryRequired||s.safety.state==='LOCKOUT'") ||
    !ui.includes('remoteReady&&relayStartReady&&(canControl||webShot)') ||
    !ui.includes("$('stopButton').disabled=!s.relayClosed")) {
  throw new Error('CN9 Actions must preserve Stop while inhibiting unsafe starts');
}
if (!network.includes('/api/v1/status/home') ||
    !network.includes('/api/v1/status/settings') ||
    !network.includes('/api/v1/status/admin') ||
    !network.includes('/api/v1/status/diagnostic')) {
  throw new Error('Status API must expose per-page /api/v1/status/{home|settings|admin|diagnostic}');
}
if (!network.includes('statusResponseMux_') ||
    !network.includes('STATUS_BUSY')) {
  throw new Error(
      'Status handler must serialize the shared status buffers with a mutex');
}
if (!network.includes('"Connection"') || !network.includes('"close"')) {
  throw new Error(
      'API JSON responses must send Connection: close to avoid keep-alive socket pinning');
}
if (!network.includes('recv_wait_timeout = 2') ||
    !network.includes('send_wait_timeout = 2')) {
  throw new Error(
      'HTTP recv/send wait timeouts must stay short so LRU can free stalled sockets');
}
const maxReqHdrMatch = network.match(/max_req_hdr_len\s*=\s*(\d+)/);
if (!maxReqHdrMatch || Number(maxReqHdrMatch[1]) < 2048) {
  throw new Error(
      'HTTP server must allow at least 2048 request header bytes (Safari UA/Cookie 431)');
}
const maxRespHeadersMatch = network.match(/max_resp_headers\s*=\s*(\d+)/);
if (!maxRespHeadersMatch || Number(maxRespHeadersMatch[1]) < 12) {
  throw new Error('HTTP server must allow at least 12 response headers for gzip and ETag');
}
if (!ui.includes('function withPollGate(') ||
    !ui.includes('function withCommandGate(') ||
    !ui.includes('function armStatusTimer(') ||
    !ui.includes('function statusPollDue(') ||
    !ui.includes('commandBusy') ||
    !ui.includes('statusLiveShot') ||
    !ui.includes('visibilitychange') ||
    !ui.includes('await refreshStatus()') ||
    !ui.includes('noteReachFail(') ||
    !ui.includes('function startView(') ||
    !ui.includes('function stopViewPolls(') ||
    !ui.includes('function renderRoute(') ||
    !ui.includes('armStatusTimer()') ||
    !ui.includes('AbortController') ||
    !ui.includes('Device timeout') ||
    !ui.includes("throw new Error('Invalid response')") ||
    !ui.includes("throw new Error('Invalid status')") ||
    !ui.includes('function statusUrl(') ||
    !ui.includes('function ensureSettingsHydrated(') ||
    !ui.includes('function homeConfigPatch(') ||
    !ui.includes('function withBaseRev(') ||
    !ui.includes('function isConfigStale(') ||
    !ui.includes('formRev') ||
    !ui.includes('formRev===c.revision') ||
    !ui.includes('baseRevision') ||
    !ui.includes('command(path,value={},soft)') ||
    !ui.includes('/api/v1/status/') ||
    !ui.includes('function statusPageOk(') ||
    !ui.includes('function applyHomeStatus(') ||
    !ui.includes('function applySettingsStatus(') ||
    !ui.includes('function applyAdminStatus(') ||
        ui.includes("api('/api/v1/status')") ||
    !ui.includes('DEVICE_MAX_INFLIGHT') ||
    !ui.includes('acquireDeviceSlot') ||
    !ui.includes('releaseDeviceSlot') ||
    ui.includes('/api/v1/heartbeat') ||
    ui.includes('function heartbeat(') ||
    ui.includes('setInterval(()=>refreshStatus(),2500)')) {
  throw new Error('Web UI must adapt/pause status polls, serialize commands, time out hung fetches, and use DEVICE_MAX_INFLIGHT without POST heartbeat');
}
if (!ui.includes('async function loadStatus(){') ||
    !ui.includes('async function loadShots(){') ||
    !ui.includes('async function loadLog(){') ||
    !ui.includes('function refreshStatus(){return withPollGate(loadStatus)}') ||
    !ui.includes('function refreshShots(){return withPollGate(loadShots)}') ||
    !ui.includes('function refreshLog(){return withPollGate(loadLog)}') ||
    !ui.includes("name==='home'||name==='settings'||name==='admin'||name==='diagnostic'") ||
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
    ui.includes('id="view-debug"') ||
    !ui.includes('id="view-diagnostic"') ||
    ui.includes('id="view-log"') ||
    !ui.includes('data-route="/settings"') ||
    ui.includes('data-route="/presets"') ||
    !ui.includes('data-route="/admin"') ||
    ui.includes('data-route="/debug"') ||
    !ui.includes('data-route="/diagnostic"') ||
    !ui.includes('history.pushState')) {
  throw new Error('Web UI must expose Home/History/Admin/Diagnostic/Settings routes as an SPA');
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
    const statusPage = uri.match(/^\/api\/v1\/status\/(home|settings|admin|diagnostic)$/);
    if (!(statusPage && ui.includes('function statusUrl(') && ui.includes('/api/v1/status/'))) {
      throw new Error(`Registered API is not referenced by the UI: ${uri}`);
    }
  }
}

const forbiddenResponseFields = ['staPassword', 'apPassword', 'authHash', 'authSalt'];
const statusHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::statusHandler');
const statusHandlerEnd = network.indexOf('esp_err_t ShotStopperNetwork::logHandler', statusHandlerStart);
if (statusHandlerStart < 0 || statusHandlerEnd < 0) {
  throw new Error('Status handler not found');
}
const statusFormat = network.slice(statusHandlerStart, statusHandlerEnd);
for (const field of forbiddenResponseFields) {
  if (statusFormat.includes('\\"' + field + '\\"') ||
      statusFormat.includes('"' + field + '"')) {
    throw new Error(`Secret field exposed by status JSON: ${field}`);
  }
}
const soundAlertStatusFields =
    statusFormat.match(/\\"soundAlertsEnabled\\":%s/g) || [];
if (soundAlertStatusFields.length !== 2 ||
    !statusFormat.includes('page == StatusPage::Home') ||
    !statusFormat.includes('page == StatusPage::Settings')) {
  throw new Error(
      'soundAlertsEnabled must be projected only by status/home and status/settings');
}
// Shared status envelope: firmware/bootId/mutable/liveShot/ringRetain only.
// NTP → admin; serialDebug/diagnostics → diagnostic; buzzerSupported → settings.
if (!statusFormat.includes(
        '{\\"firmwareVersion\\":\\"%s\\",\\"bootId\\":%lu,\\"configMutable\\":%s,' +
            '\\"liveShot\\":%s"') ||
    !ui.includes("typeof s.bootId==='number'") ||
    !ui.includes('updateFirmwareFooter()')) {
  throw new Error(
      'Status shared envelope must open with firmwareVersion/bootId/configMutable/liveShot');
}
if (/buzzerSupported.*liveShot|liveShot.*buzzerSupported/.test(
        statusFormat.slice(
            statusFormat.indexOf('{\\"firmwareVersion\\"'),
            statusFormat.indexOf('{\\"firmwareVersion\\"') + 200))) {
  throw new Error('buzzerSupported must not ride in the shared status open append');
}
if (!statusFormat.includes('page == StatusPage::Settings') ||
    !statusFormat.includes(',\\"buzzerSupported\\":%s') ||
    statusFormat.includes('StatusPage::Debug')) {
  throw new Error('buzzerSupported must be gated to status settings only');
}
if (!statusFormat.includes('page == StatusPage::Admin') ||
    !statusFormat.includes('\\"timezoneOffsetMinutes\\":%d') ||
    !statusFormat.includes('\\"ntpServerPreset\\":\\"%s\\"') ||
    !statusFormat.includes('\\"ntpServerCustom\\":\\"%s\\"')) {
  throw new Error('NTP/timezone config must be gated to status admin');
}
{
  const adminMarker = statusFormat.indexOf('Admin page: Wi-Fi/AP status');
  if (adminMarker < 0) {
    throw new Error('status/admin must use an Admin-only network body');
  }
  const adminBody = statusFormat.slice(
      adminMarker, statusFormat.indexOf('} else if (ok && page == StatusPage::Diagnostic)',
                                        adminMarker));
  for (const field of [
    'apActive', 'apIp', 'apClients', 'wifiConfigured', 'ssid', 'open',
    'staState', 'staIp', 'ipMode', 'configState', 'confirmRemainingMs', 'rssi',
    'signalQualityPct', 'configuredIp', 'configuredNetmask', 'configuredGateway',
    'configuredDns1', 'configuredDns2'
  ]) {
    if (!adminBody.includes(field)) {
      throw new Error('status/admin missing required network field: ' + field);
    }
  }
  // Transversal fields used by Admin (footer + config revision + NTP form)
  if (!statusFormat.includes('\\"bootId\\":%lu') ||
      !statusFormat.includes('\\"firmwareVersion\\"') ||
      !statusFormat.includes('\\"configMutable\\"') ||
      !statusFormat.includes('\\"liveShot\\"') ||
      !statusFormat.includes('\\"ringRetainLogLevel\\"') ||
      !ui.includes("typeof s.bootId==='number'") ||
      !ui.includes('function applyAdminStatus(') ||
      !ui.includes('function loadAdminConfig(') ||
      !ui.includes('loadNetworkAddress(s.network)')) {
    throw new Error(
        'status/admin must keep transversal bootId/firmware/liveShot and Admin network/NTP wiring');
  }
  // Diagnostics metrics must not ride on status/admin anymore
  for (const forbidden of [
    'maintenance', 'persistPending', 'hwmon', 'uptimeMs', 'resetReasonCode',
    'packetGaps', 'lastCommand', 'utcSec', 'activeServer', 'serialDebugOutput',
    'buzzerSupported', 'presets', 'brewByWeight'
  ]) {
    if (adminBody.includes(forbidden)) {
      throw new Error(
          'status/admin must not include Diagnostic/settings-only field: ' +
          forbidden);
    }
  }
  if (!ui.includes(
          "v==='admin'?!!(s.network&&typeof c.timezoneOffsetMinutes==='number'&&c.ntpServerPreset!=null)")) {
    throw new Error(
        'statusPageOk(admin) must validate network + NTP config only');
  }
}
if ((statusFormat.match(/page == StatusPage::Diagnostic/g) || []).length < 1 ||
    !statusFormat.includes(',\\"serialDebugOutput\\":%s') ||
    !statusFormat.includes('StatusPage::Diagnostic')) {
  throw new Error('serialDebugOutput and diagnostic metrics must be gated to status diagnostic');
}
{
  const leanMarker = statusFormat.indexOf('Lean diagnostic snapshot');
  if (leanMarker < 0) {
    throw new Error('status/diagnostic must use a lean Diagnostic-only body');
  }
  const diagBody = statusFormat.slice(
      leanMarker, statusFormat.indexOf('if (ok) {', leanMarker));
  for (const field of [
    'apActive', 'apIp', 'apClients', 'wifiConfigured', 'ssid', 'staState',
    'channel', 'staIp', 'ipMode', 'configState', 'confirmRemainingMs', 'rssi',
    'signalQualityPct', 'utcSec', 'lastSyncAgeMs', 'nextRetryInMs',
    'activeServer', 'maintenance', 'persistPending', 'uptimeMs', 'hwmon',
    'freeHeapBytes', 'minimumFreeHeapBytes', 'largestFreeHeapBlockBytes',
    'resetReasonCode', 'packetGaps', 'rejectedPackets', 'reconnects',
    'eventsDropped', 'lastCommand'
  ]) {
    if (!diagBody.includes(field)) {
      throw new Error('status/diagnostic missing required field: ' + field);
    }
  }
  // Transversal fields used by Diagnostic (footer + log controls + mutability)
  if (!statusFormat.includes('\\"bootId\\":%lu') ||
      !statusFormat.includes('\\"firmwareVersion\\"') ||
      !statusFormat.includes('\\"configMutable\\"') ||
      !statusFormat.includes('\\"liveShot\\"') ||
      !statusFormat.includes('\\"ringRetainLogLevel\\"') ||
      !statusFormat.includes('\\"timezoneOffsetMinutes\\":%d') ||
      !ui.includes("typeof s.bootId==='number'") ||
      !ui.includes('function applyDiagnosticStatus(')) {
    throw new Error(
        'status/diagnostic must keep transversal bootId/firmware/liveShot/ringRetain for the Diagnostic page');
  }
  for (const forbidden of [
    'configuredIp', 'configuredNetmask', 'configuredGateway', 'configuredDns1',
    'configuredDns2'
  ]) {
    if (diagBody.includes(forbidden)) {
      throw new Error(
          'status/diagnostic must not include Admin-only network field: ' +
          forbidden);
    }
  }
  // "open" appears only on Admin network object, not Diagnostic lean body.
  if (/\\"open\\"/.test(diagBody)) {
    throw new Error('status/diagnostic must not include Admin-only network open flag');
  }
  if (diagBody.includes('ntpServerPreset') ||
      diagBody.includes('ntpServerCustom') ||
      diagBody.includes('buzzerSupported') ||
      diagBody.includes('presets') ||
      diagBody.includes('brewByWeight')) {
    throw new Error(
        'status/diagnostic must not include settings/admin-only payload fields');
  }
}
const sharedRingOpen = statusFormat.indexOf(
    ',\\"config\\":{\\"revision\\":%lu,\\"ringRetainLogLevel\\":\\"%s\\"');
if (sharedRingOpen < 0) {
  throw new Error(
      'Status shared config must open with revision then ringRetainLogLevel');
}
const ringOpenSlice = statusFormat.slice(sharedRingOpen, sharedRingOpen + 180);
if (ringOpenSlice.includes('timezoneOffsetMinutes') ||
    ringOpenSlice.includes('ntpServerPreset') ||
    ringOpenSlice.includes('serialDebugOutput')) {
  throw new Error(
      'NTP/serialDebug must not share the revision/ringRetainLogLevel open append');
}
if (!ui.includes(
        "typeof s.buzzerSupported==='boolean')updateBuzzerAlertVisibility") &&
    !ui.includes(
        'typeof s.buzzerSupported==="boolean")updateBuzzerAlertVisibility')) {
  throw new Error(
      'applyCommonStatus must only update buzzer visibility when buzzerSupported is present');
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

if (/AP_WINDOW_MS/.test(networkHeader) || /AP_WINDOW_MS/.test(network)) {
  throw new Error('Idle SoftAP shutdown window must remain removed');
}
if (!network.includes('WiFi.mode(WIFI_STA)') ||
    !network.includes('WiFi.mode(WIFI_AP)') ||
    !network.includes('WIFI_AP_STA') ||
    !network.includes('ensureAccessPoint') ||
    !network.includes('beginStationConnect') ||
    !network.includes('stopSoftApKeepStation') ||
    !network.includes('wifiScanInProgress') ||
    !network.includes('STA_RECOVERY_ATTEMPT_MS')) {
  throw new Error(
      'Network must use STA-first boot, SoftAP when unassociated, WIFI_AP_STA while retrying STA, and pause retries during Wi-Fi scan');
}
if (!network.includes('WiFi.scanNetworks(true, false, false, 120)') ||
    !network.includes('esp_wifi_scan_stop()') ||
    !network.includes('abortWifiScan') ||
    !network.includes('WIFI_SCAN_TIMEOUT_MS')) {
  throw new Error(
      'WiFi scan must be asynchronous, cancelable, abortable on mode change, and time-bounded');
}
if (network.includes('recycleHttpServer') ||
    network.includes('noteHttpServeResult') ||
    network.includes('recoverFromResourcePressure') ||
    network.includes('associated without IP')) {
  throw new Error(
      'HTTP recycle / sticky no-IP recovery must stay removed — they kill ping when WebUI opens');
}
if (network.includes('networkShutdownPending_') ||
    /networkShutdownPending_\s*=\s*age\s*>=/.test(network)) {
  throw new Error('SoftAP must not shut down on an idle visibility timer');
}
if (!network.includes(
        'there is no idle SoftAP shutdown') ||
    !network.includes('link loss does not auto-raise SoftAP')) {
  throw new Error(
      'serviceSessions must keep SoftAP up without idle shutdown; post-CONNECTED link loss must not auto-raise SoftAP');
}
const stopSoftApStart = network.indexOf(
    'void ShotStopperNetwork::stopSoftAp(');
const stopSoftApKeepStart = network.indexOf(
    'void ShotStopperNetwork::stopSoftApKeepStation()');
const stopSoftApEnd = network.indexOf(
    'bool ShotStopperNetwork::wifiScanInProgress()', stopSoftApKeepStart);
if (stopSoftApStart < 0 || stopSoftApKeepStart < 0 || stopSoftApEnd < 0) {
  throw new Error('stopSoftAp / stopSoftApKeepStation implementation not found');
}
const stopSoftAp = network.slice(stopSoftApStart, stopSoftApEnd);
if (stopSoftAp.includes('WiFi.mode(WIFI_STA)')) {
  throw new Error(
      'stopSoftApKeepStation must not force WIFI_STA and risk dropping the STA link');
}
if (!stopSoftAp.includes('stopHttpServer()')) {
  throw new Error(
      'stopSoftAp must be able to stop HTTP so STA rebind can restart the server');
}
if (!network.includes('stopSoftApLeaveHttp') ||
    !network.includes('httpStartHeld_') ||
    !network.includes('staReconnectHeld_') ||
    !network.includes('apStartHeld_') ||
    !network.includes('apKeepRequested_')) {
  throw new Error(
      'Network CLI holds must keep SoftAP/HTTP/STA stop from being undone');
}
const serviceStart = network.indexOf('void ShotStopperNetwork::service()');
const serviceEnd = network.indexOf(
    'bool ShotStopperNetwork::controlAllowsNetworkMutation', serviceStart);
if (serviceStart < 0 || serviceEnd < 0) {
  throw new Error('ShotStopperNetwork::service implementation not found');
}
const serviceBody = network.slice(serviceStart, serviceEnd);
const processAt = serviceBody.indexOf('processAcceptedCommands()');
const startupReturnAt = serviceBody.indexOf('if (!startupComplete_)');
if (processAt < 0 || startupReturnAt < 0 || processAt > startupReturnAt) {
  throw new Error(
      'CLI network actions must drain before the startupComplete_ early return');
}
if (!network.includes('!apKeepRequested_')) {
  throw new Error(
      'STA-up SoftAP teardown must keep a user AP_START SoftAP');
}
const ensureStart = network.indexOf(
    'bool ShotStopperNetwork::ensureAccessPoint');
const ensureEnd = network.indexOf(
    'void ShotStopperNetwork::stopNetwork()', ensureStart);
if (ensureStart < 0 || ensureEnd < 0) {
  throw new Error('ensureAccessPoint implementation not found');
}
const ensureBody = network.slice(ensureStart, ensureEnd);
if (!ensureBody.includes('httpStartHeld_') ||
    !ensureBody.includes('staLinkUp') ||
    !ensureBody.includes('keepHttp')) {
  throw new Error(
      'ensureAccessPoint must keep a live STA link and skip HTTP while WEBUI_STOP is held');
}
const snapshotStart = network.indexOf(
    'void ShotStopperNetwork::printActionSnapshot');
const snapshotEnd = network.indexOf(
    'void ShotStopperNetwork::noteCliNetworkProgress()', snapshotStart);
if (snapshotStart < 0 || snapshotEnd < 0 ||
    !network.slice(snapshotStart, snapshotEnd)
         .includes('refreshExtendedStatus')) {
  throw new Error(
      'CLI action snapshot must refresh holds after the mutation');
}
if (!network.includes('void ShotStopperNetwork::lifecycleLog(') ||
    !network.includes('serialDebugEnabled() && message')) {
  throw new Error(
      'Automatic STA/SoftAP lifecycle logs must stay behind serialDebugEnabled');
}
if (network.includes('raising SoftAP and retrying STA') ||
    network.includes('retrying STA before SoftAP')) {
  throw new Error(
      'STA disconnect after prior connect must retry station without SoftAP auto-raise');
}
if (!network.includes('no SoftAP after prior connect') ||
    !network.includes('SoftAP suppressed after prior connect') ||
    !network.includes('!staEverConnected_') ||
    !network.includes('STA_CONNECT_TIMEOUT_MS')) {
  throw new Error(
      'SoftAP auto-raise must gate on !staEverConnected_ and wait STA_CONNECT_TIMEOUT only for boot/bootstrap');
}
if (network.includes('staEverConnected_ = false')) {
  throw new Error(
      'staEverConnected_ must latch for process lifetime (never clear after first CONNECTED)');
}
if (!network.includes('SoftAP suppressed (AP_START or reboot)') ||
    !networkHeader.includes('Latched for process lifetime')) {
  throw new Error(
      'Pending revert / startStation must keep SoftAP boot-only after prior STA join');
}
if (!network.includes('!status.apActive') ||
    !network.includes('STA_CONNECT_TIMEOUT_MS')) {
  throw new Error(
      'STA connect timeout must remain SoftAP bootstrap-only when AP is inactive');
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
if (!safeBeep.includes('return setBeepLevel(1)') ||
    !safeBeep.includes('GENERIC_BEEP_LEVEL_CMD') ||
    !safeBeep.includes('fillGenericCommand') ||
    safeBeep.includes('BEEP_LEVEL_1_BOOKOO') ||
    safeBeep.includes('TARE_ACAIA') || safeBeep.includes('TARE_GENERIC') ||
    safeBeep.includes('_connected = false')) {
  throw new Error('First-drop beep must not tare or mutate scale connection state');
}
if (!firmware.includes('emitAlert(AlertEvent::FIRST_DROP') ||
    !firmware.includes('emitAlert(AlertEvent::SCALE_CONNECTED') ||
    !firmware.includes('emitAlert(AlertEvent::SCALE_LOST') ||
    !firmware.includes('BuzzerPattern::ECHO') ||
    !firmware.includes('BuzzerPattern::ECHO_INVERTED') ||
    !firmware.includes('requestScaleBrewBeep(') ||
    !firmware.includes('cancelScaleBrewBeep(session.id)') ||
    !firmware.includes('onFirstDropsDetected') ||
    !firmware.includes('notifyRetareFlowDetected') ||
    !firmware.includes('retareFlowFirstDetectedAtMs') ||
    !firmware.includes('bbwProtectionActive') ||
    !firmware.includes('retareWindowOpen') ||
    !firmware.includes('scale.supportsTareStartTimer()') ||
    !firmware.includes('alertOutputChannel') ||
    !firmware.includes('applyBookooConnectBeepPolicy') ||
    !firmware.includes('requestBookooSilenceIfConfigured') ||
    !firmware.includes('emitCommandAlert') ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer') ||
    !firmware.includes('commandAlertUsesBuzzer') ||
    /enum class ScaleCommandType[\s\S]*BEEP/.test(
      firmware.slice(firmware.indexOf('enum class ScaleCommandType'),
                     firmware.indexOf('enum class ScaleEventType')))) {
  throw new Error('Best-effort beep must stay outside the critical BLE command queue');
}

const emitCommandImplStart = firmware.indexOf(
    '// BLE-result fallback only');
const emitCommandImplEnd = firmware.indexOf(
    'void requestScaleBrewBeep(uint32_t cycleId) {', emitCommandImplStart);
const emitCommandImpl = emitCommandImplStart < 0 || emitCommandImplEnd < 0
    ? ''
    : firmware.slice(emitCommandImplStart, emitCommandImplEnd);
if (!emitCommandImpl.includes('AlertOutputChannel::BUZZER_ONLY') ||
    emitCommandImpl.includes('if (commandAlertUsesBuzzer())') ||
    /if \(channel == AlertOutputChannel::BUZZER_ONLY\) \{\s*emitLocalAlertBuzzer/.test(
        emitCommandImpl)) {
  throw new Error('Buzzer-routed command alerts must not wait for BLE results');
}
const immediateCommandAlertCalls =
    firmware.split('emitImmediateCommandAlertIfBuzzer();').length - 1;
if (immediateCommandAlertCalls < 4 ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer();\n    if (!requestRemoteTimerStart())') ||
    !firmware.includes(
        'if (shotCompletionGetsLongBeep(reason)) {\n    // Completion LONG replaces the stop-timer SINGLE so ends are one cue.\n    scheduleScaleCompletionBeep();\n  } else {\n    emitImmediateCommandAlertIfBuzzer();') ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer();\n  markRetareEnded')) {
  throw new Error('Command alerts must fire at CN9/paddle/retare, not after BLE');
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
if (generated.gzip.length > 9216) {
  throw new Error('Compressed Web UI HTML exceeds the 9 KiB gzip budget');
}
if (generated.jsGzip.length > 20480) {
  throw new Error('Compressed Web UI JS exceeds the 20 KiB gzip budget');
}
if (generated.cssGzip.length > 6144) {
  throw new Error('Compressed Web CSS exceeds the 6 KiB gzip budget');
}
if (generated.gzip.length + generated.jsGzip.length + generated.cssGzip.length >
    31232) {
  throw new Error('Combined HTML+JS+CSS gzip exceeds the 30.5 KiB flash budget');
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
    network.includes('SHOT_STOPPER_WEB_LOGO_GZIP') ||
    !network.includes('"Content-Encoding"') ||
    !network.includes('"gzip"')) {
  throw new Error('GET /, GET /app.js, and GET /app.css must send precompressed gzip bodies without logo');
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
const notFoundHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::notFoundHandler');
const loginHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::loginHandler');
if (rootHandlerStart < 0 || jsHandlerStart < 0 || cssHandlerStart < 0 ||
    notFoundHandlerStart < 0 || loginHandlerStart < 0 ||
    network.includes('logoHandler') ||
    !(rootHandlerStart < jsHandlerStart && jsHandlerStart < cssHandlerStart &&
      cssHandlerStart < notFoundHandlerStart &&
      notFoundHandlerStart < loginHandlerStart)) {
  throw new Error('rootHandler/jsHandler/cssHandler/notFoundHandler order not found');
}
const rootHandler = network.slice(rootHandlerStart, jsHandlerStart);
const jsHandler = network.slice(jsHandlerStart, cssHandlerStart);
const cssHandler = network.slice(cssHandlerStart, notFoundHandlerStart);
const notFoundHandler = network.slice(notFoundHandlerStart, loginHandlerStart);
if (rootHandler.includes('no-store') || !rootHandler.includes('no-cache') ||
    !rootHandler.includes('STATUS_NOT_MODIFIED') ||
    !rootHandler.includes('ifNoneMatchEquals') ||
    !rootHandler.includes('ETag') ||
    !rootHandler.includes("style-src 'self'") ||
    !rootHandler.includes("script-src 'self'") ||
    !rootHandler.includes('"Connection"') ||
    !rootHandler.includes('"close"') ||
    rootHandler.includes("script-src 'unsafe-inline'") ||
    rootHandler.includes('HTTPD_RESP_USE_STRLEN')) {
  throw new Error('GET / must revalidate with ETag/304, CSP script/style self, Connection close, and gzip by length');
}
if (jsHandler.includes('no-store') ||
    !jsHandler.includes('max-age=31536000') ||
    !jsHandler.includes('immutable') ||
    !jsHandler.includes('STATUS_NOT_MODIFIED') ||
    !jsHandler.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !jsHandler.includes('"Connection"') ||
    !jsHandler.includes('"close"') ||
    !jsHandler.includes('application/javascript')) {
  throw new Error('GET /app.js must serve immutable gzip JS with ETag/304 and Connection close');
}
if (cssHandler.includes('no-store') ||
    !cssHandler.includes('max-age=31536000') ||
    !cssHandler.includes('immutable') ||
    !cssHandler.includes('STATUS_NOT_MODIFIED') ||
    !cssHandler.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !cssHandler.includes('"Connection"') ||
    !cssHandler.includes('"close"') ||
    !cssHandler.includes('text/css')) {
  throw new Error('GET /app.css must serve immutable gzip CSS with ETag/304 and Connection close');
}
if (network.includes('logoHandler') || network.includes('SHOT_STOPPER_WEB_LOGO')) {
  throw new Error('Firmware must not serve /logo.svg');
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
if (!network.includes('touchSessionIfPresent') ||
    network.includes('heartbeatHandler') ||
    network.includes('/api/v1/heartbeat') ||
    networkHeader.includes('WEB_PADDLE_HEARTBEAT_TIMEOUT_MS') ||
    network.includes('WEB_PADDLE_HEARTBEAT_TIMEOUT_MS') ||
    network.includes('heartbeatStopSent_')) {
  throw new Error(
      'Session touch must replace POST /heartbeat; web paddle heartbeat CN9 timeout must be gone');
}
const logHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::logHandler');
const shotsHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::shotsHandler');
const wifiScanStatusStart =
    network.indexOf('esp_err_t ShotStopperNetwork::wifiScanStatusHandler');
if (logHandlerStart < 0 || shotsHandlerStart < 0 || wifiScanStatusStart < 0 ||
    !network.slice(logHandlerStart, shotsHandlerStart).includes('"Connection"') ||
    !network.slice(logHandlerStart, shotsHandlerStart).includes('"close"') ||
    !network.slice(wifiScanStatusStart).includes('"Connection"') ||
    !network.slice(wifiScanStatusStart, wifiScanStatusStart + 800)
         .includes('"close"')) {
  throw new Error('Chunked log and Wi-Fi scan status responses must send Connection: close');
}
if (!js.includes('withPollGate(async()=>{if(scanBusy||!token)return;scanBusy=true') ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/shots/clear'") ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/shots/delete'") ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/logout'")) {
  throw new Error('Wi-Fi scan and shot clear/delete/logout must use poll/command gates');
}

{
  const start = js.indexOf('function buildRuleChartModel(');
  const end = js.indexOf('let ruleChartSig=');
  if (start < 0 || end < 0 || end <= start) {
    throw new Error('Rule chart model helpers not found for matrix checks');
  }
  const helpers = new Function(
      js.slice(start, end) +
      ';return{buildRuleChartModel:buildRuleChartModel};')();
  const base = {
    brewByWeight: true,
    fastExtractionGuardEnabled: true,
    slowExtractionGuardEnabled: true,
    operationalWallMs: 50000,
    minBrewTimeMs: 28000,
    maxBrewTimeMs: 44000,
    goalWeightG: 36,
    minRecoveryWeightG: 34,
    maxRecoveryWeightG: 42.5,
  };
  const kinds = (segs) => (segs || []).map((s) => s[0]).join(',');
  const off = helpers.buildRuleChartModel({...base, brewByWeight: false});
  if (off.mode !== 'timerOnly' || kinds(off.tSeg) !== 'idle' ||
      kinds(off.wSeg) !== 'idle' || off.fs) {
    throw new Error('Rule chart: BBW off must be timerOnly idle (ignore guard flags)');
  }
  const both = helpers.buildRuleChartModel(base);
  if (both.mode !== 'active' || kinds(both.tSeg) !== 'fast,bbw,slow' ||
      kinds(both.wSeg) !== 'slow,bbw,fast' || !both.fs || both.goal !== 36) {
    throw new Error('Rule chart: BBW+Fast+Slow matrix row failed');
  }
  const fastOnly = helpers.buildRuleChartModel({
    ...base, slowExtractionGuardEnabled: false
  });
  if (kinds(fastOnly.tSeg) !== 'fast,bbw' ||
      kinds(fastOnly.wSeg) !== 'bbw,fast' || !fastOnly.fs) {
    throw new Error('Rule chart: Fast on / Slow off must extend BBW into Slow');
  }
  const slowOnly = helpers.buildRuleChartModel({
    ...base, fastExtractionGuardEnabled: false
  });
  if (kinds(slowOnly.tSeg) !== 'bbw,slow' ||
      kinds(slowOnly.wSeg) !== 'slow,bbw' || slowOnly.fs) {
    throw new Error('Rule chart: Fast off / Slow on must extend BBW into Fast');
  }
  const none = helpers.buildRuleChartModel({
    ...base, fastExtractionGuardEnabled: false,
    slowExtractionGuardEnabled: false
  });
  if (kinds(none.tSeg) !== 'bbw' || kinds(none.wSeg) !== 'bbw' ||
      none.fs || none.goal !== 36) {
    throw new Error('Rule chart: both guards off must be BBW-only with goal');
  }
  const ignoredGuards = helpers.buildRuleChartModel({
    ...base, brewByWeight: false, fastExtractionGuardEnabled: true,
    slowExtractionGuardEnabled: true
  });
  if (ignoredGuards.mode !== 'timerOnly' ||
      kinds(ignoredGuards.tSeg) !== 'idle') {
    throw new Error('Rule chart: guards must be ignored when BBW is off');
  }
}

console.log(
  `Embedded Web UI: JavaScript valid, ${htmlBytes} bytes HTML / ${jsBytes} bytes JS source, ` +
  `${generated.gzip.length} bytes HTML gzip, ${generated.jsGzip.length} bytes JS gzip, ` +
  `${generated.cssGzip.length} bytes CSS gzip, ` +
  `${expected.size} routes checked`
);
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
