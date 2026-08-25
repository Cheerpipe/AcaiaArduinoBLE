'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const webUi = require('../../scripts/gen_web_ui.js');

const sketchDir = path.resolve(__dirname, '..');
const asset = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssets.h'), 'utf8');
const network = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
const networkHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.h'), 'utf8');
const firmwareCore = fs.readFileSync(path.join(sketchDir, 'shotStopper.cpp'), 'utf8');
const firmware = [
  firmwareCore,
  fs.readFileSync(path.join(sketchDir, 'ShotStopperHardware.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachine.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineRelay.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineActivatorSample.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleInput.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleControl.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleState.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryInput.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryConfig.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryControl.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryReedState.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryOnlyState.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleSense.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperCupPresence.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperRinse.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperShotCurveTypes.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperAlert.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertChannel.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertTone.h'), 'utf8'),
].join('\n');
const shotLogIo = fs.readFileSync(path.join(sketchDir, 'ShotStopperShotLog.h'), 'utf8');
const shotCurveIo = fs.readFileSync(path.join(sketchDir, 'ShotStopperShotCurve.h'), 'utf8');
const lastShotIo = fs.readFileSync(path.join(sketchDir, 'ShotStopperLastShot.h'), 'utf8');
const jsonArena = fs.readFileSync(path.join(sketchDir, 'ShotStopperJsonArena.h'), 'utf8');
const wallClock = fs.readFileSync(path.join(sketchDir, 'ShotStopperTime.h'), 'utf8');
const domainCore = fs.readFileSync(path.join(sketchDir, 'ShotStopperDomain.h'), 'utf8');
const domain = [
  domainCore,
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleTypes.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8'),
].join('\n');
const serialCli = fs.readFileSync(path.join(sketchDir, 'ShotStopperSerialCli.h'), 'utf8');
const buzzer = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzer.h'), 'utf8');
const buzzerPatterns = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzerPatterns.h'), 'utf8');
const buzzerPassive = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzerPassive.h'), 'utf8');
const buzzerRtttl = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzerRtttl.h'), 'utf8');
const alertChannel = fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertChannel.h'), 'utf8');
const alertTone = fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertTone.h'), 'utf8');
const kconfig = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'main', 'Kconfig.projbuild'), 'utf8');
const bleLibrary = fs.readFileSync(
  path.resolve(sketchDir, '..', 'libraries', 'AcaiaArduinoBLE', 'AcaiaArduinoBLE.cpp'),
  'utf8'
);
const bleCompanion = fs.readFileSync(
  path.join(sketchDir, 'ShotStopperBleCompanion.h'), 'utf8');
const taskProfiler = fs.readFileSync(
  path.join(sketchDir, 'ShotStopperTaskProfiler.h'), 'utf8');
const sdkconfigDefaults = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'sdkconfig.defaults'), 'utf8');
const flashIoScratch = fs.readFileSync(
  path.join(sketchDir, 'ShotStopperFlashIoScratch.h'), 'utf8');
if (!bleCompanion.includes('BLECharacteristic::writeValue') ||
    bleCompanion.includes('String("")') ||
    bleCompanion.includes('String(value)')) {
  throw new Error(
      'BLE companion string writes must use BLECharacteristic::writeValue(const char*), not Arduino String');
}
if (!taskProfiler.includes('allocExternalOrInternal(sizeof(ActiveWorkspace))') ||
    !taskProfiler.includes('heapCapsFree(workspace_)') ||
    taskProfiler.includes('calloc(') ||
    /\bfree\(workspace_\)/.test(taskProfiler) ||
    /\bfree\(next\)/.test(taskProfiler)) {
  throw new Error(
      'TaskProfiler workspace must use allocExternalOrInternal/heapCapsFree, not calloc/free');
}
if (taskProfiler.includes('task.xCoreID') ||
    !taskProfiler.includes('xTaskGetCoreID(task.xHandle)')) {
  throw new Error(
      'TaskProfiler must read core pin via xTaskGetCoreID, not TaskStatus_t.xCoreID');
}
if (!sdkconfigDefaults.includes('CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y') ||
    !sdkconfigDefaults.includes('CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y')) {
  throw new Error(
      'sdkconfig.defaults must enable run-time stats and vTaskList core IDs');
}
if (!taskProfiler.includes('void copySnapshot(TaskProfilerSnapshot &out) const') ||
    !taskProfiler.includes('taskYIELD()') ||
    !taskProfiler.includes('__atomic_load_n(&seq_') ||
    !taskProfiler.includes('beginSnapshotWrite_()')) {
  throw new Error(
      'TaskProfiler snapshot copies must use a seqlock; uxTaskGetSystemState must not run under it');
}
if (!firmwareCore.includes('#include "ShotStopperTaskProfiler.h"') ||
    !firmwareCore.includes('TaskProfiler taskProfiler') ||
    !firmwareCore.includes('taskProfiler.service(millis())') ||
    !firmwareCore.includes('void copyTaskProfiler(TaskProfilerSnapshot &output)') ||
    !firmwareCore.includes('taskProfiler.copySnapshot(output)') ||
    !firmwareCore.includes('WebCommandType::TASK_PROFILER_START') ||
    !firmwareCore.includes('WebCommandType::TASK_PROFILER_STOP') ||
    !firmwareCore.includes('callbacks.copyTaskProfiler = copyTaskProfiler') ||
    (firmwareCore.match(/taskProfiler\.start\(/g) || []).length !== 1) {
  throw new Error(
      'TaskProfiler must be wired as opt-in Diagnostic telemetry with copyTaskProfiler');
}
{
  const setupBody = firmwareCore.slice(
      firmwareCore.indexOf('void setup()'), firmwareCore.indexOf('void loop()'));
  const publishBody = firmwareCore.slice(
      firmwareCore.indexOf('void publishControlStatus()'),
      firmwareCore.indexOf('void resetSerialCliState()'));
  if (setupBody.includes('taskProfiler.start') ||
      publishBody.includes('taskProfiler') ||
      publishBody.includes('TaskProfiler')) {
    throw new Error(
        'Task profiler must not auto-start on boot and must not ride ControlStatusSnapshot');
  }
}
if (!network.includes('copyTaskProfiler') ||
    !network.includes('statusJsonAppendTaskProfiler') ||
    !network.includes('\\"tasks\\"') ||
    !network.includes('\\"currentTotalCpuPct\\"') ||
    !network.includes('\\"unreportedCurrentCpuPct\\"') ||
    !network.includes('/api/v1/diagnostic/profiler') ||
    !network.includes('taskProfilerHandler') ||
    !network.includes('static constexpr size_t kStatusJson = 12288') ||
    !networkHeader.includes('void (*copyTaskProfiler)(TaskProfilerSnapshot &out)') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperDebugExport.h'), 'utf8')
        .includes('DEBUG_EXPORT_SCHEMA_VERSION = 4')) {
  throw new Error(
      'Diagnostic status, POST /api/v1/diagnostic/profiler, and debug export must expose tasks');
}
const psram = fs.readFileSync(path.join(sketchDir, 'ShotStopperPsram.h'), 'utf8');
if (!psram.includes('#define SHOT_STOPPER_PSRAM_BSS EXT_RAM_BSS_ATTR') ||
    !firmwareCore.includes('SHOT_STOPPER_PSRAM_BSS ShotLog shotLog') ||
    !firmwareCore.includes('SHOT_STOPPER_PSRAM_BSS ShotCurveLog shotCurves') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS PersistedSettings persistedSettings') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistRequest') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistReceive') ||
    !firmwareCore.includes(
        'xQueueReceive(settingsPersistQueue, &settingsPersistReceive') ||
    !firmwareCore.includes(
        'constexpr uint32_t SETTINGS_PERSIST_TASK_STACK_SIZE = 4096') ||
    !network.includes(
        'static SHOT_STOPPER_PSRAM_BSS WifiScanSnapshot g_wifiScan') ||
    !network.includes(
        'static SHOT_STOPPER_PSRAM_BSS PersistedSettings g_networkSettings') ||
    !networkHeader.includes('PersistedSettings &settings_') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS DebugRingBuffer debugLog') ||
    !flashIoScratch.includes('allocInternal(FLASH_IO_SCRATCH_BYTES)') ||
    firmwareCore.includes('SHOT_STOPPER_PSRAM_BSS ControlStatusSnapshot')) {
  throw new Error(
      'Large history/settings/debug-ring BSS must use SHOT_STOPPER_PSRAM_BSS; flash scratch and live status snapshots stay internal');
}
if (network.includes('ControlStatusSnapshot status;') ||
    network.includes('ControlStatusSnapshot control;') ||
    firmwareCore.includes('ControlStatusSnapshot status;') ||
    !network.includes('ControlGateSnapshot ShotStopperNetwork::controlGate()') ||
    !firmwareCore.includes('void copyControlGate(ControlGateSnapshot &output)')) {
  throw new Error(
      'Network/httpd gate checks must copy ControlGateSnapshot, not a stack ControlStatusSnapshot');
}
if (firmwareCore.includes('portENTER_CRITICAL(&webStatusMux)') ||
    firmwareCore.includes('next.presets = presetBank') ||
    firmwareCore.includes('copyScaleHistory(next.scaleHistory)') ||
    !firmwareCore.includes('__atomic_fetch_add(&controlStatusSeq') ||
    !network.includes('self.callbacks_.copyPresetBank(&g_work->presetBank)') ||
    !network.includes('self.callbacks_.copyScaleHistory(g_work->scaleHistory)')) {
  throw new Error(
      'Status snapshot publish must use a seqlock and fill presets/history on demand');
}
if (network.includes('composeEffectiveConfig(candidate, status.presets)') ||
    network.includes('status.presets') ||
    !network.includes('self.callbacks_.copyPresetBank(&livePresets)') ||
    !network.includes('composeEffectiveConfig(candidate, livePresets)')) {
  throw new Error(
      'configHandler must composeEffectiveConfig against copyPresetBank, not ControlGateSnapshot.presets');
}
if (!flashIoScratch.includes('inline void feedFlashIoWatchdog()') ||
    !flashIoScratch.includes('esp_task_wdt_status(nullptr) == ESP_OK') ||
    !flashIoScratch.includes('(void)esp_task_wdt_reset()')) {
  throw new Error(
      'Flash I/O watchdog feed must reset TWDT only when the current task is subscribed');
}
if (!bleLibrary.includes('readValue(input, MAX_BLE_PACKET_LENGTH)') ||
    !bleLibrary.includes('length > MAX_BLE_PACKET_LENGTH')) {
  throw new Error(
      'Acaia BLE reads must clamp to MAX_BLE_PACKET_LENGTH');
}
if (!bleLibrary.includes('BLE.scan(true)') ||
    bleLibrary.includes('BLE.scan(false)')) {
  throw new Error(
      'Idle GAP scan must stay on with withDuplicates=true');
}
if (!firmwareCore.includes('scaleWorkerTickDelayMs()') ||
    !firmwareCore.includes('controlLoopTickDelayMs()') ||
    !firmwareCore.includes('SCALE_WORKER_NO_SCALE_DELAY_MS') ||
    !firmwareCore.includes('CONTROL_STATUS_PUBLISH_NO_SCALE_MS') ||
    !firmwareCore.includes('taskYIELD()') ||
    (firmwareCore.split('publishBleCompanionStatus(inactiveStatus)').length - 1) !== 1) {
  throw new Error(
      'No-scale idle must relax worker/loop/status and skip inactive Companion each tick');
}
{
  const reedState = fs.readFileSync(
      path.join(sketchDir, 'ShotStopperMachineMomentaryReedState.h'), 'utf8');
  const getterNames = [
    ['bool machineAllowsFirmwareStopPulse', 'inline bool machineRunningElapsed'],
    ['inline bool machineRunningElapsed', 'inline bool machineIsRunning'],
    ['inline bool machineIsRunning', 'inline uint32_t machineElapsedMs'],
    ['inline MachineRunState machineRunState', 'inline void machineFillInferenceStatus'],
  ];
  for (const [startName, endName] of getterNames) {
    const start = reedState.indexOf(startName);
    const end = reedState.indexOf(endName);
    if (start < 0 || end <= start || reedState.slice(start, end).includes('sampleReed()')) {
      throw new Error('Reed getters must use the last sampled state, not sampleReed()');
    }
  }
  if (!reedState.includes('void serviceReedAssumeWindow() {\n  sampleReed();') ||
      !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachine.h'), 'utf8')
           .includes('sampleReed();')) {
    throw new Error('Reed must be sampled from input/service, not from getters');
  }
}
{
  const hwmon = fs.readFileSync(path.join(sketchDir, 'ShotStopperHwmon.h'), 'utf8');
  if (!hwmon.includes('const HeapCapSnapshot *heap = nullptr') ||
      !hwmon.includes('heap != nullptr ? *heap : sampleHeapCaps()') ||
      !firmwareCore.includes('hwmon.sample(intervalMs > 0U ? intervalMs') ||
      !firmwareCore.includes('&heap)')) {
    throw new Error('Hwmon must reuse the 5 s HeapCapSnapshot instead of sampling heap twice');
  }
}
if (!bleLibrary.includes('BLE_CONNECT_TIMEOUT_MS') ||
    !bleLibrary.includes('SCALE_CONNECT_ATTEMPTS') ||
    !bleLibrary.includes('ConnectStep::Settle') ||
    !bleLibrary.includes('rememberPeripheral(peripheral)') ||
    !bleLibrary.includes('_connectAttempts < SCALE_CONNECT_ATTEMPTS')) {
  throw new Error(
      'GAP connect must settle after stopScan, use a longer timeout, and retry');
}
if (/#define\s+BLE_CONNECT_TIMEOUT_MS\s+4000UL/.test(bleLibrary)) {
  throw new Error(
      'GAP connect timeout must stay under the 5 s task watchdog (not 4 s)');
}
{
  const pollScanStart = bleLibrary.indexOf('bool AcaiaArduinoBLE::pollScan()');
  const pollScanEnd = bleLibrary.indexOf('bool AcaiaArduinoBLE::beginConnection', pollScanStart);
  const pollScan = pollScanStart >= 0 && pollScanEnd > pollScanStart
      ? bleLibrary.slice(pollScanStart, pollScanEnd)
      : '';
  if (!pollScan.includes('char mac[ACAIA_MAC_CAPACITY]') ||
      !pollScan.includes('char name[ACAIA_NAME_CAPACITY]') ||
      !pollScan.includes('isScaleName(name)') ||
      (pollScan.match(/peripheral\.address\(\)/g) || []).length > 1 ||
      (pollScan.match(/peripheral\.localName\(\)/g) || []).length > 1) {
    throw new Error(
        'pollScan must capture MAC/name once into C buffers and reuse them');
  }
}
{
  const nwStart = bleLibrary.indexOf('bool AcaiaArduinoBLE::newWeightAvailable()');
  const nwEnd = bleLibrary.indexOf('bool AcaiaArduinoBLE::supportedPacketLength', nwStart);
  const nw = nwStart >= 0 && nwEnd > nwStart ? bleLibrary.slice(nwStart, nwEnd) : '';
  if (!bleLibrary.includes('bool AcaiaArduinoBLE::isLinkUp()') ||
      !nw.includes('if (!_connected)') ||
      nw.includes('isConnected()')) {
    throw new Error(
        'Acaia newWeightAvailable must use the local link flag, not a live GAP isConnected()');
  }
}
if (firmwareCore.includes('if (scaleLinked || changed)')) {
  throw new Error(
      'Companion status publish must not force every tick while the scale is linked');
}
if (!firmwareCore.includes('companionAdvertisingShouldPause') ||
    !firmwareCore.includes('syncCompanionAdvertisingForScaleLink') ||
    !firmwareCore.includes('scale.isConnecting()') ||
    !firmwareCore.includes('BLE.poll(tickDelayMs)') ||
    firmwareCore.includes('vTaskDelay(pdMS_TO_TICKS(scaleWorkerTickDelayMs()))') ||
    (firmwareCore.split('syncCompanionAdvertisingForScaleLink();').length - 1) < 3 ||
    !bleCompanion.includes('advertisingPaused_ && !status_.connected')) {
  throw new Error(
      'Companion advertising must pause while connecting or scale-linked; worker must block on HCI');
}
if (!network.includes('\\"lastDisconnectReasonName\\":\\"%s\\"},') ||
    !network.includes('\\"macCachePauseRemainingMs\\":%lu,')) {
  throw new Error(
      'Home status must include lastDisconnectReasonName on scale');
}

const htmlMatch = asset.match(/R"HTML\(([\s\S]*?)\)HTML"/);
if (!htmlMatch) throw new Error('Embedded HTML raw string not found');
const shellHtml = htmlMatch[1];
const VIEW_NAMES = webUi.VIEW_NAMES;
const partialHtml = {};
for (const name of VIEW_NAMES) {
  partialHtml[name] = fs.readFileSync(
      path.join(sketchDir, 'web', 'html', name + '.html'), 'utf8');
}
const allHtml = shellHtml.replace(
    /<section id="view-([a-z]+)" class="view" data-view="\1"><\/section>/g,
    (_, name) => {
      if (!partialHtml[name]) {
        throw new Error('Missing partial for shell placeholder: ' + name);
      }
      return `<section id="view-${name}" class="view" data-view="${name}">${
          partialHtml[name]}</section>`;
    });
const appJsSource = fs.readFileSync(path.join(sketchDir, 'web', 'app.js'), 'utf8');
const runtimeJs = fs.readFileSync(path.join(sketchDir, 'web', 'js', 'runtime.js'), 'utf8');
const viewJs = {};
for (const name of VIEW_NAMES) {
  viewJs[name] = fs.readFileSync(
      path.join(sketchDir, 'web', 'js', name + '.js'), 'utf8');
}
const allJs = [appJsSource, runtimeJs, ...VIEW_NAMES.map((n) => viewJs[n])].join('\n');
const css = fs.readFileSync(path.join(sketchDir, 'web', 'app.css'), 'utf8');
// Most wiring checks look across shell + partials + all JS modules.
const html = allHtml;
const js = allJs;
const ui = allHtml + '\n' + allJs;
if (/<details\b[^>]*\bopen\b/i.test(allHtml)) {
  throw new Error('All collapsible <details> groups must start collapsed (no open attribute)');
}
if (css.includes('.brandLogo') || allHtml.includes('logo.svg') || allHtml.includes('brandLogo')) {
  throw new Error('Web UI must not embed a logo asset or .brandLogo styles');
}
if (!css.includes('.brand') || !css.includes('inline-flex') ||
    !css.includes('.brandMark') || !shellHtml.includes('class="brandMark"') ||
    !shellHtml.includes('<svg') || !shellHtml.includes('<small>Advanced</small>Shot Stopper') ||
    shellHtml.includes('logo.svg')) {
  throw new Error('Brand lockup must use inline SVG mark plus HTML wordmark');
}
if (!shellHtml.includes('class="pageNav"') ||
    !shellHtml.includes('id="navToggle"') ||
    shellHtml.indexOf('class="pageNav"') > shellHtml.indexOf('id="app"') ||
    shellHtml.indexOf('class="topBar"') > shellHtml.indexOf('class="pageNav"') ||
    !css.includes('@media(min-width:700px)') ||
    !css.includes('.navToggle{display:none}') ||
    !appJsSource.includes("matchMedia('(min-width: 700px)')")) {
  throw new Error('Desktop Web UI must show a top nav instead of the hamburger');
}
if (!shellHtml.includes('type="module"') ||
    !shellHtml.includes('src="/app.js?v=__FW_VERSION__"') ||
    /<script(?![^>]*\bsrc=)[^>]*>\s*\S/i.test(shellHtml)) {
  throw new Error('Web UI must load same-origin /app.js as a module (no inline script body)');
}
for (const name of VIEW_NAMES) {
  if (!shellHtml.includes('id="view-' + name + '"') ||
      !shellHtml.includes('data-view="' + name + '"') ||
      !shellHtml.includes(`<section id="view-${name}" class="view" data-view="${name}"></section>`)) {
    throw new Error('Shell must keep empty placeholder for view: ' + name);
  }
}

const htmlBytes = Buffer.byteLength(allHtml, 'utf8');
const jsBytes = Buffer.byteLength(allJs, 'utf8');
if (htmlBytes > 51650) {
  throw new Error('Web UI HTML source exceeds the authoring budget');
}
if (jsBytes > 131500) {
  throw new Error('Web UI JS source exceeds the authoring budget');
}
if (htmlBytes + jsBytes > 183500) {
  throw new Error('Web UI HTML+JS source exceeds the combined authoring budget');
}
if (!/lang="en"/.test(html) || !ui.includes('role="switch"') ||
    !ui.includes('id="dActivator"') || !ui.includes('firstDropBeep') ||
    !ui.includes('paddleReturnReminderBeep') ||
    !ui.includes('buzzerScaleLostBeep') ||
    !ui.includes('buzzerAutoToManualGuardEndBeep') ||
    !ui.includes('buzzerManualNoScaleBeep') ||
    !ui.includes('buzzerScaleConnectedBeep') ||
    !ui.includes('scaleConnectedLed') ||
    !ui.includes('buzzerExtendedPulseRate') ||
    !ui.includes('buzzerSlowExtendedPulseRate') ||
    !html.includes('id="buzzerExtendedPulseRate"') ||
    !html.includes('id="buzzerSlowExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Extended shot pulse<select id="buzzerExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Slow extended pulse<select id="buzzerSlowExtendedPulseRate"') ||
    !css.includes('color-scheme:light dark') ||
    !css.includes('html,input,select,textarea{color-scheme:dark}') ||
    !css.includes('input[type=text],input[type=password]{-webkit-appearance:none;appearance:none}') ||
    !css.includes('min-height:2.5rem') ||
    !css.includes('background:var(--bg)') ||
    !css.includes('input:-webkit-autofill') ||
    !css.includes('-webkit-box-shadow:0 0 0 2.5rem var(--bg) inset') ||
    css.includes('input[type=text],input[type=password],select{-webkit-appearance:none') ||
    html.includes('id="staSsid" type="number"') ||
    html.includes('id="ntpServerCustom" type="number"') ||
    !html.includes('id="staSsid" type="text" maxlength="32" autocomplete="off"') ||
    !html.includes('id="ntpServerCustom" type="text" maxlength="63" placeholder="e.g. ntp.example.com" autocomplete="off"') ||
    !html.includes('> Scale lost<small class="fieldHint">Local buzzer when the scale disconnects') ||
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
    !html.includes('id="scaleConnectedLed" type="checkbox" checked') ||
    !html.includes('Blue LED while scale connected') ||
    !html.includes('</div><label><input id="scaleConnectedLed"') ||
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
    !ui.includes('validateDevicePasswordClient') ||
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
    !network.includes('"scaleConnectedLed"') ||
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
    !network.includes('WEB_UI_ETAG') ||
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
    !network.includes('"avoidAccidentalTouchEnabled"') ||
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
    !html.includes('id="staSsid" type="text"') ||
    !html.includes('id="ntpServerCustom" type="text"') ||
    !html.includes('id="presetRenameInput" type="text"') ||
    !ui.includes('id="syncTimeButton"') ||
    !ui.includes('id="autoRetare"') ||
    !ui.includes('id="fastExtractionGuardEnabled"') ||
    !ui.includes('id="avoidAccidentalTouchEnabled"') ||
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
    ui.includes('Up to 120 shots') ||
    !ui.includes('/api/v1/time/sync') ||
    !firmware.includes('session.config.firstDropBeep') ||
    !firmware.includes('candidate.buzzerScaleConnectedBeep') ||
    !firmware.includes('candidate.scaleConnectedLed') ||
    !firmware.includes('candidate.buzzerSlowExtendedPulseRate') ||
    !firmware.includes('localBuzzer') ||
    !firmware.includes('BUZZER_SUPPORT_ENABLED') ||
    !firmware.includes('BUZZER_GPIO') ||
    !firmware.includes('machineServiceReminders')) {
  throw new Error('Scale beep settings must be configurable end-to-end');
}
if (firmware.includes('SHOT_STOPPER_ENABLE_ALED') ||
    firmware.includes('WS2812') ||
    firmware.includes('rgbLedWrite') ||
    firmware.includes('status_indicator') ||
    domain.includes('SHOT_STOPPER_ENABLE_ALED') ||
    domain.includes('BOOT_SUBSYSTEM_INDICATORS')) {
  throw new Error('WS2812B/ALED support must be fully removed');
}
if (!ui.includes("scaleConnectedLed:$('scaleConnectedLed').checked") ||
    !ui.includes("'scaleConnectedLed'") ||
    !network.includes('\\"scaleConnectedLed\\":%s') ||
    !firmware.includes('serviceScaleConnectedLed') ||
    !firmware.includes('SCALE_CONNECTED_LED_GPIO') ||
    !domain.includes('bool scaleConnectedLed = true')) {
  throw new Error('Scale-connected GPIO LED must be wired through Settings, status/settings, and firmware');
}

if (!ui.includes('id="soundAlertsEnabled"') ||
    ui.includes('id="homeSoundAlertsEnabled"') ||
    ui.includes('id="homeAlertsSub"') ||
    ui.includes("setHomeSub('homeAlertsSub'") ||
    ui.includes('function formatAlertsChannel(') ||
    ui.includes("persistHomeGuard('homeSoundAlertsEnabled'") ||
    !ui.includes('soundAlertsEnabled:$(\'soundAlertsEnabled\').checked') ||
    ui.includes('p.soundAlertsEnabled=$(\'homeSoundAlertsEnabled\').checked') ||
    ui.includes("k!=='soundAlertsEnabled'") ||
    js.includes("keys[0]==='soundAlertsEnabled'") ||
    !ui.includes("typeof c.soundAlertsEnabled==='boolean'")) {
  throw new Error('Sound alerts must be controlled from Settings, not Home Quick Settings');
}

if (html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('<summary>Cup protection</summary>') ||
    html.indexOf('<summary>Cup protection</summary>') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !ui.includes('id="cupProtectionEnabled"') ||
    html.indexOf('id="cupProtectionEnabled"') >
        html.indexOf('id="stopIfCupRemoved"') ||
    html.indexOf('id="stopIfCupRemoved"') >
        html.indexOf('id="requireCupToStart"') ||
    html.indexOf('id="requireCupToStart"') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !ui.includes('id="stopIfCupRemoved"') ||
    !ui.includes('id="requireCupToStart"') ||
    ui.includes('id="cupPresentWeightG"') ||
    html.includes('id="cupPresentWeightG"') ||
    html.indexOf('id="cupRemovedWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.includes('id="requireCupToStart" type="checkbox" checked') ||
    !ui.includes('Enable cup protection') ||
    !ui.includes('cupProtectOpt') ||
    !ui.includes('place the cup after connect so presence can be detected.') ||
    !ui.includes('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeAvoidAccidentalTouchEnabled"') >
        html.indexOf('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeCupProtectionEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    !ui.includes("persistHomeGuard('homeCupProtectionEnabled'") ||
    !ui.includes("'cupProtectionEnabled',1)") ||
    !ui.includes('cupProtectionEnabled:$(\'cupProtectionEnabled\')') ||
    !ui.includes('stopIfCupRemoved:$(\'stopIfCupRemoved\')') ||
    !ui.includes('requireCupToStart:$(\'requireCupToStart\')') ||
    !network.includes('cupProtectionEnabled') ||
    !network.includes('stopIfCupRemoved') ||
    !network.includes('requireCupToStart') ||
    !network.includes('cupRemovedWeightG')) {
  throw new Error('Cup protection master must precede Stop if cup is removed and Require cup to start; Home mirrors the master after accidental touch');
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
    !domain.includes('SHOT_STOPPER_ENABLE_BUZZER must be 0 (off) or 1 (passive RTTTL)') ||
    !domain.includes('compiledBuzzerModeId') ||
    !domain.includes('DEFAULT_ALERT_OUTPUT_CHANNEL') ||
    !domain.includes(
        'BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY') ||
    !domain.includes('static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL)') ||
    !buzzer.includes('requestTone') ||
    !buzzerPassive.includes('ledcAttach') ||
    !buzzerPassive.includes('parseRtttl') ||
    !buzzerPatterns.includes('BUZZER_ECHO_INVERTED_NOTES') ||
    buzzerPatterns.includes('buzzerActiveSetTone') ||
    !buzzerRtttl.includes('RTTTL_TARE') ||
    !buzzerRtttl.includes('RTTTL_START_TIMER') ||
    !buzzerRtttl.includes('RTTTL_STOP_TIMER') ||
    !buzzerRtttl.includes('RTTTL_TARE_START') ||
    !buzzerRtttl.includes('RTTTL_FIRST_DROP') ||
    !buzzerRtttl.includes('RTTTL_PADDLE_OFF') ||
    !buzzerRtttl.includes('RTTTL_SHOT_END') ||
    !buzzerRtttl.includes('RTTTL_SCALE_CONNECTED') ||
    !buzzerRtttl.includes('RTTTL_NO_CUP') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_SLOW') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_MEDIUM') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_FAST') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_RAPID') ||
    buzzerRtttl.includes('ShotStopperDomain.h') ||
    buzzerRtttl.includes('rtttlForExtendedPulseRate') ||
    buzzerPassive.includes('ShotStopperBuzzerRtttl.h') ||
    !buzzerRtttl.includes('RTTTL_SCALE_LOST') ||
    !buzzerRtttl.includes('RTTTL_GUARD_STOP') ||
    !buzzerRtttl.includes('RTTTL_NO_SCALE') ||
    !buzzerRtttl.includes('RTTTL_RECOVERY_START') ||
    !buzzerRtttl.includes('RTTTL_NETWORK_RESET_OK') ||
    !buzzerRtttl.includes('RTTTL_FACTORY_RESET_OK') ||
    !buzzerRtttl.includes('RTTTL_RECOVERY_ERROR') ||
    !alertChannel.includes('selectAlertSink') ||
    !alertChannel.includes('AlertKind::Recovery') ||
    !alertChannel.includes('AlertKind::CommandImmediate') ||
    !alertChannel.includes('AlertKind::CommandFallback') ||
    !alertTone.includes('deriveBuzzerTone') ||
    !alertTone.includes('buzzerCueForAlertEvent') ||
    !alertTone.includes('rtttlForExtendedPulseRate') ||
    alertTone.includes('buzzerPatternForCue') ||
    !buzzer.includes('startRtttl') ||
    buzzer.includes('BUZZER_ACTIVE_DRIVE') ||
    !buzzer.includes('memcpy(rtttlBuf') ||
    !buzzer.includes('stopExtendedPulse') ||
    !domain.includes('ECHO_INVERTED') ||
    !firmware.includes('startExtendedPulseTrain') ||
    !firmware.includes('startPulseTrain') ||
    !firmware.includes('stopPulseTrains') ||
    !firmware.includes('serviceExtendedPulseAlert') ||
    firmwareCore.includes('rtttlForExtendedPulseRate') ||
    firmwareCore.includes('BuzzerCue::ABNORMAL') ||
    !domain.includes('DEFAULT_EXTENDED_PULSE_RATE') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)') ||
    !kconfig.includes('0=off, 1=passive RTTTL')) {
  throw new Error('Local buzzer must be compile-time off (0) or passive RTTTL (1)');
}
if (!domainCore.includes('#ifndef SHOT_STOPPER_DEVELOPMENT') ||
    !domainCore.includes('#define SHOT_STOPPER_DEVELOPMENT 0') ||
    !domainCore.includes('DEVELOPMENT_BUILD = SHOT_STOPPER_DEVELOPMENT == 1') ||
    !domainCore.includes('SHOT_STOPPER_DEVELOPMENT must be 0 or 1') ||
    !domainCore.includes('CONFIG_SHOT_STOPPER_DEVELOPMENT') ||
    !kconfig.includes('config SHOT_STOPPER_DEVELOPMENT') ||
    !kconfig.includes('bypass WebUI admin unlock') ||
    !network.includes('SHOT_STOPPER_DEVELOPMENT == 1') ||
    !network.includes('adminUnlockAllowed') ||
    (network.match(/\\"development\\":%s/g) || []).length < 4 ||
    !js.includes('developmentMode') ||
    !js.includes("'development'in s") ||
    !js.includes('||developmentMode')) {
  throw new Error(
      'SHOT_STOPPER_DEVELOPMENT must default off, bypass adminUnlockAllowed when on, and unlock Web UI');
}
if (ui.includes('authenticatedOnly') ||
    ui.includes("s.setItem('shotStopperToken'") ||
    ui.includes('pageNav authenticatedOnly') ||
    ui.includes("authenticated()&&known") ||
    !ui.includes('function knownPath(') ||
    !ui.includes('class="brand"') ||
    !ui.includes('class="brandMark"') ||
    !ui.includes('<small>Advanced</small>Shot Stopper') ||
    !ui.includes('href="/" data-route="/"') ||
    !ui.includes("querySelectorAll('a[data-route]')") ||
    !ui.includes('ensureView') ||
    !ui.includes('/partials/') ||
    !network.includes('HTTPD_404_NOT_FOUND') ||
    !network.includes('notFoundHandler')) {
  throw new Error('Web UI must expose public SPA routes and redirect unknown paths to /');
}
if (html.includes('id="rememberMe"') ||
    js.includes('rememberMe:r') ||
    js.includes("s.setItem('shotStopperToken'") ||
    js.includes('function clearAuth()') ||
    network.includes('jsonBoolean(root, "rememberMe", rememberMe)') ||
    network.includes('createSession(token, csrf, rememberMe)') ||
    network.includes('uiAuthenticated') ||
    networkHeader.includes('SESSION_REMEMBER_MS')) {
  throw new Error(
      'Web UI must not use login tokens; exclusive WebUI claim owns the session');
}
const statusSection = html.match(/<fieldset[^>]*id="statusPanel"[^>]*><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/) ||
    html.match(/<fieldset id="statusPanel"><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/);
const scaleSection = html.match(/<fieldset[^>]*id="scalePanel"[^>]*><legend>Scale<\/legend>([\s\S]*?)<\/fieldset>/) ||
    html.match(/<fieldset id="scalePanel"><legend>Scale<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statusSection || !statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric"/g) || []).length !== 3 ||
    !statusSection[1].includes('<strong>Machine</strong>') ||
    !statusSection[1].includes('<strong>Brew</strong>') ||
    !statusSection[1].includes('<strong>Cup</strong>') ||
    statusSection[1].includes('data-label="Machine"') ||
    !statusSection[1].includes('id="machineState"') ||
    !statusSection[1].includes('id="state"') ||
    !statusSection[1].includes('id="cupState"') ||
    statusSection[1].includes('id="paddle"') ||
    statusSection[1].includes('id="relay"') ||
    statusSection[1].includes('id="safety"') ||
    statusSection[1].includes('id="statusExtractionGuard"') ||
    !scaleSection || !scaleSection[1].includes('class="statusColumn"') ||
    (scaleSection[1].match(/class="metric"/g) || []).length !== 4 ||
    !scaleSection[1].includes('<strong>Status</strong>') ||
    !scaleSection[1].includes('<strong>Preferred</strong>') ||
    !scaleSection[1].includes('<strong>Weight</strong>') ||
    !scaleSection[1].includes('<strong>Timer</strong>') ||
    scaleSection[1].includes('data-label="Status"') ||
    !scaleSection[1].includes('id="scale"') ||
    !scaleSection[1].includes('id="preferredScale"') ||
    !scaleSection[1].includes('id="scaleWeight"') ||
    !scaleSection[1].includes('id="scaleTimer"') ||
    !ui.includes("s.physicalActivatorOn?'ON':'OFF'") ||
    !ui.includes("s.relayClosed?'ON':'OFF'") ||
    !ui.includes('function formatScaleWeight(') ||
    !ui.includes('function formatScaleStatus(') ||
    !ui.includes('function formatScaleTimer(') ||
    !ui.includes('function formatMachineState(') ||
    !ui.includes("CONFIRMED_OFF:'Idle'") ||
    !ui.includes("ASSUMED_ON:'Assumed on'") ||
    !ui.includes("CONFIRMED_ON:'Confirmed on'") ||
    !ui.includes("ASSUMED_OFF:'Assumed off'") ||
    !ui.includes('function formatCupState(') ||
    !ui.includes('lastDisconnectReasonName') ||
    !ui.includes("'Stale'") ||
    !ui.includes("'No sample'") ||
    !ui.includes('formatScaleStatus(s)') ||
    !ui.includes('id="preferredScale"') ||
    !ui.includes('id="preferredScaleSelect"') ||
    !ui.includes('id="preferredScalePauseHint"') ||
    !ui.includes('id="scalePreference"') ||
    !ui.includes('id="forgetPairedScale"') ||
    !ui.includes('Scale preference') ||
    !ui.includes('First available') ||
    !ui.includes('Prefer selected') ||
    !ui.includes('Preferred only') ||
    !ui.includes('Preferred scale') ||
    !ui.includes('Clear preferred') ||
    !ui.includes('scaleMacCacheMode') ||
    !ui.includes('/api/v1/scale/preferred/clear') ||
    !ui.includes('/api/v1/scale/preferred/select') ||
    !ui.includes('function formatPreferredScale(') ||
    !ui.includes('function updatePreferredScaleSelect(') ||
    !ui.includes('function updateScalePreferenceOptions(') ||
    // Regression: missing ';' after `prev` concatenated into
    // `prevupdateScalePreferenceOptions` and broke Settings status refresh.
    ui.includes(':prevupdateScalePreferenceOptions') ||
    !ui.includes(':prev;updateScalePreferenceOptions()') ||
    !ui.includes('preferredScaleSelectSyncing') ||
    !ui.includes("mac===(sel.dataset.applied||'')") ||
    !ui.includes("scaleMacCacheMode:(()=>{const el=$('scalePreference')") ||
    !ui.includes("el.id==='preferredScaleSelect'") ||
    ui.includes('id="alwaysUseThisScale"') ||
    ui.includes('Always use this scale') ||
    !ui.includes('function selectPreferredScale(') ||
    !ui.includes('function forgetPairedScale(') ||
    !ui.includes('Saved scale history is kept') ||
    !ui.includes('formatPreferredScale(s)') ||
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
    !network.includes('Scale preference must be first, prefer, or only.') ||
    network.includes('Always use this scale must be on or off.') ||
    network.includes('scaleMacCacheMode must be disabled or full.') ||
    !network.includes('scaleMacCacheMode must be first, prefer, or only.') ||
    !network.includes('The paired scale cannot be forgotten while a cycle') ||
    !network.includes('\\"history\\"') ||
    network.includes('Preferred scale cache cannot be cleared') ||
    !network.includes('\\"timerMs\\"')) {
  throw new Error('Home Status must show Machine/Brew/Cup and a Scale panel with one value per label');
}
if (!ui.includes('id="shotPanel"') ||
    !ui.includes('id="shotBar"') ||
    !ui.includes('id="shotBarFast"') ||
    !ui.includes('id="shotBarTicks"') ||
    !partialHtml.home.includes('id="shotBarTicks"') ||
    !partialHtml.home.includes('<legend>Last/Current shot</legend>') ||
    !partialHtml.home.includes('class="ruleChartLabel">Weight (g)</div>') ||
    partialHtml.home.includes('id="shotIdle"') ||
    css.includes('content:"Weight (g)"') ||
    css.includes('#shotIdle') ||
    ui.includes('shotMark') ||
    !ui.includes('Math.max(goal,wt)') ||
    !css.includes('.shotTrack{position:relative;height:1rem;background:var(--ln);border-radius:.5rem;overflow:hidden}') ||
    !css.includes('.shotTrack #shotBarFast{background:#d97706}') ||
    css.includes('.shotMark') ||
    css.includes('max-width:150%') ||
    !ui.includes('id="shotCard"') ||
    !html.includes('id="shotSparkHost"') ||
    !css.includes('.shotSparkHost') ||
    !css.includes('.shotSpark{') ||
    !css.includes('.shotSparkY{') ||
    !css.includes('.shotSparkHost .ruleChartTicks') ||
    !css.includes('.shotSparkHost[hidden]') ||
    !css.includes('#shotPanel .shotSparkHost{min-height:4.05rem;margin:.55rem 0 .1rem') ||
    !css.includes('#shotPanel{position:relative;padding-right:3.4rem') ||
    !css.includes('#shotPanel .shotDel{top:.35rem;right:.35rem') ||
    !css.includes('#shotTable tr.noSpark{') ||
    !css.includes('.shotSpark{grid-area:plot;display:block;width:100%;height:100%;color:var(--ok);overflow:visible}') ||
    !ui.includes('function renderShotSpark(') ||
    !runtimeJs.includes('function buildShotSparkModel(') ||
    !runtimeJs.includes('function axisLabel(') ||
    !runtimeJs.includes('function fillChartTicks(') ||
    runtimeJs.includes('s[a=') ||
    !runtimeJs.includes('style.left=') ||
    runtimeJs.includes("style=\"left:") ||
    !runtimeJs.includes('function shotDisplayFlowGS(') ||
    !runtimeJs.includes('lastCurveWeightG(w)===null') ||
    !runtimeJs.includes('model.firstDropS>0&&dur>0') ||
    !runtimeJs.includes("'1st '+L(") ||
    !runtimeJs.includes('fillChartTicks($(\'shotBarTicks\')') ||
    !runtimeJs.includes('raw.sort(') ||
    runtimeJs.includes('shotIdle') ||
    runtimeJs.includes("last?'Last shot.'") ||
    !runtimeJs.includes('stroke="\'+cN+\'"') ||
    runtimeJs.includes('stroke="currentColor"') ||
    !runtimeJs.includes("if(spark.hidden)row.classList.add('noSpark')") ||
    !css.includes('.shotCard{') ||
    !css.includes('.metric,.shotCard > *{') ||
    !css.includes('.metric strong,.shotCard strong{') ||
    !css.includes('.metric > div,.shotCard > * > div,.swS{') ||
    css.includes('#diagnosticsPanel .metric,#statusPanel .metric,#scalePanel .metric,.shotCard > *{') ||
    css.includes('#statusPanel .metric::before,#scalePanel .metric::before,.shotCard > *::before{') ||
    css.includes('font-size:1rem;font-weight:700;color:var(--mu)') ||
    !css.includes('.shotCard .shotDur > div,.shotCard .shotActual > div') ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "goal goal flow flow drop drop" "err err shot shot ended ended"') ||
    !ui.includes('id="shotElapsed"') ||
    !ui.includes('id="shotFirstDrop"') ||
    !ui.includes('id="shotCurrentWeight"') ||
    !partialHtml.home.includes('<strong>Weight</strong>') ||
    !partialHtml.home.includes('<strong>Dur</strong>') ||
    partialHtml.home.includes('data-label=') ||
    html.includes('data-label="Actual"') ||
    !ui.includes('id="shotGoalWeight"') ||
    !ui.includes('id="shotErr"') ||
    !ui.includes('id="shotFlow"') ||
    !ui.includes('id="shotEnded"') ||
    !ui.includes('id="shotType"') ||
    ui.includes('id="shotRetare"') ||
    ui.includes('id="shotScale"') ||
    ui.includes('id="shotGuard"') ||
    ui.includes('id="shotPct"') ||
    ui.includes('class="shotHero"') ||
    !ui.includes('function updateShot(') ||
    !network.includes('firstDropElapsedMs') ||
    !network.includes('retarePerformed') ||
    !network.includes('shotType') ||
    !network.includes('scaleProtocol') ||
    !network.includes('safeScaleProtocol') ||
    !ui.includes('remoteReady&&relayStartReady&&canControl') ||
    !ui.includes('Remote machine control disabled by policy') ||
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
    !html.includes('Added after the scale timer catches up to circuit whole seconds') ||
    html.includes('Added after measured scale start lag') ||
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
    !ui.includes('id="homeAtmSub"') ||
    !ui.includes('id="homeNoScaleSub"') ||
    !ui.includes('id="avoidBbwShotWithoutScale"') ||
    !ui.includes('id="lastShotCooldownMin"') ||
    !ui.includes('Avoid BBW shot without scale') ||
    !ui.includes('Last shot cooldown') ||
    !ui.includes('function formatNoScaleGuard(') ||
    !ui.includes('function formatSlowExtractionGuard(') ||
    !ui.includes("setHomeSub('homeSlowSub',formatSlowExtractionGuard(") ||
    !ui.includes('function updateStatusGuards(') ||
    ui.includes('function updateNoScaleGuard(') ||
    html.includes('id="shotAtmGuard"') ||
    html.includes('id="shotNoScaleGuard"') ||
    html.includes('id="statusExtractionGuard"') ||
    html.includes('id="statusSlowExtractionGuard"') ||
    html.includes('id="statusAtmGuard"') ||
    html.includes('id="statusNoScaleGuard"') ||
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
    !network.includes('cupPresence') ||
    !network.includes('control.cupPresent') ||
    !network.includes('machineState') ||
    !network.includes('machineRunStateName') ||
    !network.includes('cupPresenceStateName') ||
    !firmware.includes('last.noScaleShotGuardEnabled') ||
    !firmware.includes('last.noScaleShotGuardArmed') ||
    !firmware.includes('next.cupPresent') ||
    !firmware.includes('next.machineRunState') ||
    !firmware.includes('next.cupPresenceState') ||
    !firmware.includes('cupPresenceState() == CupPresenceState::PRESENT') ||
    !domain.includes('bool cupPresent = false') ||
    !domain.includes('MachineRunState machineRunState') ||
    !domain.includes('CupPresenceState cupPresenceState') ||
    !ui.includes('A→M ·') ||
    !ui.includes('function updateHomeGuardSubs(') ||
    !ui.includes('updateHomeGuardSubs(s,live)') ||
    !ui.includes("setHomeSub('homeBbwSub'") ||
    !ui.includes("setHomeSub('homeCupSub'") ||
    ui.includes("setHomeSub('homeAlertsSub'") ||
    !ui.includes('function formatCupProtection(') ||
    ui.includes('function formatAlertsChannel(') ||
    !ui.includes("Can't brew — no cup") ||
    !ui.includes('Shot aborted') ||
    !ui.includes('Brew allowed') ||
    !ui.includes("c.state==='PRESENT'||c.present?'Present':'Absent'") ||
    !network.includes('endReasonName(control.lastShot.endReason)') ||
    !html.includes('option value="scale_priority">Scale priority') ||
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
    !network.includes('AUTO_TO_MANUAL_GUARD') ||
    !network.includes('UNCONFIRMED_START') ||
    !network.includes('cupProtectionEnabled') ||
    !network.includes('stopIfCupRemoved') ||
    !network.includes('requireCupToStart') ||
    !network.includes('cupRemovedWeightG') ||
    !ui.includes('cupProtectionEnabled:$(\'cupProtectionEnabled\')') ||
    !ui.includes('stopIfCupRemoved:$(\'stopIfCupRemoved\')') ||
    !ui.includes('requireCupToStart:$(\'requireCupToStart\')') ||
    !ui.includes("cupRemovedWeightG:number('cupRemovedWeightG')")) {
  throw new Error('Auto-to-manual time guard must be wired in config UI, live panel, shots API, and routes');
}
if (!html.includes('<summary>Paddle</summary>') ||
    !html.includes('id="paddleMode"') ||
    !html.includes('<option value="auto">Auto</option>') ||
    !html.includes('<option value="natural">Natural</option>') ||
    !html.includes('<option value="original">Original</option>') ||
    html.indexOf('<option value="auto">Auto</option>') >
        html.indexOf('<option value="natural">Natural</option>') ||
    html.indexOf('<option value="natural">Natural</option>') >
        html.indexOf('<option value="original">Original</option>') ||
    !html.includes('<strong>Natural:</strong>') ||
    !html.includes('<strong>Original:</strong>') ||
    !html.includes('<strong>Auto:</strong>') ||
    html.indexOf('<strong>Natural:</strong>') >
        html.indexOf('<strong>Original:</strong>') ||
    html.indexOf('<strong>Original:</strong>') >
        html.indexOf('<strong>Auto:</strong>') ||
    !html.includes('like a normal brew switch') ||
    !html.includes('original Tater Mazer Shot Stopper') ||
    !html.includes('move the paddle ON during the shot') ||
    !html.includes('Do not press the scale') ||
    !html.includes('auto-natural') ||
    !html.includes('auto-original') ||
    !ui.includes("paddleMode:['auto','natural','original']") ||
    !ui.includes("if($('paddleMode'))$('paddleMode').value=") ||
    !network.includes('"paddleMode"') ||
    !network.includes('paddleMode must be auto, natural or original.') ||
    !network.includes('jsonPaddleMode') ||
    !firmware.includes('machineApplyWorkflowConfig') ||
    !firmwareCore.includes('machineApplyWorkflowConfig(candidate, command.config)') ||
    !firmware.includes('dst.paddleMode = src.paddleMode') ||
    firmwareCore.includes('candidate.paddleMode') ||
    !firmware.includes('machineHidesPhysicalStop()') ||
    !firmware.includes('machineAllowsAutomationStop()') ||
    !firmware.includes('machineBeginCycle') ||
    domain.includes('enum class PaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('enum class PaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('NATURAL = 0') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('ORIGINAL = 1') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('AUTO = 2') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('enum class PaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('parsePaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('enum class MachineType') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8')
         .includes('machinePollIntention()') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8')
        .includes('enum class PaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('PaddleMode::') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('paddleMode')) {
  throw new Error('Machine Paddle mode must expose Auto/Natural/Original in UI, API, and APPLY_CONFIG');
}
const brewHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8');
const scaleSenseHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleSense.h'), 'utf8');
const paddlePolicyHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8');
const machineTypesHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8');
const momentaryInputHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryInput.h'), 'utf8');
const momentaryConfigHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryConfig.h'), 'utf8');
const momentaryControlHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryControl.h'), 'utf8');
const momentaryOnlyHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryOnlyState.h'), 'utf8');
const relayHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineRelay.h'), 'utf8');
if (brewHeader.includes('machinePollIntention') ||
    brewHeader.includes('getScaleLinkSnapshot') ||
    brewHeader.includes('cupPresenceState(') ||
    brewHeader.includes('scaleAvailable(')) {
  throw new Error('Brew/guards must consume GuardInputs from the stopper — not poll machine/scale/cup');
}
if (scaleSenseHeader.includes('onFirstDropsDetected') ||
    scaleSenseHeader.includes('notifyCupPresenceTare') ||
    scaleSenseHeader.includes('holdCupPresenceTransitions')) {
  throw new Error('Scale sense must return events; the stopper applies brew/cup effects');
}
if (momentaryInputHeader.includes('session.active') ||
    momentaryConfigHeader.includes('session.active') ||
    momentaryControlHeader.includes('session.active') ||
    momentaryOnlyHeader.includes('session.active') ||
    momentaryOnlyHeader.includes('currentWeight') ||
    relayHeader.includes('session.automaticEnabled')) {
  throw new Error('Machine momentary/relay must not read session or live scale globals');
}
if (firmwareCore.includes('readRawActivatorOn()') ||
    firmwareCore.includes('pinMode(RELAY_GPIO') ||
    firmwareCore.includes('#if SHOT_STOPPER_MACHINE_TYPE') ||
    firmwareCore.includes('MACHINE_USES_MOMENTARY') ||
    firmwareCore.includes('stopPulseTenMs') ||
    firmwareCore.includes('maxSinglePressHundredMs') ||
    !firmware.includes('pinMode(RELAY_GPIO, OUTPUT)') ||
    !firmware.includes('machineBootActivatorHeldStably()') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineActivatorSample.h'), 'utf8')
         .includes('bool rawActivatorOn') ||
    firmwareCore.includes('bool rawActivatorOn')) {
  throw new Error(
      'Machine GPIO, activator sample BSS, and boot debounce must live in machine headers — not shotStopper.cpp');
}
if (!html.includes('<summary>Switch</summary>') ||
    !html.includes('id="stopPulseMs"') ||
    !html.includes('id="maxSinglePressMs"') ||
    !html.includes('id="momentaryStartEdge"') ||
    !html.includes('id="reedConfirmTimeoutS"') ||
    !html.includes('id="assumeIdleWhenScaleConnects"') ||
    !html.includes('id="shotReactTimeoutS"') ||
    !html.includes('class="reedOnly"') ||
    !html.includes('class="switchOnly"') ||
    !html.includes('class="cfgGroup momentaryOnly"') ||
    !html.includes('Auto-stop pulse') ||
    !html.includes('Single-press limit') ||
    !html.includes('Start/stop on') ||
    !html.includes('Reed confirm timeout') ||
    !html.includes('Button press') ||
    !html.includes('Button release') ||
    !html.includes('when the reed confirm window starts') ||
    !html.includes(
        'how long Assumed may disagree with the reed') ||
    !html.includes('Starts on press/release, not relay mirror') ||
    !html.includes('undone if the hold exceeds this limit') ||
    html.includes('<summary>Momentary</summary>') ||
    html.indexOf('<summary>Paddle</summary>') >
        html.indexOf('<summary>Switch</summary>') ||
    html.indexOf('<summary>Switch</summary>') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="stopPulseMs"') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    !html.includes(
        'mimic a single button press when it needs to stop the brew automatically') ||
    !ui.includes('stopPulseMs:number(') ||
    !ui.includes("if($('stopPulseMs'))$('stopPulseMs').value=") ||
    !ui.includes("'Auto-stop pulse'") ||
    !ui.includes("'Single-press limit'") ||
    !ui.includes("'Reed confirm timeout'") ||
    !ui.includes('momentaryStartEdge:') ||
    !ui.includes('reedConfirmTimeoutMs:') ||
    !ui.includes('assumeIdleWhenScaleConnects:') ||
    !ui.includes('shotReactTimeoutS:') ||
    !ui.includes('id="overrideIdleLink"') ||
    !ui.includes('id="overrideBrewingLink"') ||
    !ui.includes('id="machineStateValue"') ||
    !ui.includes('/api/v1/control/state-override') ||
    !ui.includes('updateHomeAdminActions') ||
    !ui.includes('function updateHomeAdminActions(unlocked){const panel=$(') ||
    !ui.includes('show=!!unlocked') ||
    ui.includes('id="overrideIdleButton"') ||
    ui.includes('id="overrideBrewingButton"') ||
    ui.includes("d.classList.contains('momentaryMachine')&&!d.classList.contains('reedMachine')") ||
    !html.includes('Assume idle when the scale connects') ||
    !html.includes('On connect with no brew, mark idle. No pulse.') ||
    !html.includes('Shot reaction timeout') ||
    !html.includes(
        'quiet pan to Assumed off in this many s. No pulse; late flow confirms ON.') ||
    !html.includes('Override idle') ||
    !html.includes('Override brewing') ||
    !css.includes('html.reedMachine .switchOnly') ||
    !css.includes('html:not(.momentaryMachine) .switchOnly') ||
    !css.includes('#machineStateValue .switchOnly') ||
    !css.includes('#machineStateValue .switchOnly,#machineStateValue a{font-size:.78rem;font-weight:400}') ||
    !css.includes('.momentaryOnly') ||
    !css.includes('html:not(.reedMachine) .reedOnly') ||
    !network.includes('"stopPulseMs"') ||
    !network.includes('"momentaryStartEdge"') ||
    !network.includes('"reedConfirmTimeoutMs"') ||
    !network.includes('stopPulseMs must be an integer from 50 to 1000.') ||
    !network.includes('maxSinglePressMs must be an integer from 100 to 5000.') ||
    !network.includes('momentaryStartEdge must be press or release.') ||
    !network.includes(
        'reedConfirmTimeoutMs must be an integer from 200 to 5000.') ||
    !network.includes(
        'Shot reaction timeout must be 0 (compiled default) or from 3 to 30 s.') ||
    !network.includes(
        'shotReactTimeoutS must be 0 (compiled default) or an integer from 3 to 30.') ||
    !network.includes('jsonStopPulseMs') ||
    !network.includes('jsonMomentaryStartEdge') ||
    !network.includes('jsonReedConfirmTimeoutMs') ||
    !network.includes('jsonAssumeIdleWhenScaleConnects') ||
    !network.includes('jsonShotReactTimeoutS') ||
    !network.includes('stateOverrideHandler') ||
    !network.includes('/api/v1/control/state-override') ||
    !firmware.includes('machineApplyWorkflowConfig') ||
    !firmware.includes('dst.stopPulseTenMs = src.stopPulseTenMs') ||
    !momentaryControlHeader.includes(
        'dst.momentaryStartOnPress = src.momentaryStartOnPress') ||
    !momentaryControlHeader.includes(
        'dst.reedConfirmTimeoutHundredMs = src.reedConfirmTimeoutHundredMs') ||
    !momentaryControlHeader.includes(
        'dst.assumeIdleWhenScaleConnects = src.assumeIdleWhenScaleConnects') ||
    !momentaryControlHeader.includes(
        'dst.shotReactTimeoutS = src.shotReactTimeoutS') ||
    paddlePolicyHeader.includes('momentaryStartOnPress') ||
    paddlePolicyHeader.includes('reedConfirmTimeoutHundredMs') ||
    paddlePolicyHeader.includes('assumeIdleWhenScaleConnects') ||
    paddlePolicyHeader.includes('shotReactTimeoutS') ||
    machineTypesHeader.includes('parseMomentaryStartEdge') ||
    machineTypesHeader.includes('momentaryStartEdgeId') ||
    machineTypesHeader.includes('PaddleMode') ||
    machineTypesHeader.includes('parsePaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('parsePaddleMode') ||
    !momentaryConfigHeader.includes('parseMomentaryStartEdge') ||
    !momentaryConfigHeader.includes('momentaryStartEdgeId') ||
    !domain.includes('uint8_t stopPulseTenMs') ||
    !domain.includes('bool momentaryStartOnPress') ||
    !domain.includes('setRuntimeStopPulseMs') ||
    !domain.includes('setRuntimeReedConfirmTimeoutMs') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8')
        .includes('stopPulseTenMs') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('stopPulseTenMs')) {
  throw new Error(
      'Momentary Switch timings must be wired in Settings, API, APPLY_CONFIG, and runtime config — not brew');
}
if (!html.includes('class="cfgGroup paddleOnly"><summary>Paddle</summary>') ||
    !html.includes('class="cfgGroup"><summary>Quick rinse</summary>') ||
    html.includes('class="cfgGroup paddleOnly"><summary>Quick rinse</summary>') ||
    !html.includes('id="rinseEnabled" type="checkbox"') ||
    html.indexOf('id="rinseEnabled"') >
        html.indexOf('id="rinseGestureS"') ||
    html.indexOf('<summary>Quick rinse</summary>') >
        html.indexOf('id="rinseEnabled"') ||
    !html.includes('Enable quick rinse') ||
    !html.includes('Lets you flush the group with a short paddle flip') ||
    !html.includes('Lets you flush the group with a long press from idle') ||
    !html.includes('How long to hold the switch from idle before a rinse starts') ||
    !html.includes('How long water runs through the group after a rinse starts') ||
    !html.includes('id="rinseButton" class="btnGlyph" title="Start rinse"') ||
    html.includes('id="rinseButton" class="btnGlyph paddleOnly"') ||
    !ui.includes('s.config.rinseEnabled===true') ||
    !ui.includes('rinseEnabled:$(\'rinseEnabled\').checked') ||
    !network.includes('"rinseEnabled"') ||
    !network.includes('rinseEnabled must be a boolean.') ||
    !firmwareCore.includes('candidate.rinseEnabled = command.config.rinseEnabled') ||
    !firmware.includes('uint32_t rinseBegin(') ||
    !firmware.includes('UserIntent::REQUEST_RINSE') ||
    !firmware.includes('machineBeginRinse') ||
    !html.includes('class="cfgGroup momentaryOnly"><summary>Switch</summary>') ||
    !html.includes('class="paddleOnly"><input id="paddleReturnReminderBeep"') ||
    html.includes('cfgGroup paddleOnly momentaryOnly') ||
    html.includes('cfgGroup momentaryOnly paddleOnly') ||
    !css.includes('html.momentaryMachine .paddleOnly') ||
    !css.includes('html:not(.momentaryMachine) .momentaryOnly') ||
    !css.includes('html:not(.reedMachine) .reedOnly') ||
    !ui.includes("classList.toggle('momentaryMachine',t!=='paddle')") ||
    !ui.includes("classList.toggle('reedMachine',t==='momentary_reed')")) {
  throw new Error(
      'Paddle and Momentary Settings groups must be mutually exclusive by compiled machine type; Quick rinse is shared');
}
const rinseHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperRinse.h'), 'utf8');
const brewTypesHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8');
if (rinseHeader.includes('session.') ||
    rinseHeader.includes('runtimeConfig') ||
    brewHeader.includes('runtimeConfig.rinseGestureMs') ||
    brewTypesHeader.includes('DEFAULT_RINSE_GESTURE_MS') ||
    brewTypesHeader.includes('DEFAULT_RINSE_DURATION_MS') ||
    brewTypesHeader.includes('ENTER_RINSE =') ||
    firmware.includes('machineRinseGestureMs') ||
    domainCore.includes('snapshot.rinseGestureMs') ||
    !firmwareCore.includes('session.rinseStartedAtMs = rinseBegin') ||
    !firmwareCore.includes(
        'blockedHoldTimeoutMs = runtimeConfig.rinseGestureMs') ||
    !brewHeader.includes('inputs.blockedHoldTimeoutMs')) {
  throw new Error(
      'Rinse clock must not write session; brew must not read rinseGestureMs; ShotStopper copies the accept anchor');
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
if (!html.includes('<summary>Cup</summary>') ||
    !html.includes('<summary>Tare</summary>') ||
    html.indexOf('<summary>Cup</summary>') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="minimumCupWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="minimumCupWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="retareStabilitySamples"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="retareStabilitySamples"') >
        html.indexOf('<summary>Tare</summary>') ||
    !html.includes('<summary>Scales</summary>') ||
    html.includes('<summary>Scale & retare</summary>') ||
    html.indexOf('<summary>Tare</summary>') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Scales</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="autoTare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="postTareBaselineGraceS"') <
        html.indexOf('id="autoTare"') ||
    html.indexOf('id="postTareBaselineGraceS"') >
        html.indexOf('id="autoRetare"') ||
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
    html.indexOf('id="scalePreference"') <
        html.indexOf('id="dripDelayS"') ||
    html.indexOf('id="scalePreference"') <
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
if (!html.includes('id="postTareBaselineGraceS" type="number" min="0.5" max="10" step="0.1"') ||
    !ui.includes("rangeCheck('postTareBaselineGraceS',0.5,10,'Post-tare grace',{unit:'s'})") ||
    !ui.includes("postTareBaselineGraceMs:sToMs('postTareBaselineGraceS')") ||
    !ui.includes("['postTareBaselineGraceS','postTareBaselineGraceMs']") ||
    !ui.includes("apply('tareOpt',!$('autoTare').checked)") ||
    !(ui.includes("$('autoTare').onchange=()=>{updateConfigGroups();markConfigDirty()}") ||
      ui.includes("$('autoTare').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()}")) ||
    !ui.includes("typeof c.postTareBaselineGraceMs==='number'") ||
    !network.includes('\\"postTareBaselineGraceMs\\":%lu') ||
    !network.includes('Post-tare grace must be from 0.5 to 10 s.') ||
    !network.includes('candidate.postTareBaselineGraceMs') ||
    !firmware.includes('session.config.postTareBaselineGraceMs')) {
  throw new Error('Post-tare grace must be wired through Tare settings, status/settings, and firmware');
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
if (/R\.(homeFlushConfig|homeFlushPreset|configLoaded|formRev|brewDirty)\s*=/.test(js) ||
    !js.includes('function persistHomeBrewByWeight(') ||
    !js.includes('function invalidateSettingsHydration(') ||
    !js.includes('function clearBrewDirty(')) {
  throw new Error(
      'View modules must call runtime helpers instead of assigning read-only ESM namespace exports');
}
if (!ui.includes('<legend>Brew</legend>') ||
    !ui.includes('<legend>Machine and scale</legend>') ||
    !ui.includes('<legend>Wi-Fi</legend>') ||
    !ui.includes('<legend>Device password</legend>') ||
    !ui.includes('id="presetCards"') ||
    !ui.includes('id="presetNewBtn"') ||
    !ui.includes('id="presetDupBtn"') ||
    ui.includes('id="presetLoadBtn"') ||
    ui.includes('id="presetSaveBtn"') ||
    !html.includes('id="saveBrewPresetButton" class="btnGlyph mutable btnInvert"') ||
    !html.includes('id="saveConfigButton" class="btnGlyph mutable btnInvert"') ||
    html.includes('id="exportShotsButton" class="btnGlyph btnInvert"') ||
    !css.includes('font-variant-emoji:text') ||
    !html.includes('<span class="g">×</span>') ||
    !html.includes('<span class="g">✓</span>') ||
    ui.includes('💾') || ui.includes('⚡') ||
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
        html.indexOf('<legend>Wi-Fi</legend>') ||
    !ui.includes('id="presetResetBtn"') ||
    !ui.includes('id="presetDeleteBtn"') ||
    !ui.includes('id="presetRenameDialog"') ||
    !ui.includes('id="homePresetCards"') ||
    !html.includes('id="homePresetLabel"') ||
    !html.includes('>Presets</p>') ||
    html.indexOf('id="homePresetLabel"') > html.indexOf('id="homePresetCards"') ||
    html.indexOf('id="homePresetBlock"') > html.indexOf('id="homePresetLabel"') ||
    !ui.includes('id="homeBrewByWeight"') ||
    !ui.includes('id="quickSettingsPanel"') ||
    !html.includes('class="homeSwitchGrid"') ||
    !html.includes('id="homeBbwSub"') ||
    !html.includes('id="homeNoScaleSub"') ||
    !html.includes('id="homeFastSub"') ||
    !html.includes('id="homeTouchSub"') ||
    !ui.includes('function formatAccidentalTouch(') ||
    !html.includes('id="homeSlowSub"') ||
    !html.includes('id="homeAtmSub"') ||
    !html.includes('id="homeCupSub"') ||
    html.includes('id="homeAlertsSub"') ||
    !html.includes('>Cup protection<span') ||
    html.includes('>Alerts<span') ||
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
        html.indexOf('id="homeAvoidAccidentalTouchEnabled"') ||
    html.indexOf('id="homeAvoidAccidentalTouchEnabled"') >
        html.indexOf('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeCupProtectionEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') > html.indexOf('id="shotPanel"') ||
    !html.includes('>No-scale BBW<span') ||
    !html.includes('>Fast extraction guard<span') ||
    !html.includes('>Avoid accidental touch<span') ||
    !html.includes('>Slow extraction guard<span') ||
    !html.includes('>A→M time guard<span') ||
    html.indexOf('<summary>No-scale BBW</summary>') < 0 ||
    html.indexOf('<summary>Fast extraction guard</summary>') < 0 ||
    html.indexOf('<summary>Cup protection</summary>') < 0 ||
    html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('<summary>Cup protection</summary>') ||
    html.indexOf('<summary>Cup protection</summary>') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !ui.includes('id="cupProtectionEnabled"') ||
    html.indexOf('id="cupProtectionEnabled"') >
        html.indexOf('id="stopIfCupRemoved"') ||
    !ui.includes('id="stopIfCupRemoved"') ||
    !ui.includes('id="requireCupToStart"') ||
    html.indexOf('id="requireCupToStart"') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    ui.includes('id="cupPresentWeightG"') ||
    html.includes('id="cupPresentWeightG"') ||
    html.includes('id="requireCupToStart" type="checkbox" checked') ||
    !ui.includes('place the cup after connect so presence can be detected.') ||
    !ui.includes('id="homeCupProtectionEnabled"') ||
    html.indexOf('<summary>Slow extraction guard</summary>') < 0 ||
    html.indexOf('<summary>A→M time guard</summary>') < 0 ||
    html.includes('<summary>Avoid accidental touch</summary>') ||
    html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('id="avoidAccidentalTouchEnabled"') ||
    html.indexOf('id="avoidAccidentalTouchEnabled"') >
        html.indexOf('<summary>Cup protection</summary>') ||
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
    !ui.includes("persistHomeGuard('homeAvoidAccidentalTouchEnabled'") ||
    !ui.includes("persistHomeGuard('homeSlowExtractionGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeAutoToManualGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeCupProtectionEnabled'") ||
    ui.includes("persistHomeGuard('homeSoundAlertsEnabled'") ||
    !ui.includes("'avoidBbwShotWithoutScale',0)") ||
    !ui.includes("'fastExtractionGuardEnabled',1)") ||
    !ui.includes("'avoidAccidentalTouchEnabled',1)") ||
    !ui.includes("'slowExtractionGuardEnabled',1)") ||
    !ui.includes("'autoToManualGuardEnabled',1)") ||
    !ui.includes("'cupProtectionEnabled',1)") ||
    !ui.includes('el.disabled=!controlsMutable||u||off') ||
    ui.includes('el.disabled=!controlsMutable||off||pend') ||
    !ui.includes('homeFlushBusy') ||
    !ui.includes('scheduleHomeGuardFlush()') ||
    !ui.includes("classList.toggle('fieldOff',off)") ||
    !ui.includes('bbw.disabled=!controlsMutable||x') ||
    !ui.includes('function homeSwitchUnset(') ||
    !ui.includes("classList.add('swR')") ||
    !ui.includes("typeof c[k]==='boolean'") ||
    html.includes('id="homeBrewByWeight" type="checkbox" role="switch" aria-label="Brew by weight" checked') ||
    html.includes('id="homeAvoidBbwShotWithoutScale" type="checkbox" role="switch" aria-label="No-scale BBW" checked') ||
    html.includes('id="homeSoundAlertsEnabled"') ||
    html.includes('id="homeCupProtectionEnabled" type="checkbox" role="switch" aria-label="Cup protection" checked') ||
    !css.includes('.switchRow.switchPending') ||
    !css.includes('.homeSwitchGrid .switchRow:not(.swR) .slider') ||
    !css.includes('.homeSwitchGrid .switchRow:not(.swR) .slider:before{display:none}') ||
    !css.includes('.homeSwitchGrid .switchRow:not(.swR) .slider,.homeSwitchGrid .switchRow:not(.swR) input:checked+.slider{background:#9ca3af}') ||
    !css.includes('.homeSwitchGrid') ||
    !css.includes('justify-content:space-between') ||
    !css.includes('.homeSwitchGrid{') ||
    !css.includes('border-bottom:1px solid') ||
    !css.includes('.switchState{display:none') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('.homeGuardGrid .swL{padding-right:3ch}') ||
    !css.includes('grid-template-columns:subgrid') ||
    !css.includes('#brewModeRow{width:100%') ||
    !css.includes('#brewModeRow{width:auto') ||
    !css.includes('#brewModeRow .swL{font-size:1.55rem;font-weight:800;line-height:1.05;color:var(--ac);letter-spacing:-.02em}') ||
    !css.includes('.homeGuardGrid .switch{width:4.25rem') ||
    !css.includes('.ruleChartHead{') ||
    !css.includes('.ruleChartHead strong,.ruleChartMode{display:none}') ||
    !css.includes('background:#c9a227') ||
    !ui.includes('function persistHomeBrewByWeight(') ||
    !ui.includes("onchange=R.persistHomeBrewByWeight") ||
    !ui.includes('beginHomeSwitchPending(h,on)') ||
    !ui.includes('id="clearLastShotButton"') ||
    html.indexOf('id="shotPanel"') > html.indexOf('id="clearLastShotButton"') ||
    (html.includes('id="clearLastShotButton"') &&
     html.includes('<span class="t">Clear</span>')) ||
    !css.includes('#shotPanel{position:relative;padding-right:3.4rem') ||
    !css.includes('#shotPanel .btnGlyph{min-height:2.85rem') ||
    html.includes('id="lastCycle"') ||
    !ui.includes('function renderShotPanel(') ||
    !ui.includes('function renderShotSpark(') ||
    !ui.includes("confirm:'CLEAR_LAST_SHOT'") ||
    !ui.includes('/api/v1/last-shot/clear') ||
    !network.includes('\\"lastShot\\"') ||
    !network.includes('\\"shotCurve\\"') ||
    !network.includes('formatShotCurveJsonBody') ||
    !network.includes('lastShotClearHandler') ||
    !network.includes('LAST_SHOT_CLEAR_NOT_CONFIRMED') ||
    !firmware.includes('persistLastShotFromEndedCycle') ||
    !firmware.includes('endedCycleDurationMs') ||
    firmware.includes('elapsedMs(relayBeforeOpen.closedAtMs)') ||
    !firmware.includes('clearLastShot') ||
    !firmware.includes('clearLastShotSnapshot') ||
    !firmware.includes('serviceShotStorePersistence') ||
    !firmwareCore.includes('shotLogPersistFailLatched') ||
    !firmwareCore.includes('shotStorePersistRetryAtMs') ||
    !firmwareCore.includes('SHOT_STORE_PERSIST_RETRY_MS') ||
    !firmwareCore.includes('noteScaleHistory(seenMac, seenName, false)') ||
    !firmwareCore.includes('constexpr uint32_t kTryLockMs = 0') ||
    wallClock.includes('monotonicMs >= anchorMonotonicMs_') ||
    !wallClock.includes('monotonicElapsedMs(monotonicMs, anchorMonotonicMs_)') ||
    !network.includes('StaJoinHints ShotStopperNetwork::staJoinHints()') ||
    !shotLogIo.includes('copyToFlashIoScratch(&store_') ||
    !shotCurveIo.includes('copyToFlashIoScratch(&store_') ||
    !lastShotIo.includes('copyToFlashIoScratch(&blob_') ||
    !jsonArena.includes('size > JSON_ARENA_CAPACITY') ||
    !network.includes('workBuf_->~NetworkWorkBuf()') ||
    !firmware.includes('resetAllDurableStoresForNetwork') ||
    !firmware.includes(
        'return resetAllDurableStoresForNetwork(persistedSettings)') ||
    !network.includes('historyMutationAllowed') ||
    !network.includes('controlAllowsHistoryMutation') ||
    (network.match(/historyMutationAllowed/g) || []).length < 4 ||
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
  if (!ui.includes('id="hCpu5s"') ||
      !ui.includes('id="hCpu1m"') ||
      !ui.includes('id="hCpu5m"') ||
      !ui.includes('id="hCpuMhz"') ||
      !ui.includes('id="hWifiState"') ||
      !ui.includes('id="hSsid"') ||
      !ui.includes('id="hWifiChannel"') ||
      !ui.includes('id="hWifiIp"') ||
      !ui.includes('id="hWifiSignal"') ||
      !ui.includes('id="hWifiRssi"') ||
      !ui.includes('id="hApState"') ||
      !ui.includes('id="hApSsid"') ||
      !ui.includes('id="hApIp"') ||
      !ui.includes('id="hApClients"') ||
      !ui.includes('id="hUptime"') ||
      !ui.includes('id="hFirmware"') ||
      !ui.includes('id="hBoot"') ||
      !ui.includes('id="hResetReason"') ||
      !ui.includes('id="hTemp"') ||
      !ui.includes('id="hTPeak"') ||
      !ui.includes('id="hRamT"') ||
      !ui.includes('id="hRamU"') ||
      !ui.includes('id="hRamF"') ||
      !ui.includes('id="hTaskState"') ||
      !ui.includes('id="hTaskElapsed"') ||
      !ui.includes('id="taskProfilerStartButton"') ||
      !ui.includes('id="taskProfilerStopButton"') ||
      !ui.includes('id="taskTable"') ||
      !ui.includes('id="taskTableBody"') ||
      !ui.includes('id="taskTableHint"') ||
      !ui.includes('function applyTaskProfiler(') ||
      !ui.includes('/api/v1/diagnostic/profiler') ||
      !ui.includes('100% = 1 core busy (sum can exceed 100)') ||
      !ui.includes('id="hHeapMin"') ||
      !ui.includes('id="hHeapLargest"') ||
      !ui.includes('id="hPsramT"') ||
      !ui.includes('id="hPsramF"') ||
      !ui.includes('id="hPsramL"') ||
      !ui.includes('id="hLoopGap"') ||
      !ui.includes('id="hLoopMax"') ||
      !ui.includes('id="lastCommandState"') ||
      !ui.includes('function updH(') ||
      !ui.includes('updH(s.health,s.safety)') ||
      !ui.includes('function applyDiagnosticStatus(') ||
      !ui.includes('loopIntervalGapMs') ||
      !ui.includes("s.health.loopIntervalGapMs+' ms'") ||
      !ui.includes("s.health.loopMaxGapMs+' ms'") ||
      !ui.includes('h.uptimeMs') ||
      !ui.includes('h.minimumFreeHeapBytes') ||
      !ui.includes('h.largestFreeHeapBlockBytes') ||
      !ui.includes('h.psramSizeBytes') ||
      !ui.includes('h.psramFreeBytes') ||
      !ui.includes('h.psramLargestFreeBlockBytes') ||
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
      !network.includes('cpuMhz') ||
      !ui.includes('cpuLoad5s') ||
      !ui.includes('cpuLoad1m') ||
      !ui.includes('cpuLoad5m') ||
      !ui.includes('cpuLoadValid') ||
      !ui.includes('cpuMhz') ||
      !ui.includes("w.cpuMhz+' MHz'") ||
      !ui.includes('w.cpu0Busy') ||
      !ui.includes('w.cpu1Busy') ||
      !ui.includes("t+' ('+a.toFixed(2)+' + '+b.toFixed(2)+')'") ||
      !ui.includes("split(w.cpuLoad5s,w.cpu0Busy,w.cpu1Busy)") ||
      !ui.includes('0–2 (cpu0 + cpu1)') ||
      !network.includes('tempPeakC') ||
      !network.includes('ramTotalBytes') ||
      !network.includes('\\"uptimeMs\\"') ||
      !network.includes('\\"minimumFreeHeapBytes\\"') ||
      !network.includes('\\"largestFreeHeapBlockBytes\\"') ||
      !network.includes('\\"psramSizeBytes\\"') ||
      !network.includes('\\"psramFreeBytes\\"') ||
      !network.includes('\\"psramLargestFreeBlockBytes\\"') ||
      !network.includes('\\"bleHostAllocPsram\\"') ||
      !network.includes('\\"bleHostAllocFallback\\"') ||
      !network.includes('\\"workBufExternal\\"') ||
      !network.includes('\\"jsonArenaExternal\\"') ||
      !network.includes('\\"allocExternalFallback\\"') ||
      !network.includes('\\"resetReasonCode\\"') ||
      !diagHtml.includes('id="diagnosticsPanel"') ||
      !diagHtml.includes('<legend>Diagnostics</legend>') ||
      !diagHtml.includes('<legend>States</legend>') ||
      !diagHtml.includes('<legend>Guards</legend>') ||
      !diagHtml.includes('<legend>Machine I/O</legend>') ||
      !diagHtml.includes('<legend>WiFi</legend>') ||
      !diagHtml.includes('<legend>AP</legend>') ||
      !diagHtml.includes('<legend>CPU') ||
      !diagHtml.includes('<legend>Tasks') ||
      !diagHtml.includes('<legend>RAM</legend>') ||
      !diagHtml.includes('<legend>HEAP</legend>') ||
      !diagHtml.includes('<legend>Scale</legend>') ||
      !diagHtml.includes('<legend>MISC</legend>') ||
      !diagHtml.includes('id="dMachine"') ||
      !diagHtml.includes('id="dBrew"') ||
      !diagHtml.includes('id="dCup"') ||
      !diagHtml.includes('id="dGuardNoScaleUser"') ||
      !diagHtml.includes('id="dGuardNoScaleRaw"') ||
      !diagHtml.includes('id="dGuardAtmUser"') ||
      !diagHtml.includes('id="dGuardSlowUser"') ||
      !diagHtml.includes('id="dGuardFastUser"') ||
      !diagHtml.includes('id="dGuardTouchUser"') ||
      !diagHtml.includes('id="dGuardCupUser"') ||
      !diagHtml.includes('id="exportDebugDataButton"') ||
      !diagHtml.includes('id="dActivator"') ||
      !diagHtml.includes('id="dReed"') ||
      !diagHtml.includes('class="paddleOnly">Paddle</strong>') ||
      !diagHtml.includes('class="momentaryOnly">Switch</strong>') ||
      !diagHtml.includes('class="metric reedOnly"') ||
      !diagHtml.includes('id="dRelay"') ||
      !diagHtml.includes('id="dSource"') ||
      !diagHtml.includes('id="dSafety"') ||
      !diagHtml.includes('id="dFault"') ||
      !diagHtml.includes('id="dWatchdog"') ||
      !diagHtml.includes('id="dExternal"') ||
      !diagHtml.includes('id="dRecovery"') ||
      !diagHtml.includes('id="dStream"') ||
      !diagHtml.includes('id="dControl"') ||
      !diagHtml.includes('id="hRecoveredStales"') ||
      !diagHtml.includes('id="hStaleTime"') ||
      !ui.includes("t('dMachine',s.machineState)") ||
      !ui.includes("t('hFirmware',s.firmwareVersion)") ||
      !ui.includes("t('hBoot',typeof s.bootId==='number'&&s.bootId?'#'+s.bootId:'')") ||
      !ui.includes("t('dBrew',s.state)") ||
      !ui.includes("t('dCup',cp.state)") ||
      !ui.includes('function applyDiagnosticGuards(') ||
      !ui.includes('function exportDebugData(') ||
      !ui.includes('/api/v1/debug/export') ||
      !ui.includes("t('dActivator',s.physicalActivatorOn?'ON':'OFF')") ||
      !ui.includes("t('dReed',s.reedOn?'ON':'OFF')") ||
      !ui.includes("t('dStream',sc.streamState)") ||
      !ui.includes("t('dControl',sc.controlState)") ||
      !ui.includes("t('hRecoveredStales',String(sc.recoveredStaleCount))") ||
      !ui.includes("t('hStaleTime',typeof sc.recoveredStaleMs==='number'?sc.recoveredStaleMs+' ms':'')") ||
      !diagHtml.includes('<strong>Heap min</strong>') ||
      !diagHtml.includes('<strong>Heap largest</strong>') ||
      !diagHtml.includes('<strong>PSRAM size</strong>') ||
      !diagHtml.includes('<strong>PSRAM free</strong>') ||
      !diagHtml.includes('<strong>PSRAM largest</strong>') ||
      !diagHtml.includes('<strong>Clock</strong>') ||
      !diagHtml.includes('<strong>Temp current</strong>') ||
      !diagHtml.includes('<strong>Temp peak</strong>') ||
      diagHtml.includes('<details') ||
      diagHtml.includes('<summary>Diagnostics</summary>') ||
      !diagHtml.includes('id="hFirmware"') ||
      !diagHtml.includes('id="hBoot"') ||
      !diagHtml.includes('<strong>Firmware</strong>') ||
      !diagHtml.includes('<strong>Boot</strong>') ||
      !diagHtml.includes('id="currentTime"') ||
      !diagHtml.includes('id="ntpStatus"') ||
      !diagHtml.includes('id="ntpLastSync"') ||
      !diagHtml.includes('id="ntpServer"') ||
      diagHtml.indexOf('id="diagnosticsPanel"') > diagHtml.indexOf('id="logPanel"') ||
      diagHtml.indexOf('<legend>States</legend>') > diagHtml.indexOf('<legend>Guards</legend>') ||
      diagHtml.indexOf('<legend>Guards</legend>') > diagHtml.indexOf('<legend>Machine I/O</legend>') ||
      diagHtml.indexOf('<legend>Machine I/O</legend>') > diagHtml.indexOf('<legend>WiFi</legend>') ||
      diagHtml.indexOf('<legend>WiFi</legend>') > diagHtml.indexOf('<legend>AP</legend>') ||
      diagHtml.indexOf('<legend>AP</legend>') > diagHtml.indexOf('<legend>CPU') ||
      diagHtml.indexOf('<legend>CPU') > diagHtml.indexOf('<legend>Tasks') ||
      diagHtml.indexOf('<legend>Tasks') > diagHtml.indexOf('<legend>RAM</legend>') ||
      diagHtml.indexOf('<legend>RAM</legend>') > diagHtml.indexOf('<legend>HEAP</legend>') ||
      diagHtml.indexOf('<legend>HEAP</legend>') > diagHtml.indexOf('<legend>Scale</legend>') ||
      diagHtml.indexOf('<legend>Scale</legend>') > diagHtml.indexOf('<legend>MISC</legend>') ||
      diagHtml.indexOf('id="hWifiState"') > diagHtml.indexOf('id="hSsid"') ||
      diagHtml.indexOf('id="hCpu5s"') > diagHtml.indexOf('id="hCpuMhz"') ||
      diagHtml.indexOf('id="hCpuMhz"') > diagHtml.indexOf('id="hTemp"') ||
      diagHtml.indexOf('id="hRamF"') > diagHtml.indexOf('id="hHeapMin"') ||
      diagHtml.indexOf('id="hHeapMin"') > diagHtml.indexOf('id="hHeapLargest"') ||
      diagHtml.indexOf('id="hHeapLargest"') > diagHtml.indexOf('id="hPsramT"') ||
      diagHtml.indexOf('id="hPsramT"') > diagHtml.indexOf('id="hPsramF"') ||
      diagHtml.indexOf('id="hPsramF"') > diagHtml.indexOf('id="hPsramL"') ||
      diagHtml.indexOf('id="hFirmware"') > diagHtml.indexOf('id="hBoot"') ||
      diagHtml.indexOf('id="hBoot"') > diagHtml.indexOf('id="currentTime"') ||
      diagHtml.indexOf('id="currentTime"') > diagHtml.indexOf('id="ntpStatus"') ||
      diagHtml.indexOf('id="ntpStatus"') > diagHtml.indexOf('id="ntpLastSync"') ||
      adminHtml.includes('id="diagnosticsPanel"') ||
      adminHtml.includes('id="currentTime"') ||
      adminHtml.includes('<summary>Diagnostics</summary>') ||
      statusHtml.includes('id="currentTime"') ||
      statusHtml.includes('id="ntpStatus"') ||
      !css.includes('#diagnosticsPanel fieldset') ||
      !css.includes('.metric,.shotCard > *{') ||
      css.includes('#diagnosticsPanel .metric,#statusPanel .metric,#scalePanel .metric,.shotCard > *{') ||
      css.includes('diagGroup')) {
    throw new Error(
        'Diagnostics must be a non-collapsible fieldset at the top of Diagnostic, above Log, with States/Guards/Machine I/O/WiFi/AP/CPU/Tasks/RAM/HEAP/Scale/MISC sections and one value per label');
  }
}
if (!ui.includes('id="shotTable"') ||
    !ui.includes('id="exportShotsButton"') ||
    !ui.includes('id="clearShotsButton"') ||
    !html.includes('id="clearShotsButton" class="btnGlyph btnInvert"') ||
    html.includes('id="clearShotsButton" class="btnGlyph btnDanger"') ||
    !css.includes('#shotHistoryPanel .btnGlyph:not(.btnInvert)') ||
    !ui.includes("confirm:'CLEAR_SHOT_LOG'") ||
    !ui.includes('refreshShots()') ||
    !js.includes("'shotDur'") ||
    !js.includes("'shotActual'") ||
    !css.includes('#shotTable .shotDur,#shotTable .shotActual') ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "time time time time time time" "goal goal flow flow drop drop" "err err shot shot ended ended" "spark spark spark spark spark spark"') ||
    !css.includes('#shotTable tr.noSpark{grid-template-areas:"dur dur dur actual actual actual" "time time time time time time" "goal goal flow flow drop drop" "err err shot shot ended ended"}') ||
    css.includes('grid-area:guard') ||
    css.includes('grid-area:ext') ||
    css.includes('grid-area:stop') ||
    css.includes('grid-area:cut') ||
    !css.includes('#shotTable .shotDel') ||
    !js.includes("className='shotDel'") ||
    runtimeJs.includes('<span class="t">Delete</span>') ||
    !ui.includes('formatShotTime(r)') ||
    !runtimeJs.includes('function formatShotEnded(') ||
    !runtimeJs.includes('function shotDisplayActualG(') ||
    !runtimeJs.includes('shotDisplayActualG(r.actualG,r.wCg)') ||
    !runtimeJs.includes('shotDisplayActualG(ls.currentWeightG,cv.wCg)') ||
    !runtimeJs.includes('return y!=null&&y>=1') ||
    !runtimeJs.includes('shotDisplayFlowGS(r)') ||
    !runtimeJs.includes('const live=!!(s.cycle&&s.cycle.active)') ||
    runtimeJs.includes('const live=!!((s.cycle&&s.cycle.active)||s.liveShot)') ||
    runtimeJs.includes('const live=!!((s.cycle&&s.cycle.active)||s.relayClosed)') ||
    !runtimeJs.includes('const dropMs=live?(src.firstDropElapsedMs||0):(ls&&ls.firstDropElapsedMs||0)') ||
    !runtimeJs.includes('formatShotEnded(r.stopDetail)') ||
    !runtimeJs.includes("labels=['Time','Dur','Goal','Weight','Err%','Flow','1st drop','Ended','Shot']") ||
    runtimeJs.includes("labels=['Time','Dur','Goal','Actual','Err%','Flow','1st drop','Ended','Shot']") ||
    runtimeJs.includes("labels=['Time','Dur','Goal','Actual','Err%','Flow','1st drop','Guard','Ext','Stop','Shot','Cut']") ||
    partialHtml.history.includes('<th>Guard</th>') ||
    partialHtml.history.includes('<th>Ext</th>') ||
    partialHtml.history.includes('<th>Stop</th>') ||
    partialHtml.history.includes('>Cut</th>') ||
    !partialHtml.history.includes('<th>Ended</th>') ||
    !partialHtml.history.includes('<th>Weight</th>') ||
    partialHtml.history.includes('<th>Actual</th>') ||
    !ui.includes('no time') ||
    !ui.includes('id="timezoneOffsetMinutes"') ||
    !js.includes('m+=15') ||
    js.includes('Request accepted.') ||
    js.includes("message('Request queued.','ok')") ||
    js.includes('Request queued successfully.') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (!js.includes('function commandOkMessage(') ||
    !js.includes('function commandFailMessage(') ||
    !js.includes('function formatCommandError(') ||
    !js.includes('function homePendingPairs(') ||
    !js.includes('command(path,value={},soft,okMsg,failMsg)') ||
    js.includes("message(e&&e.message?e.message:'Request failed.','error')") ||
    !js.includes('Machine settings saved.') ||
    !js.includes("cn('save machine settings')") ||
    !js.includes('Brew settings saved.') ||
    !js.includes("'save brew settings'") ||
    !js.includes("homeFastExtractionGuardEnabledState','fastExtractionGuardEnabled','Fast extraction guard'") ||
    !js.includes("label+(on?' enabled.':' disabled.')") ||
    !js.includes("'Could not '+(on?'enable ':'disable ')+label+'.'") ||
    !js.includes('Wi-Fi settings saved. Restarting.') ||
    !js.includes("cn('save Wi-Fi settings')") ||
    !js.includes('Wi-Fi scan started.') ||
    !js.includes('Could not start Wi-Fi scan.') ||
    !js.includes('Administration unlocked.') ||
    !js.includes('Could not unlock administration.') ||
    !js.includes("R.noteReachOk();R.message('Administration unlocked.','ok')") ||
    !js.includes('Administration locked.') ||
    !js.includes('Could not lock administration.') ||
    !js.includes("noteReachOk();message('Administration locked.','ok')") ||
    !js.includes("noteReachOk();message((wanted?'BLE companion enabled.'") ||
    !js.includes('Shot history cleared.') ||
    !js.includes('Could not clear shot history.') ||
    !js.includes('BLE companion enabled.') ||
    !js.includes('Could not enable BLE companion.') ||
    !js.includes('Could not update Quick Settings.') ||
    js.includes('Shot history cleared successfully.') ||
    js.includes('Unlock failed.') ||
    js.includes('Lock failed.')) {
  throw new Error('Web UI must show action-specific success and failure toasts instead of generic queued/failed copy');
}
if (!runtimeJs.includes('SHOTS_PAGE_SIZE=10') ||
    !runtimeJs.includes('SHOTS_EXPORT_LIMIT=120') ||
    !runtimeJs.includes("shotsUrl(offset,limit)") ||
    !runtimeJs.includes("'/api/v1/shots?offset='") ||
    !runtimeJs.includes("fetchShotPage(0,SHOTS_PAGE_SIZE,'replace')") ||
    !runtimeJs.includes("fetchShotPage(shotHistory.shots.length,SHOTS_PAGE_SIZE,'append')") ||
    !runtimeJs.includes("fetchShotPage(0,SHOTS_PAGE_SIZE,'poll')") ||
    !runtimeJs.includes('async function loadMoreShots(){') ||
    !runtimeJs.includes('function shotHistoryViewActive(){') ||
    !runtimeJs.includes('if(ok)maybeLoadMoreShots()') ||
    !runtimeJs.includes("shotsUrl(0,SHOTS_EXPORT_LIMIT)") ||
    runtimeJs.includes("api('/api/v1/shots')") ||
    !viewJs.history.includes('IntersectionObserver') ||
    !viewJs.history.includes("R.loadMoreShots()") ||
    !partialHtml.history.includes('id="shotHistorySentinel"') ||
    !css.includes('#shotHistorySentinel{min-height:1px') ||
    !network.includes('parseShotsPageQuery') ||
    !network.includes('shotLogPageSlice') ||
    !network.includes('SHOT_LOG_PAGE_DEFAULT') ||
    !network.includes('\\"hasMore\\":%s') ||
    !network.includes('\\"total\\":%u') ||
    !network.includes('index == start ? "" : ","') ||
    !appJsSource.includes('mod.activate()')) {
  throw new Error('Shot history must page 10 shots with infinite scroll and poll only the first page');
}
const shotLogTypes = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperShotLogTypes.h'), 'utf8');
const statsSection = partialHtml.history.match(
    /<fieldset id="shotStatsPanel"><legend>Averages<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statsSection ||
    partialHtml.history.indexOf('id="shotStatsPanel"') < 0 ||
    partialHtml.history.indexOf('id="shotStatsPanel"') >
        partialHtml.history.indexOf('id="shotHistoryPanel"') ||
    !statsSection[1].includes('class="shotCard"') ||
    !statsSection[1].includes('class="shotDur"') ||
    !statsSection[1].includes('class="shotActual"') ||
    !statsSection[1].includes('<strong>Avg time</strong>') ||
    !statsSection[1].includes('<strong>Avg weight</strong>') ||
    !statsSection[1].includes('<strong>Daily shots</strong>') ||
    !statsSection[1].includes('<strong>Avg error</strong>') ||
    !statsSection[1].includes('<strong>Avg flow</strong>') ||
    !statsSection[1].includes('id="histAvgDur"') ||
    !statsSection[1].includes('id="histAvgWeight"') ||
    !statsSection[1].includes('id="histAvgDaily"') ||
    !statsSection[1].includes('id="histAvgErr"') ||
    !statsSection[1].includes('id="histAvgFlow"') ||
    !statsSection[1].includes('class="fieldHint"') ||
    !statsSection[1].includes('Based on the last 10 shots.') ||
    !runtimeJs.includes('function renderShotStats(){') ||
    !runtimeJs.includes('shotHistory.shots.slice(0,SHOTS_PAGE_SIZE)') ||
    !runtimeJs.includes('renderShotStats();') ||
    runtimeJs.includes('slice(0,20)') ||
    !css.includes('.shotCard:has(>:nth-child(5):last-child){grid-template-areas:"dur dur dur actual actual actual" "goal goal err err flow flow"}') ||
    !css.includes('.shotCard+.fieldHint{margin-top:.44rem}') ||
    css.includes('#shotStatsPanel') ||
    css.includes('#histAvgDur') ||
    css.includes('histAvgDaily') ||
    network.includes('shotLogComputeAverages') ||
    network.includes('SHOT_LOG_STATS_WINDOW') ||
    network.includes('\\"avgDailyShots\\"') ||
    network.includes('/api/v1/shots/stats') ||
    shotLogTypes.includes('shotLogComputeAverages') ||
    shotLogTypes.includes('SHOT_LOG_STATS_WINDOW')) {
  throw new Error(
      'History averages must be a 2+3 shotCard above the table, last-10 window in JS, and no stats API/firmware');
}
if (!ui.includes('id="firmwareFooter"') ||
    !ui.includes('id="inactiveFirmware"') ||
    !ui.includes('firmwareVersion') ||
    !ui.includes('updateFirmwareFooter()') ||
    !ui.includes("const nav=$('navFirmware')") ||
    !ui.includes("const inactive=$('inactiveFirmware')") ||
    !shellHtml.includes('id="navFirmware"') ||
    !shellHtml.includes('class="navMeta"') ||
    !css.includes('body.homeAdminActions #view-home:not(.hidden)~.pageFooter{display:none}') ||
    css.includes('#view-home:not(.hidden)~.pageFooter{padding-bottom:8.5rem}') ||
    !css.includes('body.homeAdminActions #view-home:not(.hidden){padding-bottom:') ||
    !css.includes('#actionsPanel{position:fixed;left:0;right:0') ||
    !css.includes('@media(min-width:700px){#actionsPanel{left:1rem;right:1rem') ||
    !network.includes('\\"firmwareVersion\\"') ||
    !network.includes('\\"bootId\\":%lu') ||
    !network.includes('FW_VERSION')) {
  throw new Error('Firmware version must be exposed in status API, nav menu, and Diagnostic');
}
if (!shellHtml.includes('https://github.com/Cheerpipe/AcaiaArduinoBLE') ||
    !shellHtml.includes('https://github.com/Cheerpipe') ||
    !shellHtml.includes('Hecho por') ||
    !shellHtml.includes('>Cheerpipe</a>') ||
    shellHtml.indexOf('class="navMeta"') > shellHtml.indexOf('id="app"') ||
    !css.includes('.navMeta{margin-top:auto')) {
  throw new Error('Web UI footer must credit the GitHub repo and Cheerpipe in the nav menu');
}
if (!shellHtml.includes('id="message"') ||
    !shellHtml.includes('id="messageText"') ||
    !shellHtml.includes('id="messageClose"') ||
    !runtimeJs.includes('function clearMessage(') ||
    !runtimeJs.includes('b.onclick=clearMessage') ||
    !runtimeJs.includes("kind==='ok'?5e3") ||
    !runtimeJs.includes("kind==='warn'&&!e.querySelector('button:not(#messageClose)')?15e3") ||
    !runtimeJs.includes('setTimeout(clearMessage,ms)') ||
    !css.includes('.messageClose')) {
  throw new Error('Status message bar must auto-hide ok/warn and stay for errors');
}
if (!css.includes('#message[hidden]') ||
    css.includes('#message[hidden]{display:none}') ||
    !css.includes('#message[hidden]{display:flex!important;visibility:hidden') ||
    !/#message\{[^}]*min-height:2\.6rem/.test(css)) {
  throw new Error('Status message bar must keep its layout height while hidden');
}
if (!/<fieldset[^>]*><legend>Log<\/legend>/.test(html) ||
    /authenticatedOnly[^>]*><legend>Log<\/legend>/.test(html) ||
    !ui.includes('loadLog()') ||
    !ui.includes('refreshLog()') ||
    !(ui.includes("name==='diagnostic'") || ui.includes("name === 'diagnostic'")) ||
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
    !network.includes('resetAllDurableStores(next)') ||
    !network.includes('saveRecoveryIntent(RecoveryOperation::FACTORY_RESET)') ||
    !ui.includes('id="restartPanel"') ||
    html.indexOf('id="saveDateTimeButton"') > html.indexOf('id="restartPanel"') ||
    html.indexOf('id="restartPanel"') > html.indexOf('id="factoryResetButton"') ||
    html.slice(html.indexOf('id="actionsPanel"'), html.indexOf('id="view-history"'))
        .includes('restartButton') ||
    !html.includes('id="restartButton" class="btnGlyph btnWarn"') ||
    html.includes('id="restartButton" class="btnGlyph btnInvert"') ||
    !html.includes('id="factoryResetButton" class="btnGlyph mutable btnInvert"') ||
    html.includes('id="factoryResetButton" class="btnGlyph mutable btnWarn"') ||
    !css.includes('.btnGlyph.btnInvert')) {
  throw new Error('Factory reset must require UI and server-side confirmation');
}
if (!css.includes('.btnBar,.presetActions{display:flex;gap:.45rem') ||
    css.includes('.btnBar,.presetActions{display:flex;gap:0') ||
    css.includes('.btnGlyph.btnDanger,.btnGlyph.btnInvert{background:var(--ac)') ||
    !css.includes('.btnGlyph.btnInvert{background:var(--ac)') ||
    !css.includes('.btnGlyph.btnDanger{background:var(--bg)') ||
    !css.includes('#shotHistoryPanel .btnBar{') ||
    !css.includes('#shotHistoryPanel .btnBar{position:sticky;top:var(--hdr);z-index:6;background:var(--sf);margin:0 0 .65rem;border:0;overflow:visible}') ||
    css.includes('#shotHistoryPanel .btnBar{position:sticky;top:var(--hdr);z-index:6;background:var(--sf);margin:0 0 .65rem;border:1px solid var(--ln);border-radius:var(--r);overflow:hidden}')) {
  throw new Error('Action buttons must be separate with a gap; btnDanger must not share invert fill');
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
{
  const machineFn = runtimeJs.slice(
      runtimeJs.indexOf('function machinePayload('),
      runtimeJs.indexOf('function dateTimePayload('));
  const dateTimeFn = runtimeJs.slice(
      runtimeJs.indexOf('function dateTimePayload('),
      runtimeJs.indexOf('function brewPayload('));
  if (!machineFn.includes("scaleConnectedLed:$('scaleConnectedLed').checked") ||
      machineFn.includes('ntpServerPreset') ||
      machineFn.includes('timezoneOffsetMinutes') ||
      machineFn.includes('serialDebugOutput') ||
      machineFn.includes('ringRetainLogLevel') ||
      machineFn.includes('goalWeightG') ||
      machineFn.includes('brewByWeight') ||
      !dateTimeFn.includes('timezoneOffsetMinutes') ||
      !dateTimeFn.includes('ntpServerPreset') ||
      !dateTimeFn.includes('ntpServerCustom') ||
      dateTimeFn.includes('scaleConnectedLed') ||
      !ui.includes("saveDateTimeButton').onclick=R.saveDateTimeConfig") ||
      ui.includes("saveDateTimeButton').onclick=R.saveMachineConfig") ||
      (ui.includes("function markConfigDirty(") &&
       runtimeJs.slice(runtimeJs.indexOf('function markConfigDirty('),
                       runtimeJs.indexOf('function markDateTimeDirty('))
           .includes('dateTimeDirtyHint'))) {
    throw new Error(
        'Save machine must send only machine fields; Date & time has its own payload and dirty flag');
  }
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
    !ui.includes("serialDebugOutput:!!$('serialDebugOutput').checked") ||
    !ui.includes("ringRetainLogLevel:$('ringRetainLogLevel').value||'none'") ||
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
    !ui.includes("$('networkStatus').textContent=formatNetworkStatus(s.network)") ||
    !ui.includes("t('hSsid',n.ssid)") ||
    !ui.includes("t('hWifiState',n.staState)") ||
    !ui.includes("$('apStatus').textContent='AP: '+(s.network.apActive?'active':'inactive')") ||
    !ui.includes("t('hApState',n.apActive?'active':'inactive')") ||
    !html.includes('<legend>WiFi</legend>') ||
    !html.includes('<strong>SSID</strong><div id="hSsid">') ||
    !html.includes('<legend>AP</legend>') ||
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
if (!network.includes('restoreLkgToActive(next)') ||
    !network.includes('startStation(settings, now)')) {
  throw new Error('STA confirm timeout must reassociate last-known-good before SoftAP fallback');
}
if (!firmwareCore.includes('command.commitConfirmed = true') ||
    !network.includes('finalizeSavedStaCredentials(next, command.commitConfirmed)')) {
  throw new Error(
      'USB SET_WIFI must commit STA credentials; Web UI / BLE Companion keep the confirm window');
}
{
  if (network.includes('ShotStopperNetwork::loginHandler') ||
      network.includes('ShotStopperNetwork::logoutHandler') ||
      network.includes('loginRateLimited') ||
      network.includes('recordFailedLoginAttempt')) {
    throw new Error('Login handlers and rate limits must be removed; WebUI claim owns the session');
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
if (!/<script\s+type="module"\s+src="\/app\.js\?v=/.test(shellHtml) &&
    !shellHtml.includes('src="/app.js?v=__FW_VERSION__"')) {
  throw new Error('Web UI must load same-origin /app.js with a firmware version query');
}
if (!/<link\s+rel="stylesheet"\s+href="\/app\.css\?v=/.test(shellHtml) &&
    !shellHtml.includes('href="/app.css?v=__FW_VERSION__"')) {
  throw new Error('Web UI must load same-origin /app.css with a firmware version query');
}
if (!shellHtml.includes('name="color-scheme" content="light dark"')) {
  throw new Error('Web UI must declare color-scheme so Safari form controls follow dark mode');
}
if (/<link\s+[^>]*href=["']https?:\/\//i.test(shellHtml) ||
    /cdn\.|unpkg\.|jsdelivr\./i.test(shellHtml)) {
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
  ['GET /js/runtime.js', 'runtimeJsHandler'],
  ['GET /js/secondary.js', 'secondaryJsHandler'],
  ['GET /partials/history.html', 'partialHistoryHandler'],
  ['GET /partials/diagnostic.html', 'partialDiagnosticHandler'],
  ['GET /partials/settings.html', 'partialSettingsHandler'],
  ['GET /partials/admin.html', 'partialAdminHandler'],
  ['GET /js/settings.js', 'viewSettingsHandler'],
  ['GET /favicon.ico', 'browserIconHandler'],
  ['GET /apple-touch-icon.png', 'browserIconHandler'],
  ['GET /apple-touch-icon-precomposed.png', 'browserIconHandler'],
  ['POST /api/v1/ui/claim', 'claimHandler'],
  ['GET /api/v1/status/home', 'ownedApiHandler'],
  ['GET /api/v1/status/settings', 'ownedApiHandler'],
  ['GET /api/v1/status/admin', 'ownedApiHandler'],
  ['GET /api/v1/status/diagnostic', 'ownedApiHandler'],
  ['GET /api/v1/debug/export', 'ownedApiHandler'],
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
  ['POST /api/v1/control/state-override', 'ownedApiHandler'],
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
  ['POST /api/v1/device/password', 'ownedApiHandler'],
  ['POST /api/v1/admin/unlock', 'ownedApiHandler'],
  ['POST /api/v1/admin/lock', 'ownedApiHandler'],
  ['POST /api/v1/diagnostic/profiler', 'ownedApiHandler'],
  // OTA authenticates with the device password instead of the exclusive
  // WebUI claim, so a command line client can update firmware without stealing
  // control from an open browser window.
  ['GET /api/v1/ota', 'otaStatusHandler'],
  ['POST /api/v1/ota', 'otaUploadHandler'],
  ['POST /api/v1/ota/flash', 'otaFlashHandler'],
  ['POST /api/v1/ota/abort', 'otaAbortHandler'],
]);

const maxSocketsMatch = network.match(/max_open_sockets\s*=\s*(\d+)/);
if (!maxSocketsMatch || Number(maxSocketsMatch[1]) !== 4) {
  throw new Error('HTTP server must reserve exactly 4 open sockets for the single-owner WebUI');
}
if (!network.includes('backlog_conn = 3')) {
  throw new Error('HTTP server backlog must be limited to 3 for the single-owner WebUI');
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
    !ui.includes('function showInactiveOverlay()') ||
    !ui.includes('function hideInactiveOverlay()') ||
    !ui.includes('id="webUiReload"') ||
    !ui.includes('>Reload<') ||
    !ui.includes('X-WebUI-Client')) {
  throw new Error('Inactive WebUI windows must become passive and offer Reload');
}
if (!ui.includes('const WEB_UI_INACTIVITY_MS=15*60*1000') ||
    !ui.includes('function resetWebUiInactivity()') ||
    !ui.includes('function webUiPollingActive()') ||
    !ui.includes('function noteWebUiInteraction(event)') ||
    !(ui.includes("document.addEventListener('pointerdown',noteWebUiInteraction,true)") ||
      ui.includes("document.addEventListener('pointerdown', R.noteWebUiInteraction, true)")) ||
    !(ui.includes("document.addEventListener('keydown',noteWebUiInteraction,true)") ||
      ui.includes("document.addEventListener('keydown', R.noteWebUiInteraction, true)")) ||
    ui.includes("addEventListener('scroll',noteWebUiInteraction") ||
    ui.includes("addEventListener('scroll', R.noteWebUiInteraction")) {
  throw new Error('WebUI inactivity must expire after 15 minutes of direct control interaction, never scrolling');
}
if (!ui.includes('Press Reload to enable this window again.') ||
    !ui.includes('id="webUiInactive"') ||
    !ui.includes('id="inactiveHint"') ||
    !ui.includes('id="inactiveError"') ||
    !css.includes('.inactiveOverlay') ||
    !css.includes('.inactiveOverlay.isVisible') ||
    !css.includes('opacity .5s') ||
    !network.includes('This WebUI window is inactive. Reactivate to continue.') ||
    ui.includes('Another WebUI window controls this device.') ||
    network.includes('Another WebUI window has taken control.') ||
    ui.includes('function ownershipBanner(') ||
    ui.includes('webUiOwnership') ||
    ui.includes('btnTakeControl') ||
    ui.includes('Reactivate')) {
  throw new Error('WebUI inactive notice must be a full-screen Reload overlay');
}
if (!ui.includes("w.id='reconnectWait'") ||
    !ui.includes("w.className='reconnectRing'") ||
    !ui.includes('id="reconnectSeconds"') ||
    !ui.includes('function beginNetworkReconnectWait()') ||
    !ui.includes('function endNetworkReconnectWait()') ||
    !ui.includes('function pollNetworkReconnect()') ||
    !ui.includes('NETWORK_RECONNECT_WAIT_MS=180000') ||
    !ui.includes('setTimeout(pollNetworkReconnect,2e3)') ||
    !ui.includes('setInterval(updateReconnectCountdown,1e3)') ||
    !ui.includes('rec?4e3:8e3') ||
    !ui.includes("value.action==='save'") ||
    !ui.includes('beginNetworkReconnectWait()') ||
    !ui.includes('claimWebUiOwnership()') ||
    !ui.includes('Waiting for the controller on this address.') ||
    !ui.includes('the previous network should return shortly.') ||
    !css.includes('.inactiveOverlay.isReconnectWait') ||
    !css.includes('.reconnectRing') ||
    !css.includes('conic-gradient') ||
    ui.includes('function heartbeat(') ||
    ui.includes('/api/v1/heartbeat')) {
  throw new Error(
      'Wi-Fi save must show a 180s reconnect overlay that polls ui/claim even after 0s, without a heartbeat endpoint');
}
if (!ui.includes('clearTimeout(webUiInactivityTimer)') ||
    !ui.includes('clearTimeout(scanTimer)') ||
    !ui.includes('stopViewPolls()') ||
    !ui.includes('if(!webUiPollingActive())throw new Error')) {
  throw new Error('WebUI inactivity must cancel poll timers and block further API calls');
}
if (!ui.includes('setMutable(!!s.configMutable||!!s.webUiOverrideActive)') ||
    !ui.includes('webUiOverrideActive') ||
    !ui.includes('webUiOverrideRemainingMs') ||
    !ui.includes("uiOverridePanel") ||
    !ui.includes("uiOverrideButton") ||
    !ui.includes('UI Override') ||
    !ui.includes("closest('#adminLockPanel,#diagnosticLockPanel,#uiOverridePanel,#bleCompanionPanel')") ||
    !ui.includes('function ensureUiOverridePanel(') ||
    !ui.includes('/api/v1/ui/unlock') ||
    !ui.includes('UNSAFE_WEBUI_OVERRIDE') ||
    !network.includes('/api/v1/ui/unlock') ||
    !network.includes('UNSAFE_WEBUI_OVERRIDE') ||
    !network.includes('WEB_UI_OVERRIDE_MS') ||
    !network.includes('webUiOverrideUntilMs_') ||
    !network.includes('webUiOverrideRemainingMs') ||
    !network.includes('requireAdminUnlock(request)') ||
    network.includes('clearWebUiOverrideIfSafe')) {
  throw new Error('Web UI must honor configMutable/webUiOverrideActive with a timed admin-gated UI Override');
}
if (ui.includes('configLockBanner') || css.includes('configLockBanner') ||
    ui.includes('ensureConfigLockBanner') || ui.includes('renderConfigLockBanner') ||
    ui.includes('Controls locked:') || ui.includes('Unsafe WebUI override active')) {
  throw new Error('Web UI must not render the configuration-lock warning banner');
}
if (network.includes('return "safety_recovery"') ||
    network.includes('return "safety_lockout"') ||
    ui.includes("safety_recovery:'safety recovery'") ||
    ui.includes("safety_lockout:'safety lockout'")) {
  throw new Error('Safety recovery must not lock the WebUI');
}
if (!ui.includes("s.safety.recoveryRequired||s.safety.state==='LOCKOUT'") ||
    !ui.includes('admin&&remoteReady&&relayStartReady&&canControl') ||
    !ui.includes("live?'Stop shot':'Start shot'") ||
    !ui.includes("dataset.mode==='stop'") ||
    !ui.includes("/api/v1/control/paddle") ||
    !ui.includes("/api/v1/control/stop") ||
    !ui.includes('function updateHomeAdminActions(') ||
    !ui.includes("id=\"actionsPanel\" class=\"hidden\"") ||
    !ui.includes('shot.disabled=!admin||(!live&&!(remoteReady&&relayStartReady&&canControl))')) {
  throw new Error('Circuit actions must stay behind admin unlock and preserve Stop only while unlocked');
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
if (!network.includes('recv_wait_timeout = 5') ||
    !network.includes('send_wait_timeout = 5')) {
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
    !ui.includes('command(path,value={},soft,okMsg,failMsg)') ||
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
if (!runtimeJs.includes("const SOFTAP_HOST='192.168.4.1'") ||
    !runtimeJs.includes('function statusOnSta(){return location.hostname!==SOFTAP_HOST}') ||
    !runtimeJs.includes("function statusIntervalMs(){return document.hidden?12e3:statusLiveShot&&activeView==='home'&&statusOnSta()?1e3:statusLiveShot?2500:4e3}") ||
    !runtimeJs.includes('statusLiveShot') ||
    !runtimeJs.includes("activeView==='home'") ||
    !runtimeJs.includes('12e3') ||
    !runtimeJs.includes('1e3') ||
    !runtimeJs.includes('2500') ||
    !runtimeJs.includes('4e3')) {
  throw new Error(
      'Home live-shot STA poll must be 1s; AP and other views stay 2.5s; idle 4s; hidden 12s');
}
if (!ui.includes('async function loadStatus(){') ||
    !ui.includes('async function loadShots(){') ||
    !ui.includes('async function loadLog(){') ||
    !ui.includes('LOG_EVENTS_CAPACITY') ||
    !ui.includes('logEvents.splice(0,logEvents.length-LOG_EVENTS_CAPACITY)') ||
    !ui.includes('function refreshStatus(){return withPollGate(loadStatus)}') ||
    !ui.includes('function refreshShots(){return withPollGate(pollShots)}') ||
    !ui.includes('function refreshLog(){return withPollGate(loadLog)}') ||
    !(ui.includes("name==='home'||name==='settings'||name==='admin'||name==='diagnostic'") ||
      ui.includes("name === 'home' || name === 'settings' || name === 'admin' ||") ||
      ui.includes("name === 'diagnostic'")) ||
    ui.includes("name==='presets'") ||
    !(ui.includes("name==='history'") || ui.includes("name === 'history'")) ||
    !ui.includes('renderRoute(location.pathname)') ||
    !ui.includes('ensureView') ||
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
    const lazyAsset = uri.match(/^\/(partials|js)\//);
    const browserIcon = uri === '/favicon.ico' ||
        uri === '/apple-touch-icon.png' ||
        uri === '/apple-touch-icon-precomposed.png';
    if (!(statusPage && ui.includes('function statusUrl(') && ui.includes('/api/v1/status/')) &&
        !(lazyAsset && (ui.includes('/partials/') || ui.includes('/js/'))) &&
        !browserIcon) {
      throw new Error(`Registered API is not referenced by the UI: ${uri}`);
    }
  }
}

const forbiddenResponseFields = ['staPassword', 'devicePassword', 'authHash', 'authSalt'];
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
const alertChannelStatusFields =
    statusFormat.match(/\\"alertOutputChannel\\":\\"%s\\"/g) || [];
if (alertChannelStatusFields.length !== 2 ||
    !statusFormat.includes('page == StatusPage::Home') ||
    !statusFormat.includes('page == StatusPage::Settings')) {
  throw new Error(
      'alertOutputChannel must be projected only by status/home and status/settings');
}
// Shared status envelope: firmware/bootId/mutable/liveShot/ringRetain only.
// NTP → admin; serialDebug/diagnostics → diagnostic; buzzerSupported → settings.
if (!statusFormat.includes('{\\"firmwareVersion\\":\\"%s\\",\\"bootId\\":%lu,\\"configMutable\\":%s,') ||
    !statusFormat.includes('\\"webUiOverrideActive\\":%s,\\"webUiOverrideRemainingMs\\":%lu,') ||
    !statusFormat.includes('\\"configLockReason\\":\\"%s\\",\\"liveShot\\":%s"') ||
    !ui.includes("typeof s.bootId==='number'") ||
    !ui.includes('updateFirmwareFooter()')) {
  throw new Error(
      'Status shared envelope must open with firmwareVersion/bootId/configMutable/webUiOverride/liveShot');
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
          "v==='admin'?!!(typeof s.adminUnlocked==='boolean'&&s.network&&(s.adminUnlocked?(s.bleCompanion&&typeof s.bleCompanion.enabled==='boolean'&&typeof s.bleCompanion.active==='boolean'&&typeof s.bleCompanion.restartRequired==='boolean'&&typeof c.timezoneOffsetMinutes==='number'&&c.ntpServerPreset!=null&&s.ota&&typeof s.ota.available==='boolean'):typeof s.network.configState==='string'))")) {
    throw new Error(
        'statusPageOk(admin) must accept a locked payload and validate unlocked network/BLE/NTP/OTA');
  }
  if (!ui.includes(
          "v==='diagnostic'?!!(typeof s.adminUnlocked==='boolean'&&(s.adminUnlocked?(s.network&&s.time&&s.maintenance&&s.health&&s.safety&&s.scale&&s.lastCommand&&typeof s.machineState==='string'&&typeof s.state==='string'&&s.cupPresence&&typeof s.physicalActivatorOn==='boolean'&&'reedOn' in s&&typeof s.relayClosed==='boolean'&&typeof s.controlSource==='string'&&typeof s.safety.state==='string'&&typeof s.scale.streamState==='string'&&typeof c.serialDebugOutput==='boolean'&&s.compileFlags&&s.guards&&typeof s.guards.bbwEnabled==='boolean'&&s.guards.noScale&&s.guards.atm&&s.guards.slowExtraction&&s.guards.fastExtraction&&s.guards.accidentalTouch&&s.guards.cupProtection&&s.tasks&&typeof s.tasks.state==='string'):true))")) {
    throw new Error(
        'statusPageOk(diagnostic) must accept a locked payload and validate unlocked states, machine I/O, guards, diagnostic metrics, and task profiler');
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
    'psramSizeBytes', 'psramFreeBytes', 'psramLargestFreeBlockBytes',
    'bleHostAllocPsram', 'bleHostAllocFallback',
    'workBufExternal', 'jsonArenaExternal', 'allocExternalFallback',
    'resetReasonCode', 'packetGaps', 'rejectedPackets', 'reconnects',
    'eventsDropped', 'recoveredStaleCount', 'recoveredStaleMs',
    'lastCommand', 'loopIntervalGapMs', 'loopMaxGapMs',
    'machineState', 'physicalActivatorOn', 'reedOn', 'controlSource', 'cupPresence',
    'streamState', 'controlState', 'taskWatchdogReady', 'recoveryRequired',
    'compileFlags', 'remoteMachineControl', 'complete', 'degraded', 'scaleWorker',
    'development'
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
      !ui.includes('function applyDiagnosticStatus(') ||
      !ui.includes('dBz') ||
      !ui.includes('dCircuit') ||
      !ui.includes('dArch') ||
      !ui.includes('Compile flags') ||
      !ui.includes('s.compileFlags') ||
      !html.includes('paddleOnly') ||
      !html.includes('id="dMt"') ||
      !css.includes('html.momentaryMachine .paddleOnly') ||
      !css.includes('.momentaryOnly') ||
      !ui.includes('function applyMachineTypeUi(')) {
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
if (!network.includes('ShotStopperDebugExport.h') ||
    !network.includes('DEBUG_EXPORT_SCHEMA_VERSION') ||
    !network.includes('exportSchemaVersion') ||
    !network.includes('debugExportHandler') ||
    !network.includes('/api/v1/debug/export') ||
    !network.includes('\\"guards\\"') ||
    !network.includes('\\"bbwEnabled\\"') ||
    !firmware.includes('copyDebugExportExtras') ||
    !firmwareCore.includes('const ControlStatusSnapshot &control') ||
    !network.includes(
        'self.callbacks_.copyDebugExportExtras(work.debugExport, work.control)') ||
    !network.includes(
        'return sendCopiedChunk(request, text, strlen(text)) == ESP_OK') ||
    network.includes('httpd_resp_send_chunk(request, text, HTTPD_RESP_USE_STRLEN)') ||
    !firmware.includes('ShotStopperDebugExport.h') ||
    !ui.includes('/api/v1/debug/export') ||
    !ui.includes('exportDebugDataButton') ||
    !html.includes('id="exportDebugDataButton"') ||
    !network.slice(
        network.indexOf('esp_err_t ShotStopperNetwork::debugExportHandler'),
        network.indexOf('esp_err_t ShotStopperNetwork::debugExportHandler') +
            450)
        .includes('requireAdminUnlock(request)')) {
  throw new Error(
      'Diagnostic must expose guards status and GET /api/v1/debug/export with schema version');
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
if (!logHandler.includes('requireAdminUnlock(request)') ||
    logHandler.includes('authenticate(request')) {
  throw new Error('Diagnostic log must require admin unlock, not HTTP authenticate()');
}
if (logHandler.includes('"message":"%s"') ||
    !logHandler.includes('sendJsonStringChunk(request, message)')) {
  throw new Error('Log event messages must be JSON-escaped via sendJsonStringChunk');
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
    !network.includes('WIFI_ALL_CHANNEL_SCAN') ||
    !network.includes('WIFI_CONNECT_AP_BY_SIGNAL') ||
    !network.includes('setScanMethod') ||
    !network.includes('setSortMethod') ||
    !network.includes('all-channel RSSI sort') ||
    !network.includes('preferStaWifiCoex') ||
    network.includes('findBestStaCandidate') ||
    !network.includes('stopSoftApKeepStation') ||
    !network.includes('wifiScanInProgress') ||
    !network.includes('STA_RECOVERY_ATTEMPT_MS')) {
  throw new Error(
      'Network must use STA-first boot, SoftAP when unassociated, WIFI_AP_STA while retrying STA, pause retries during Wi-Fi scan, and associate via IDF all-channel RSSI sort (no incomplete BSSID lock)');
}
if (!domain.includes('selectBestStaAp') ||
    !domain.includes('StaApScanEntry')) {
  throw new Error('Strongest-AP selection helpers must live in Domain for host tests');
}
if (!network.includes('staBssid') ||
    !serialCli.includes('staBssid=')) {
  throw new Error('Associated AP BSSID must be exposed on status and WIFI_STATUS');
}
if (!serialCli.includes('workBufExternal=') ||
    !serialCli.includes('jsonArenaExternal=') ||
    !serialCli.includes('allocExternalFallback=')) {
  throw new Error(
      'Serial HEALTH must report WorkBuf/JSON arena placement and allocExternalFallback');
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
if (!network.includes('never auto-raise SoftAP') ||
    !network.includes('on link loss') ||
    !network.includes('SoftAP via AP_START or reboot')) {
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
    !alertTone.includes('BuzzerCue::SCALE_CONNECTED') ||
    !alertTone.includes('BuzzerCue::SCALE_LOST') ||
    !firmware.includes('requestScaleBrewBeep(') ||
    !firmware.includes('cancelScaleBrewBeep(session.id)') ||
    !firmware.includes('onFirstDropsDetected') ||
    !firmware.includes('notifyRetareFlowDetected') ||
    !firmware.includes('retareFlowFirstDetectedAtMs') ||
    !firmware.includes('bbwProtectionActive') ||
    !firmware.includes('classifyAccidentalTouch') ||
    !firmware.includes('stepFirstFlow') ||
    !firmware.includes('accidentalTouchHolding') ||
    !firmware.includes('retareWindowOpen') ||
    !firmware.includes('scale.supportsTareStartTimer()') ||
    !firmware.includes('alertOutputChannel') ||
    !firmware.includes('applyBookooConnectBeepPolicy') ||
    !firmware.includes('requestBookooSilenceIfConfigured') ||
    !firmware.includes('emitCommandAlert') ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer') ||
    !firmware.includes('emitCircuitCycleAlert') ||
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
if (!alertChannel.includes('AlertKind::CommandImmediate') ||
    !alertChannel.includes('AlertKind::CommandFallback') ||
    !alertChannel.includes('AlertOutputChannel::BUZZER_ONLY') ||
    emitCommandImpl.includes('if (commandAlertUsesBuzzer())') ||
    /if \(channel == AlertOutputChannel::BUZZER_ONLY\) \{\s*emitLocalAlertBuzzer/.test(
        emitCommandImpl)) {
  throw new Error('Buzzer-routed command alerts must not wait for BLE results');
}
const immediateCommandAlertCalls =
    firmware.split('emitImmediateCommandAlertIfBuzzer(').length - 1;
const circuitCycleAlertCalls = firmware.split('emitCircuitCycleAlert(').length - 1;
if (immediateCommandAlertCalls < 3 || circuitCycleAlertCalls < 4 ||
    !firmware.includes(
        'emitCircuitCycleAlert(session.startedWithScale && session.config.autoTare &&\n                            session.config.canTareStartTimer\n                        ? AlertEvent::TARE_START\n                        : AlertEvent::START_TIMER,\n                    true);') ||
    !firmware.includes(
        'if (shotCompletionGetsLongBeep(reason)) {\n      // Completion LONG replaces the stop-timer SINGLE so ends are one cue.\n      requestCompletionAlert();\n    } else {\n      emitCircuitCycleAlert(AlertEvent::STOP_TIMER, true);') ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer(AlertEvent::TARE);\n  markRetareEnded') ||
    !firmware.includes('emitCircuitCycleAlert(AlertEvent::START_TIMER, true);')) {
  throw new Error('Command alerts must fire at circuit/paddle/retare, not after BLE');
}
if (firmware.includes('SCALE_COMPLETION_BEEP_DELAY_MS') ||
    firmware.includes('scheduleScaleCompletionBeep') ||
    firmware.includes('scaleCompletionBeepDueAtMs') ||
    !firmware.includes('void requestCompletionAlert()')) {
  throw new Error('Completion beep must fire at machine circuit open with no emission delay');
}
if (firmware.includes('maybeCaptureScaleStartLag') ||
    firmware.includes('scaleStartLagCaptured') ||
    firmware.includes('scaleTimerStopDelayMsForCycle') ||
    firmware.includes('setAdvertisingPaused(preferBluetooth)') ||
    !firmware.includes('remoteTimerStartSettled') ||
    !firmware.includes(
        'command.type == ScaleCommandType::STOP_TIMER &&\n      session.remoteTimerStartSettled')) {
  throw new Error('Scale timer stop must catch up live, wait for start, and not BLE.advertise() on machine circuit open');
}

(async () => {
const generated = await webUi.generate();
if (!generated.assetTag || !generated.cacheVersion ||
    !generated.html.includes(`v=${generated.cacheVersion}`) ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssetsGzip.h'), 'utf8')
         .includes(`WEB_UI_ASSET_TAG[] = "${generated.assetTag}"`) ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssetsGzip.h'), 'utf8')
         .includes(`WEB_UI_ETAG[] = ${JSON.stringify('"' + generated.cacheVersion + '"')}`)) {
  throw new Error('Web UI cache-buster must embed FW version + asset content tag');
}
const roundTrip = zlib.gunzipSync(generated.gzip).toString('utf8');
if (roundTrip !== generated.html) {
  throw new Error('Generated gzip Web UI does not round-trip to the minified HTML');
}
if (!generated.html.includes('id="view-home"') ||
    !generated.html.includes('id="stopButton"') ||
    !generated.html.includes('Start shot') ||
    generated.html.includes('virtualPaddle') ||
    generated.html.includes('<section id="view-home" class="view" data-view="home"></section>')) {
  throw new Error('Generated shell must embed home partial markup');
}
const jsRoundTrip = zlib.gunzipSync(generated.jsGzip).toString('utf8');
if (jsRoundTrip !== generated.js) {
  throw new Error('Generated gzip Web JS does not round-trip to the minified JS');
}
if (!generated.js.includes('import') || !generated.runtimeJs.includes('export') ||
    !generated.js.includes('/api/v1/control/paddle') ||
    generated.js.includes('/js/home.js') ||
    !generated.secondaryJs.includes('export') ||
    !generated.secondaryJs.includes('views') ||
    !appJsSource.includes('__homeModule') ||
    !appJsSource.includes('/js/secondary.js')) {
  throw new Error('Generated Web UI JS must remain ES modules (shell + runtime + secondary)');
}
const runtimeRoundTrip = zlib.gunzipSync(generated.runtimeGzip).toString('utf8');
if (runtimeRoundTrip !== generated.runtimeJs) {
  throw new Error('Generated gzip runtime JS does not round-trip');
}
const secondaryRoundTrip =
    zlib.gunzipSync(generated.secondaryGzip).toString('utf8');
if (secondaryRoundTrip !== generated.secondaryJs) {
  throw new Error('Generated gzip secondary JS does not round-trip');
}
const settingsRoundTrip =
    zlib.gunzipSync(generated.settingsGzip).toString('utf8');
if (settingsRoundTrip !== generated.settingsJs) {
  throw new Error('Generated gzip settings JS does not round-trip');
}
for (const name of webUi.LAZY_PARTIALS) {
  const partialRt = zlib.gunzipSync(generated.partialGzip[name]).toString('utf8');
  if (partialRt !== generated.partials[name]) {
    throw new Error('Generated gzip partial does not round-trip: ' + name);
  }
}
const cssRoundTrip = zlib.gunzipSync(generated.cssGzip).toString('utf8');
if (cssRoundTrip !== generated.css) {
  throw new Error('Generated gzip Web CSS does not round-trip to the minified CSS');
}
if (generated.gzip.length > 4096) {
  throw new Error('Compressed Web UI shell HTML exceeds the 4 KiB gzip budget');
}
if (generated.jsGzip.length > 6144) {
  throw new Error('Compressed Web UI shell JS exceeds the 6 KiB gzip budget');
}
if (generated.runtimeGzip.length > 26500) {
  throw new Error('Compressed Web UI runtime JS exceeds the 25 KiB gzip budget');
}
if (generated.secondaryGzip.length > 4096) {
  throw new Error('Compressed secondary view JS exceeds the 4 KiB gzip budget');
}
if (generated.settingsGzip.length > 4096) {
  throw new Error('Compressed settings view JS exceeds the 4 KiB gzip budget');
}
if (generated.cssGzip.length > 6144) {
  throw new Error('Compressed Web CSS exceeds the 6 KiB gzip budget');
}
if (generated.combined > 52224) {
  throw new Error('Combined Web UI gzip exceeds the 51 KiB flash budget');
}
if (!network.includes('#include "ShotStopperWebAssetsGzip.h"') ||
    network.includes('#include "ShotStopperWebAssets.h"')) {
  throw new Error('Firmware must embed the gzip Web UI, not the HTML source string');
}
if (!network.includes('SHOT_STOPPER_WEB_UI_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_UI_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_RUNTIME_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_SECONDARY_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_PARTIAL_SETTINGS_GZIP') ||
    !network.includes('WEB_UI_ETAG') ||
    network.includes('SHOT_STOPPER_WEB_PARTIAL_HOME_GZIP') ||
    network.includes('SHOT_STOPPER_WEB_VIEW_HOME_GZIP') ||
    network.includes('formatWebUiEtag') ||
    network.includes('SHOT_STOPPER_WEB_LOGO_GZIP') ||
    !network.includes('"Content-Encoding"') ||
    !network.includes('"gzip"')) {
  throw new Error('GET /, assets, partials, and view modules must send precompressed gzip bodies without logo');
}
if (network.includes('zlib.h') || network.includes('miniz.h') ||
    /mz_compress|deflateInit|gzipCompress/.test(network)) {
  throw new Error('Firmware must not compress the Web UI at runtime');
}
if (!network.includes('sendCopiedBody(request, SHOT_STOPPER_WEB_UI_GZIP') ||
    !network.includes('serveImmutableGzip') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !network.includes('return sendCopiedBody(request, json, length)') ||
    !network.includes('HTTP_DRAM_BOUNCE_BYTES') ||
    !network.includes('g_httpSendBounce') ||
    !network.includes('allocExternal(sizeof(NetworkWorkBuf))') ||
    !network.includes(
        'allocExternal(sizeof(wifi_ap_record_t) * kWifiScanFetchMax)') ||
    !psram.includes('inline void *allocExternal(size_t bytes)') ||
    !jsonArena.includes('jsonArenaIsExternal()') ||
    !network.includes(
        'sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem))')) {
  throw new Error(
      'HTTP bodies must copy through internal RAM before tcp_write; large work buffers live in PSRAM heap');
}
if (!network.includes('If-None-Match')) {
  throw new Error('GET / must honor If-None-Match for cached Web UI revalidation');
}
const rootHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::rootHandler');
const jsHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::jsHandler');
const cssHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::cssHandler');
const runtimeHandlerStart =
    network.indexOf('esp_err_t ShotStopperNetwork::runtimeJsHandler');
const browserIconHandlerStart =
    network.indexOf('esp_err_t ShotStopperNetwork::browserIconHandler');
const notFoundHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::notFoundHandler');
const serveImmutableStart = network.indexOf('static esp_err_t serveImmutableGzip');
if (rootHandlerStart < 0 || jsHandlerStart < 0 || cssHandlerStart < 0 ||
    runtimeHandlerStart < 0 || browserIconHandlerStart < 0 ||
    notFoundHandlerStart < 0 ||
    statusHandlerStart < 0 || serveImmutableStart < 0 ||
    network.includes('logoHandler') ||
    network.includes('loginHandler') ||
    !(rootHandlerStart < jsHandlerStart && jsHandlerStart < cssHandlerStart &&
      cssHandlerStart < runtimeHandlerStart &&
      runtimeHandlerStart < browserIconHandlerStart &&
      browserIconHandlerStart < notFoundHandlerStart &&
      notFoundHandlerStart < statusHandlerStart)) {
  throw new Error('rootHandler/jsHandler/cssHandler/runtime/notFoundHandler order not found');
}
const rootHandler = network.slice(rootHandlerStart, jsHandlerStart);
const serveImmutable = network.slice(serveImmutableStart, rootHandlerStart);
const jsHandler = network.slice(jsHandlerStart, cssHandlerStart);
const cssHandler = network.slice(cssHandlerStart, runtimeHandlerStart);
const browserIconHandler = network.slice(browserIconHandlerStart, notFoundHandlerStart);
const notFoundHandler = network.slice(notFoundHandlerStart, statusHandlerStart);
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
if (serveImmutable.includes('no-store') ||
    !serveImmutable.includes('max-age=31536000') ||
    !serveImmutable.includes('immutable') ||
    !serveImmutable.includes('STATUS_NOT_MODIFIED') ||
    !serveImmutable.includes('"Connection"') ||
    !serveImmutable.includes('"close"')) {
  throw new Error('Immutable gzip assets must use long-cache ETag/304 and Connection close');
}
if (!jsHandler.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !jsHandler.includes('application/javascript')) {
  throw new Error('GET /app.js must serve immutable gzip JS');
}
if (!cssHandler.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !cssHandler.includes('text/css')) {
  throw new Error('GET /app.css must serve immutable gzip CSS');
}
if (network.includes('logoHandler') || network.includes('SHOT_STOPPER_WEB_LOGO')) {
  throw new Error('Firmware must not serve /logo.svg');
}
if (!browserIconHandler.includes('STATUS_NO_CONTENT') ||
    !browserIconHandler.includes('max-age=31536000') ||
    !browserIconHandler.includes('immutable') ||
    browserIconHandler.includes('302 Found') ||
    browserIconHandler.includes('Location')) {
  throw new Error('Safari icon probes must 204 with long cache and must not 302 to /');
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
if (!network.includes('requireActiveWebUiClient') ||
    network.includes('heartbeatHandler') ||
    network.includes('/api/v1/heartbeat') ||
    networkHeader.includes('WEB_PADDLE_HEARTBEAT_TIMEOUT_MS') ||
    network.includes('WEB_PADDLE_HEARTBEAT_TIMEOUT_MS') ||
    network.includes('heartbeatStopSent_')) {
  throw new Error(
      'WebUI claim must replace POST /heartbeat; web paddle heartbeat circuit timeout must be gone');
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
if (!js.includes('withPollGate(async()=>{if(scanBusy||!webUiPollingActive())return;scanBusy=true') ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/shots/clear'") ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/shots/delete'") ||
    js.includes("withCommandGate(async()=>{try{await api('/api/v1/logout'")) {
  throw new Error('Wi-Fi scan and shot clear/delete must use poll/command gates without login');
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

{
  const start = js.indexOf('function lastCurveWeightG(');
  const end = js.indexOf('function populateTimezoneOptions(');
  if (start < 0 || end < 0 || end <= start) {
    throw new Error('Shot spark helpers not found for matrix checks');
  }
  const helpers = new Function(
      '"use strict";' + js.slice(start, end) +
      ';return{buildShotSparkModel:buildShotSparkModel,shotDisplayFlowGS:shotDisplayFlowGS,fillChartTicks:fillChartTicks};')();
  if (helpers.buildShotSparkModel(null) !== null) {
    throw new Error('Spark model must tolerate a null shot (idle Home panel)');
  }
  const kids = [];
  const tickEl = {
    replaceChildren(){kids.length=0},
    appendChild(n){kids.push(n)},
  };
  const prevDoc = global.document;
  global.document = {
    createElement(){return {className:'',style:{},textContent:''}},
  };
  try {
    helpers.fillChartTicks(tickEl, [[0, '0 g'], [18, '18 g'], [36, '36 g']], 36);
    const spaced = kids.map((n) => parseFloat(n.style.left));
    if (kids.length !== 3 || kids[0].textContent !== '0 g' ||
        kids.some((n) => n.className !== 'ruleTick') ||
        spaced[0] !== 0 || Math.abs(spaced[1] - 50) > 0.01 || spaced[2] !== 100) {
      throw new Error('fillChartTicks must place labels at exact values');
    }
    helpers.fillChartTicks(tickEl, [[0, '0 g'], [36, '36 g'], [18, '18 g']], 36);
    if (kids.map((n) => n.textContent).join('|') !== '0 g|18 g|36 g') {
      throw new Error('fillChartTicks must sort labels by position');
    }
    helpers.fillChartTicks(tickEl, [[0, '0 g'], [35.5, '35.5 g'], [36, '36 g']], 36);
    const close = kids.map((n) => parseFloat(n.style.left));
    if (kids.map((n) => n.textContent).join('|') !== '0 g|35.5 g|36 g' ||
        Math.abs(close[1] - 35.5 / 36 * 100) > 0.01 || close[2] !== 100) {
      throw new Error('fillChartTicks must keep close labels on exact values');
    }
  } finally {
    if (prevDoc === undefined) delete global.document;
    else global.document = prevDoc;
  }
  const fast15 = helpers.buildShotSparkModel({
    wCg: [20, 800, 1600, 2400, 3200, 3600, 4000, 4370],
    wDtS: 2,
    durationS: 15.1,
    firstDropS: 4.5,
    dropS: 4.5,
    dropCg: 50,
    extendedS: 13.3,
    extCg: 3600,
    endS: 15.1,
    endCg: 4370,
    extractionExtended: true,
    goalG: 36,
  });
  if (!fast15 || !fast15.segs.some((s) => s.color === '#d97706' &&
      s.pts[s.pts.length - 1].t >= 15)) {
    throw new Error('Spark 15.1s Fast guard must paint orange through ended');
  }
  const slowLate = helpers.buildShotSparkModel({
    wCg: Array.from({length: 14}, (_, i) => (i + 1) * 200),
    wDtS: 2,
    durationS: 26.4,
    firstDropS: 8.4,
    extendedS: 24.1,
    extCg: 2600,
    endS: 26.4,
    endCg: 2800,
    slowExtractionExtended: true,
    goalG: 36,
  });
  if (!slowLate || !slowLate.segs.some((s) => s.color === '#2563eb' &&
      s.pts[0].t >= 24)) {
    throw new Error('Spark late Slow guard must paint blue from extended');
  }
  const atm = helpers.buildShotSparkModel({
    wCg: [100, 800, 1600, 2400, 2800, 3000, 3200, 3400],
    wDtS: 2,
    durationS: 18,
    firstDropS: 4,
    atmS: 12,
    atmCg: 2800,
    endS: 18,
    endCg: 3400,
    goalG: 36,
  });
  const gray = atm && atm.segs.find((s) => s.color === 'var(--mu)');
  if (!gray || gray.pts.some((p) => p.cg !== 2800) ||
      gray.pts[0].t < 12 || gray.pts[gray.pts.length - 1].t < 18) {
    throw new Error('Spark A→M must be flat gray at last scale weight through ended');
  }
  const flow = helpers.shotDisplayFlowGS({
    avgFlowGS: null,
    actualG: 43.7,
    durationS: 15.1,
    firstDropS: 4.5,
  });
  if (!(flow > 4.12 && flow < 4.13)) {
    throw new Error('History flow fallback must use actual/(duration-firstDrop)');
  }
}

// Over-the-air update safety contract. The controller lives inside a closed
// machine, so every rule below exists to keep a bad image from becoming the
// boot image, and a bad boot from becoming permanent.
{
  const ota = fs.readFileSync(path.join(sketchDir, 'ShotStopperOta.cpp'), 'utf8');
  const otaHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperOta.h'), 'utf8');
  const otaImage = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperOtaImage.h'), 'utf8');
  const version = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperVersion.h'), 'utf8');

  // The transfer must target the spare slot, never the one that is executing.
  if (!ota.includes('esp_ota_get_next_update_partition(nullptr)') ||
      !ota.includes('target != running')) {
    throw new Error('OTA must stage into the inactive slot only');
  }
  // Any early exit has to release the OTA handle; a leaked handle would keep
  // the flash driver's state machine open until the next reboot.
  if (!ota.includes('esp_ota_abort(handle)') || !ota.includes('esp_ota_end(handle)')) {
    throw new Error('OTA must abort or end every esp_ota_begin handle');
  }
  // The boot selection changes in exactly one place, after re-reading the
  // staged identity straight from flash.
  if ((ota.match(/esp_ota_set_boot_partition\(/g) || []).length !== 1 ||
      !/if \(!reconfirmStagedTag\(\)\) \{[\s\S]{0,400}?esp_ota_set_boot_partition\(target\)/
          .test(ota)) {
    throw new Error(
      'OTA must re-verify the staged image immediately before switching the boot partition');
  }
  // Rolling back must never reboot on its own: machine circuit has to be opened first.
  if (ota.includes('esp_ota_mark_app_invalid_rollback_and_reboot') ||
      ota.includes('ESP.restart') || ota.includes('esp_restart')) {
    throw new Error('OTA must not restart the controller directly; machine circuit is opened first');
  }
  if (!ota.includes('rejected_ = true') ||
      !/if \(rejected_\) \{\s*return false;/.test(ota)) {
    throw new Error(
      'rejectRunningImage must record the rejection so confirmRunningImage cannot cancel it');
  }
  if (!css.includes('.otaProgress.hidden{display:none}') &&
      !css.includes('.otaProgress.hidden { display: none }')) {
    throw new Error('.otaProgress.hidden must override display:block so the bar can hide');
  }
  if (!ota.includes('esp_ota_check_rollback_is_possible()')) {
    throw new Error(
      'OTA must confirm a bootable alternative exists before arming a rollback');
  }
  // Running out of URI handler slots makes registration fail at runtime, which
  // stops the HTTP server outright: no Web UI, and no way to update over the
  // air. It is a silent runtime failure, so it has to be caught here.
  {
    const limit = network.match(/config\.max_uri_handlers = (\d+);/);
    if (limit == null) {
      throw new Error('Could not find config.max_uri_handlers');
    }
    const routes = (network.match(/registerHandler\(server_/g) || []).length;
    if (routes >= Number(limit[1])) {
      throw new Error(
        `${routes} routes registered with max_uri_handlers=${limit[1]}; raise ` +
        'the limit so registration keeps headroom');
    }
  }

  // The OTA object is spliced into the admin status, so an unclosed brace here
  // would make the whole Admin page unparseable, not just the OTA panel.
  if (!/const size_t tagCapacity = capacity - 1;/.test(network) ||
      !/if \(used \+ 2 > capacity\) \{\s*\n\s*snprintf\(buffer, capacity, "\{\\"available\\":false\}"\);/
        .test(network)) {
    throw new Error(
      'buildOtaJson must reserve room for its closing brace and fall back to a ' +
      'valid object when the OTA JSON does not fit');
  }
  // Recovering an image too broken to run any firmware code is the bootloader's
  // job, so losing that configuration must break the build, not the machine.
  for (const symbol of ['CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE',
                        'CONFIG_APP_ROLLBACK_ENABLE',
                        'CONFIG_BOOTLOADER_WDT_ENABLE']) {
    if (!new RegExp(`#if !defined\\(${symbol}\\)\\s*\\n#error`).test(otaHeader)) {
      throw new Error(`OTA header must fail the build when ${symbol} is missing`);
    }
  }
  // A build that lost its identity marker could never be verified by the next
  // one, and would also accept a foreign image.
  if (!version.includes('FW_IMAGE_TAG_STRING') ||
      !version.includes('FW_BOARD_ARCH_STRING') ||
      !ota.includes('FW_IMAGE_TAG_STRING')) {
    throw new Error('Firmware must embed the Shot Stopper OTA image tag');
  }
  // The needle is assembled at run time so a compiled image contains exactly
  // one contiguous copy of the prefix: its own tag, never the search pattern.
  if (otaImage.includes('"SHOTSTOPPER_FW_TAG_V1|"') ||
      !otaImage.includes('OTA_TAG_PREFIX_PART_1') ||
      !otaImage.includes('OTA_TAG_PREFIX_PART_2')) {
    throw new Error('OTA tag prefix must stay split so it is not embedded contiguously');
  }
  if (!otaHeader.includes('PENDING_VERIFY') ||
      !ota.includes('if (!confirmed_) {')) {
    throw new Error(
      'OTA must refuse to stage while the running image is still pending verification');
  }

  // The Arduino core would confirm the image inside initArduino(), before this
  // firmware has proven anything.
  if (!/extern "C" bool verifyRollbackLater\(\) \{\s*return true;/.test(firmware)) {
    throw new Error('Firmware must defer OTA rollback verification to the network layer');
  }
  if (!firmware.includes('ShotStopperOta::instance().begin()')) {
    throw new Error('Firmware must initialise the OTA module during setup');
  }

  // The unsafe WebUI override may relax settings, never a firmware update.
  const otaHandlers = network.slice(network.indexOf('ShotStopperNetwork::otaStatusHandler'));
  if (otaHandlers.includes('webUiOverrideAllowed') ||
      otaHandlers.includes('webUiConfigurationAllowed')) {
    throw new Error('OTA handlers must not honour the unsafe WebUI override');
  }
  if ((otaHandlers.match(/authorizeOtaRequest\(request\)/g) || []).length !== 4) {
    throw new Error('Every OTA route must authenticate the request');
  }
  if (!network.includes('devicePasswordsMatch(password, expected)')) {
    throw new Error(
      'OTA authentication must compare the device password in constant time');
  }
  if (network.includes('tokenAuthorized = !factoryPassword && otaTokensMatch') ||
      network.includes('passwordAuthorized = !factoryPassword') ||
      (network.includes('authorizeOtaRequest') &&
       network.slice(network.indexOf('ShotStopperNetwork::authorizeOtaRequest'),
                     network.indexOf('ShotStopperNetwork::buildOtaJson'))
           .includes('if (factoryPassword)'))) {
    throw new Error(
      'OTA authentication must accept the factory default device password');
  }
  const currentDeviceAt = html.indexOf('id="currentDevicePassword"');
  const newDeviceAt = html.indexOf('id="newDevicePassword"');
  const confirmDeviceAt = html.indexOf('id="confirmDevicePassword"');
  if (currentDeviceAt >= 0) {
    throw new Error('Device password form must not ask for the current password after admin unlock');
  }
  if (newDeviceAt < 0 || confirmDeviceAt < 0 || !(newDeviceAt < confirmDeviceAt)) {
    throw new Error('Device password form must ask for the new password and confirmation');
  }
  if (!js.includes("newPassword:$('newDevicePassword').value") ||
      !network.includes('"newPassword"') ||
      network.includes('"currentPassword"') ||
      !network.includes('DEVICE_PASSWORD_INVALID') ||
      !network.includes('requireAdminUnlock(request)')) {
    throw new Error('Web device password change must require admin unlock, not the current password');
  }
  if (!html.includes('id="adminLockPanel"') ||
      !html.includes('id="adminUnlockPassword"') ||
      !html.includes('id="adminUnlockButton"') ||
      !html.includes('Unlock administration') ||
      !html.includes('id="adminControls"') ||
      !html.includes('15 minutes after the last privileged action') ||
      !html.includes('Lock closes it now') ||
      !html.includes('id="navAdminLock"') ||
      !html.includes('id="adminLockButton"') ||
      html.includes('id="homeAdminLock"') ||
      !html.includes('this window will confirm automatically') ||
      html.includes('unlock to confirm') ||
      js.includes('Unlock to confirm') ||
      js.includes('unlock to confirm') ||
      !js.includes('function lockAdminUi()') ||
      !js.includes('function lockAdmin()') ||
      !js.includes("aria-expanded','false');clearTimeout(scanTimer);scanTimer=0;api('/api/v1/admin/lock'") ||
      !js.includes('function syncAdminSessionUi(unlocked)') ||
      !js.includes('stopViewPolls();lockAdminUi();setMutable(false)') ||
      !js.includes('/api/v1/admin/unlock') ||
      !js.includes('/api/v1/admin/lock') ||
      !js.includes("closest('#adminLockPanel") ||
      !css.includes('.navLock.hidden,.textLock.hidden{display:none}') ||
      !css.includes('.textLock') ||
      !network.includes('/api/v1/admin/unlock') ||
      !network.includes('/api/v1/admin/lock') ||
      !network.includes('ShotStopperNetwork::adminLockHandler') ||
      !network.includes('ADMIN_LOCKED') ||
      !network.includes('ADMIN_UNLOCK_COOLDOWN') ||
      !network.includes('ADMIN_UNLOCK_IDLE_MS') ||
      !network.includes('grantAdminUnlock') ||
      !network.includes('secretsMatch(password, expected)') ||
      js.includes('loginHandler') ||
      network.includes('ShotStopperNetwork::loginHandler')) {
    throw new Error('Admin must gate behind a temporary device-password unlock on firmware and UI');
  }
  if (!html.includes('id="diagnosticLockPanel"') ||
      !html.includes('id="diagnosticUnlockPassword"') ||
      !html.includes('id="diagnosticUnlockButton"') ||
      !html.includes('Unlock diagnostics') ||
      !html.includes('id="diagnosticControls"') ||
      !html.includes('id="diagnosticLockButton"') ||
      !js.includes('diagnosticLockPanel') ||
      !js.includes('if(!diagnosticUnlocked||logBusy') ||
      !js.includes('Could not unlock diagnostics.') ||
      !js.includes("closest('#adminLockPanel,#diagnosticLockPanel")) {
    throw new Error(
        'Diagnostic must gate behind the same admin unlock as Admin, locked by default');
  }
  if (runtimeJs.includes('row.innerHTML') ||
      runtimeJs.includes("shotType[0]!=='a'|y<1") ||
      runtimeJs.includes('pollChain=run.catch(()=>{})') ||
      !runtimeJs.includes('pollChain=run.catch(console.warn)')) {
    throw new Error(
        'Shot history must use DOM textContent, logical OR in stats, and must not swallow poll errors');
  }
  {
    const start = firmwareCore.indexOf('void completeBootRecovery(');
    const end = firmwareCore.indexOf('void resumePendingBootRecovery(');
    const body = start >= 0 && end > start ? firmwareCore.slice(start, end) : '';
    const clearAt = body.indexOf('(void)clearRecoveryIntent()');
    const holdAt = body.lastIndexOf('holdFailedBootRecovery');
    if (clearAt < 0 || holdAt < 0 || holdAt > clearAt) {
      throw new Error(
          'Successful boot recovery must clear intent without hanging on clear failure');
    }
  }
  {
    const factoryFnStart = network.indexOf('ShotStopperNetwork::factoryResetHandler');
    const passwordFnStart = network.indexOf('ShotStopperNetwork::devicePasswordHandler');
    const restartFnStart = network.indexOf('ShotStopperNetwork::restartHandler');
    const bleFnStart = network.indexOf('ShotStopperNetwork::bleCompatHandler');
    const timeFnStart = network.indexOf('ShotStopperNetwork::timeSyncHandler');
    for (const [label, start] of [
      ['factory-reset', factoryFnStart],
      ['device-password', passwordFnStart],
      ['restart', restartFnStart],
      ['ble-compat', bleFnStart],
      ['task-profiler', network.indexOf('ShotStopperNetwork::taskProfilerHandler')],
      ['time-sync', timeFnStart],
      ['wifi-scan-start', network.indexOf('ShotStopperNetwork::wifiScanStartHandler')],
      ['wifi-scan-status', network.indexOf('ShotStopperNetwork::wifiScanStatusHandler')],
      ['paddle', network.indexOf('ShotStopperNetwork::paddleHandler')],
      ['rinse', network.indexOf('ShotStopperNetwork::rinseHandler')],
      ['stop', network.indexOf('ShotStopperNetwork::stopHandler')]
    ]) {
      if (start < 0 ||
          !network.slice(start, start + 500).includes('requireAdminUnlock(request)')) {
        throw new Error(label + ' must require admin unlock');
      }
    }
    const unlockFnStart = network.indexOf('ShotStopperNetwork::unlockHandler');
    if (unlockFnStart < 0 ||
        !network.slice(unlockFnStart, unlockFnStart + 600).includes('requireAdminUnlock(request)')) {
      throw new Error('ui/unlock must require admin unlock');
    }
    const networkFn = network.slice(
        network.indexOf('ShotStopperNetwork::networkHandler'),
        network.indexOf('ShotStopperNetwork::wifiScanStartHandler'));
    if (!networkFn.includes('requireAdminUnlock(request)') ||
        !networkFn.includes('strcmp(action, "confirm") != 0')) {
      throw new Error('Network save/forget must require admin unlock; confirm may stay ungated');
    }
    if (networkFn.includes('settingsCopy()')) {
      throw new Error(
          'networkHandler must not return PersistedSettings by value on the httpd stack');
    }
  }
  if ((statusFormat.match(/\\"adminUnlocked\\":%s/g) || []).length < 3) {
    throw new Error('status/home, status/admin, and status/diagnostic must report adminUnlocked');
  }
  if ((statusFormat.match(/\\"adminUnlocked\\":%s,\\"development\\":%s/g) || []).length < 3) {
    throw new Error('status/home, status/admin, and status/diagnostic must report development with adminUnlocked');
  }
  if (!network.includes('page == StatusPage::Admin || page == StatusPage::Home') ||
      !network.includes('page == StatusPage::Diagnostic') ||
      !ui.includes("v==='home'?!!(typeof s.adminUnlocked==='boolean'") ||
      !js.includes('function syncAdminSessionUi(unlocked)') ||
      !js.includes('syncAdminSessionUi(admin)') ||
      !js.includes('updateHomeAdminActions(on)')) {
    throw new Error('Home must report and honor adminUnlocked for the Actions panel');
  }
  if (!network.includes(
          'page == StatusPage::Admin || page == StatusPage::Diagnostic') ||
      !network.includes('self.touchAdminUnlock()') ||
      /if \(adminUnlocked\) \{\s*self\.touchAdminUnlock\(\);/.test(network)) {
    throw new Error(
        'Home status must report adminUnlocked without sliding idle; Admin and Diagnostic polls may renew');
  }
  {
    const start = network.indexOf('ShotStopperNetwork::adminLockHandler');
    const end = network.indexOf('bool ShotStopperNetwork::requireActiveWebUiClient');
    const body = start >= 0 && end > start ? network.slice(start, end) : '';
    if (!body.includes('clearAdminUnlock()') ||
        body.includes('requireAdminUnlock')) {
      throw new Error('Admin lock must clear unlock without requiring a still-valid unlock');
    }
  }
  if (!statusFormat.includes('!adminUnlocked') ||
      !js.includes('s.adminUnlocked')) {
    throw new Error('Locked admin status must omit Wi-Fi/BLE/OTA bodies until unlocked');
  }
  if (!network.includes('serviceOtaRollback(now)') ||
      !networkHeader.includes('OTA_CONFIRM_MIN_UPTIME_MS') ||
      !networkHeader.includes('OTA_CONFIRM_DEADLINE_MS')) {
    throw new Error('Network service must confirm or roll back a pending OTA image');
  }
  for (const code of [
    'OTA_UPLOAD_STARTED', 'OTA_IMAGE_STAGED', 'OTA_UPLOAD_REJECTED',
    'OTA_FLASH_COMMITTED', 'OTA_IMAGE_CONFIRMED', 'OTA_ROLLBACK_ARMED',
    'OTA_ROLLBACK_FAILED'
  ]) {
    if (!domain.includes(`DebugCode::${code}`)) {
      throw new Error(`Diagnostic log must report OTA event: ${code}`);
    }
  }

  // Admin page layout: the update lives between Restart and Factory reset.
  const restartAt = html.indexOf('id="restartPanel"');
  const otaAt = html.indexOf('id="otaPanel"');
  const factoryAt = html.indexOf('id="factoryResetButton"');
  if (restartAt < 0 || otaAt < 0 || factoryAt < 0 ||
      !(restartAt < otaAt && otaAt < factoryAt)) {
    throw new Error('Admin OTA panel must sit between Restart and Factory reset');
  }
  for (const id of [
    'otaStatus', 'otaRunning', 'otaStaged', 'otaFile',
    'otaProgress', 'otaVerifyButton', 'otaFlashButton', 'otaDiscardButton'
  ]) {
    if (!html.includes(`id="${id}"`)) {
      throw new Error(`Admin OTA panel is missing control: ${id}`);
    }
  }
  if (html.includes('id="otaToken"') || js.includes('otaGuardToken') ||
      js.includes('X-OTA-Token') || js.includes('X-Device-Password') ||
      js.includes('OTA token') || js.includes('ota password')) {
    throw new Error(
      'Web UI OTA must not collect a separate password after admin unlock');
  }
  if (!js.includes('xhr.setRequestHeader(WEB_UI_CLIENT_HEADER,webUiClientId)') ||
      !network.includes('adminUnlockAllowed(request)') ||
      !network.includes('devicePasswordsMatch(password, expected)') ||
      !network.includes('X-Device-Password')) {
    throw new Error(
      'OTA must accept an admin unlock session or the device password header');
  }
  // Two steps: verify writes the spare slot, a separate button flashes it.
  if (!js.includes("otaSend('/api/v1/ota',file") ||
      !js.includes("otaSend('/api/v1/ota/flash'") ||
      !js.includes("otaSend('/api/v1/ota/abort'") ||
      !js.includes("$('otaFlashButton').disabled=!ready||!staged")) {
    throw new Error('Web UI must upload/verify and flash as two separate steps');
  }
  if (!js.includes('!otaBusy&&Date.now()-lastStatusAt')) {
    throw new Error('Web UI must pause status polling during a firmware transfer');
  }
}

console.log(
  `Embedded Web UI: modules+partials valid, ${htmlBytes} bytes HTML / ${jsBytes} bytes JS source, ` +
  `${generated.gzip.length} bytes shell gzip, ${generated.jsGzip.length} bytes app.js gzip, ` +
  `${generated.runtimeGzip.length} bytes runtime gzip, ${generated.secondaryGzip.length} bytes secondary gzip, ` +
  `${generated.cssGzip.length} bytes CSS gzip, combined ${generated.combined} bytes gzip, ` +
  `${expected.size} routes checked`
);
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
