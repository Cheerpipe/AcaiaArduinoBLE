#pragma once

#include <pgmspace.h>

namespace shotstopper {

const char SHOT_STOPPER_WEB_UI[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Micra Shot Stopper</title>
<style>
body{font-family:system-ui;max-width:760px;margin:1rem auto;padding:0 1rem;color:#111;background:#fff}fieldset{margin:1rem 0;padding:1rem}label{display:block;margin:.55rem 0}input,select,button{font:inherit;padding:.4rem}input[type=number],input[type=text],input[type=password],select{width:18rem;max-width:100%}button{margin:.25rem;min-height:2.75rem}@media(min-width:641px){button{min-height:auto}}.pageFooter{margin:1.5rem 0 .5rem;color:#666;font-size:.85rem;text-align:center}.pageNav a{color:#2563eb;text-decoration:none}.pageNav a:hover{text-decoration:underline}.row{display:flex;gap:1rem;flex-wrap:wrap}.statusColumn{display:grid;grid-template-columns:1fr;gap:.75rem}.metric{overflow-wrap:anywhere}.stateReady{color:#087f23;font-weight:700}.stateActive{color:#2563eb;font-weight:700}.stateFault{color:#a00;font-weight:700}.error{color:#a00}.ok{color:#075}.warn{color:#8a4b00}.hidden{display:none}.fieldOff{opacity:.45}.fieldOff input,.fieldOff select{pointer-events:none}.cfgGroup,.diagGroup{margin:.75rem 0;padding:.75rem;border:1px dashed #ccc;border-radius:.25rem}.cfgGroup summary,.diagGroup summary{cursor:pointer;font-weight:600}.configSaveBar{position:sticky;top:0;z-index:2;background:#fff;padding:.35rem 0;margin:.35rem 0;border-bottom:1px solid #ddd;display:flex;align-items:center;gap:.75rem;flex-wrap:wrap}.shotHero{margin:.35rem 0}.shotWt{font-size:1.85rem;font-weight:700}.shotTrack{position:relative;height:1rem;background:#e5e5e5;border-radius:.3rem;margin:.35rem 0}.shotTrack #shotBar{height:100%;background:#087f23;border-radius:.3rem;width:0;max-width:150%}.shotMark{position:absolute;left:100%;top:0;bottom:0;width:2px;background:#111;transform:translateX(-1px)}#log{width:100%;height:15rem;font-family:monospace;white-space:pre;overflow:auto}small{display:block;color:#555}.fieldHint{display:block;font-size:.82em;color:#666;margin:.1rem 0 .2rem;line-height:1.35;max-width:34rem;font-weight:400}.locked{opacity:.55}.switchRow{display:flex;align-items:center;gap:.7rem;margin:.55rem 0}.switch{position:relative;display:inline-block;width:5.7rem;height:2.15rem}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;inset:0;display:flex;align-items:center;justify-content:space-between;padding:0 .42rem;border-radius:2rem;background:#9b1c1c;color:#fff;font-weight:700;font-size:.72rem;cursor:pointer;transition:.2s}.slider:before{position:absolute;content:"";height:1.65rem;width:1.65rem;left:.25rem;border-radius:50%;background:#fff;transition:.2s}.switch input:checked+.slider{background:#087f23}.switch input:checked+.slider:before{transform:translateX(3.28rem)}.switch input:focus-visible+.slider{outline:3px solid #2563eb}.switch input:disabled+.slider{cursor:not-allowed;opacity:.48}.switchOn{opacity:.45}.switch input:checked+.slider .switchOn{opacity:1}.switch input:checked+.slider .switchOff{opacity:.45}.switchState{min-width:2.3rem;font-weight:700}.btnDanger{background:#a00;color:#fff;border:1px solid #800}.btnWarn{border-color:#a00;color:#a00}#shotTableWrap{overflow:auto;max-height:16rem;margin:.5rem 0}#shotTable{width:100%;border-collapse:collapse;font-size:.85rem}#shotTable th,#shotTable td{border-bottom:1px solid #ddd;padding:.35rem .4rem;text-align:left;white-space:nowrap}#shotTable th{position:sticky;top:0;background:#fff}@media(max-width:520px){#shotTable .colFlow,#shotTable .colCut{display:none}}@media(prefers-color-scheme:dark){body{background:#121212;color:#eee}fieldset{border-color:#444}.cfgGroup,.diagGroup{border-color:#555}.configSaveBar{background:#121212;border-color:#444}#log{background:#1a1a1a;color:#ddd}.shotTrack{background:#333}#shotTable th{background:#121212}.pageNav a{color:#8cb4ff}.pageFooter{color:#888}small,.fieldHint{color:#aaa}.btnDanger{background:#c62828;border-color:#b71c1c}}
</style>
</head>
<body>
<h1>Micra Shot Stopper</h1>
<div id="sessionBar" class="authenticatedOnly hidden"><button id="logoutButton">Sign out</button></div>
<p id="message">Read-only view. Sign in to control the device.</p>
<section id="loginPanel">
  <label>Administrator password <input id="loginPassword" type="password" maxlength="63" autocomplete="current-password"></label>
  <button id="loginButton">Sign in</button>
</section>
<main id="app">
  <nav class="pageNav"><a href="#shotPanel">Shot</a> · <a href="#statusPanel">Status</a> · <a href="#actionsPanel">Actions</a> · <a href="#workflowPanel">Configuration</a> · <a href="#networkPanel">Wi-Fi</a> · <a href="#accessPointPanel">Password</a> · <a href="#shotHistoryPanel">History</a> · <a href="#logPanel">Log</a></nav>
  <fieldset id="shotPanel"><legend>Shot</legend>
    <p id="shotIdle">No active shot in progress.</p>
    <div class="shotHero"><span id="shotCurrentWeight" class="shotWt">—</span><span> / <span id="shotGoalWeight">—</span> g</span></div>
    <div class="shotTrack"><div id="shotBar"></div><div class="shotMark"></div></div>
    <small id="shotPct">—</small>
    <div class="statusColumn">
      <div class="metric"><strong>Elapsed</strong><div id="shotElapsed">—</div></div>
      <div class="metric"><strong>First drop</strong><div id="shotFirstDrop">—</div></div>
      <div class="metric"><strong>Retare</strong><div id="shotRetare">—</div></div>
      <div class="metric"><strong>Shot type</strong><div id="shotType">—</div></div>
      <div class="metric"><strong>Scale</strong><div id="shotScale">—</div></div>
    </div>
    <small id="lastCycle">No previous cycle.</small>
  </fieldset>

  <fieldset id="statusPanel"><legend>Status</legend>
    <div class="statusColumn">
      <div class="metric"><strong>State</strong><div id="state">—</div></div>
      <div class="metric"><strong>Paddle State</strong><div id="paddle">—</div></div>
      <div class="metric"><strong>CN9</strong><div id="relay">—</div></div>
      <div class="metric"><strong>CN9 Safety</strong><div id="safety">—</div></div>
      <div class="metric"><strong>Current time</strong><div id="currentTime">—</div></div>
      <div class="metric"><strong>NTP</strong><div id="ntpStatus">—</div></div>
      <div class="metric"><strong>Source</strong><div id="source">—</div></div>
      <div class="metric"><strong>Scale</strong><div id="scale">—</div></div>
    </div>
    <details class="diagGroup"><summary>Diagnostics</summary>
      <div class="statusColumn">
        <div class="metric"><strong>Uptime</strong><div id="hUptime">—</div></div>
        <div class="metric"><strong>Reset</strong><div id="hResetReason">—</div></div>
        <div class="metric"><strong>Maintenance</strong><div id="maintenance">—</div></div>
        <div class="metric"><strong>Health</strong><div id="health">—</div></div>
        <div class="metric"><strong>CPU</strong><div id="hCpu">—</div></div>
        <div class="metric"><strong>Temp</strong><div id="hTemp">—</div></div>
        <div class="metric"><strong>Peak</strong><div id="hTPeak">—</div></div>
        <div class="metric"><strong>RAM total</strong><div id="hRamT">—</div></div>
        <div class="metric"><strong>RAM used</strong><div id="hRamU">—</div></div>
        <div class="metric"><strong>RAM free</strong><div id="hRamF">—</div></div>
        <div class="metric"><strong>Last command</strong><div id="lastCommand">—</div></div>
      </div>
    </details>
  </fieldset>

  <fieldset id="actionsPanel" class="authenticatedOnly hidden"><legend>Actions</legend>
    <div class="switchRow"><span>Virtual paddle</span><label class="switch" for="virtualPaddle"><input id="virtualPaddle" type="checkbox" role="switch" aria-label="Virtual paddle"><span class="slider"><span class="switchOff">OFF</span><span class="switchOn">ON</span></span></label><span id="virtualPaddleState" class="switchState">OFF</span></div>
    <button id="rinseButton">Start rinse</button>
    <button id="stopButton" class="btnDanger">Stop shot</button>
    <button id="restartButton" class="btnWarn">Restart controller</button>
    <small id="controlPolicy">Physical paddle has priority. Stop only opens CN9.</small>
  </fieldset>

  <fieldset id="workflowPanel"><legend>Configuration</legend>
    <p id="readOnlyNotice">Sign in to edit configuration settings.</p>
    <p id="configLock" class="warn hidden">Configuration is locked while a cycle is active.</p>
    <div id="configSaveBar" class="configSaveBar authenticatedOnly hidden">
      <button class="mutable" id="saveConfigTop">Save settings</button>
      <small id="configDirtyHint" class="warn hidden">Unsaved changes</small>
    </div>
    <details class="cfgGroup" open><summary>Brew &amp; weight</summary>
      <div class="row">
        <label>Target (g)<input id="goalWeightG" type="number" min="10" max="200" required><small class="fieldHint">Stop weight for brew by weight; CN9 opens near target minus learned offset.</small></label>
        <label>CN9 limit (s)<input id="operationalWallS" type="number" min="5" max="60" step="1" required><small class="fieldHint">Max CN9 closed time per cycle; firmware hard cap is 60 s.</small></label>
      </div>
      <div class="metric"><strong>Learned stop offset (g)</strong><div id="learnedOffsetG">—</div></div>
      <label>Brew start confirmation (s)<input id="confirmationTimeoutS" type="number" min="0.5" max="30" step="0.1" required><small class="fieldHint">Min retare window + 3 s. Confirms brew start; inhibits weight stop until done.</small></label>
    </details>
    <details class="cfgGroup" open><summary>Rinse</summary>
      <div class="row">
        <label>Rinse gesture (s)<input id="rinseGestureS" type="number" min="0.1" max="5" step="0.1" required><small class="fieldHint">Maximum paddle ON time that still counts as a rinse when you release the paddle.</small></label>
        <label>Rinse duration (s)<input id="rinseDurationS" type="number" min="0.5" max="10" step="0.1" required><small class="fieldHint">How long CN9 stays closed after a rinse gesture starts.</small></label>
      </div>
    </details>
    <details class="cfgGroup"><summary>Scale &amp; retare</summary>
      <label><input id="autoTare" type="checkbox"> Automatic tare<small class="fieldHint">Send an initial tare to the scale when an automatic shot starts.</small></label>
      <label><input id="timerOnly" type="checkbox"> Timer only; do not stop by weight<small class="fieldHint">Keep tare/timer; disable weight stop and offset learning.</small></label>
      <label><input id="canTareStartTimer" type="checkbox"> Bookoo combined command<small class="fieldHint">Use the scale combined tare+start-timer command; requires automatic tare.</small></label>
      <label><input id="autoRetare" type="checkbox"> Enable automatic retare<small class="fieldHint">If a cup is placed after shot start, allow one second tare during the retare window.</small></label>
      <label class="retareOpt">Retare window (s)<input id="retareWindowS" type="number" min="0.5" max="10" step="0.1" required><small class="fieldHint">Late-cup retare window after shot start.</small></label>
      <label class="retareOpt">Minimum cup weight (g)<input id="minimumCupWeightG" type="number" min="1" max="500" step="0.1" required><small class="fieldHint">Min stable load treated as a cup for retare.</small></label>
      <label class="retareOpt">Retare stable samples<input id="retareStabilitySamples" type="number" min="2" max="10" step="1" required><small class="fieldHint">Consecutive stable readings before retare.</small></label>
      <label class="retareOpt">Retare stability tolerance (g)<input id="retareStabilityToleranceG" type="number" min="0.1" max="20" step="0.1" required><small class="fieldHint">Max weight change between stable readings.</small></label>
      <label class="retareOpt">Retare sample gap (s)<input id="retareStabilityMaxGapS" type="number" min="0.1" max="5" step="0.1" required><small class="fieldHint">Max gap between stable readings.</small></label>
      <label class="retareOpt">Retare min stable time (s)<input id="retareStabilityMinDurationS" type="number" min="0" max="2" step="0.1" required><small class="fieldHint">Min stable time before retare; 0 disables.</small></label>
    </details>
    <details class="cfgGroup"><summary>Alerts &amp; reminders</summary>
      <label><input id="brewConfirmationBeep" type="checkbox"> Beep when coffee starts<small class="fieldHint">Beep once on first coffee drop; not on confirmation timeout alone.</small></label>
      <label><input id="paddleReturnReminderBeep" type="checkbox"> Scale reminder beep until the physical paddle is switched OFF<small class="fieldHint">Repeat scale beep while paddle stays ON after CN9 opens.</small></label>
      <label class="paddleOpt">Paddle reminder interval (s)<input id="paddleReturnReminderIntervalS" type="number" min="5" max="60" step="1" required><small class="fieldHint">Time between paddle-return reminder beeps.</small></label>
      <label class="paddleOpt">Paddle reminder limit (min)<input id="paddleReturnReminderMaxDurationMin" type="number" min="1" max="60" step="1" required><small class="fieldHint">Stop beeps after this many minutes if paddle stays ON.</small></label>
    </details>
    <details class="cfgGroup"><summary>Date &amp; time</summary>
      <label>Timezone <select id="timezoneOffsetMinutes"></select></label>
      <label>NTP server <select id="ntpServerPreset"><option value="pool">pool.ntp.org</option><option value="google">time.google.com</option><option value="cloudflare">time.cloudflare.com</option><option value="nist">time.nist.gov</option></select></label>
      <label>Custom NTP (optional) <input id="ntpServerCustom" type="text" maxlength="63" placeholder="e.g. ntp.example.com"></label>
      <button class="mutable authenticatedOnly hidden" id="syncTimeButton">Sync now</button>
    </details>
    <button class="mutable authenticatedOnly hidden" id="saveConfigButton">Save settings</button>
    <button class="mutable authenticatedOnly hidden" id="resetCalibrationButton">Reset learned stop offset (1.5 g)</button>
    <small>Required: rinse gesture &lt; CN9 limit ≤ 60 s; retare window + brew start confirmation ≤ CN9 limit.</small>
  </fieldset>

  <fieldset id="networkPanel" class="authenticatedOnly hidden"><legend>Wi-Fi connection</legend>
    <div id="networkStatus">—</div>
    <button class="mutable" id="scanNetworkButton">Scan networks</button>
    <span id="scanStatus"></span>
    <label>Detected network <select id="staNetwork"><option value="">Scan and select…</option></select></label>
    <label>Selected SSID or hidden network <input id="staSsid" type="text" maxlength="32"></label>
    <label>Password <input id="staPassword" type="password" maxlength="63" autocomplete="new-password"></label>
    <label><input id="staOpen" type="checkbox"> Open network<small class="fieldHint">Connect without a password; leave password empty when enabled.</small></label>
    <button class="mutable" id="saveNetworkButton">Save and restart</button>
    <button class="mutable" id="forgetNetworkButton">Forget network and restart</button>
    <small>Scanning runs only while Ready and is cancelled when a cycle starts. The SSID field supports hidden networks.</small>
  </fieldset>

  <fieldset id="accessPointPanel" class="authenticatedOnly hidden"><legend>Web UI password</legend>
    <div id="apStatus">MicraShotStopperAP — 192.168.4.1</div>
    <label>Current password <input id="currentApPassword" type="password" maxlength="63"></label>
    <label>New password <input id="newApPassword" type="password" minlength="8" maxlength="63"></label>
    <label>Confirm new password <input id="confirmApPassword" type="password" minlength="8" maxlength="63"></label>
    <button class="mutable" id="changeApPasswordButton">Change AP/UI password</button>
  </fieldset>

  <fieldset class="authenticatedOnly hidden"><legend>Factory reset</legend>
    <p>Erase every saved setting and restore the stopper to its factory defaults.</p>
    <button class="mutable btnWarn" id="factoryResetButton">Restore all factory settings</button>
    <small>This erases Wi-Fi, settings, calibration, shot history, and the AP/UI password, then restarts.</small>
  </fieldset>

  <fieldset id="shotHistoryPanel"><legend>Shot history</legend>
    <div id="shotTableWrap"><table id="shotTable"><thead><tr><th>Time</th><th>Dur</th><th>Goal</th><th>Actual</th><th>Err%</th><th class="colFlow">Flow</th><th>1st drop</th><th>Shot</th><th class="colCut">Cut</th><th></th></tr></thead><tbody id="shotRows"><tr><td colspan="10">Loading…</td></tr></tbody></table></div>
    <button id="exportShotsButton">Export CSV</button>
    <button class="authenticatedOnly hidden" id="clearShotsButton">Clear history</button>
  </fieldset>

  <fieldset id="logPanel"><legend>Log</legend>
    <label>Filter <select id="logFilter"><option value="">All</option><option value="SCALE">Scale</option><option value="STATE">State</option><option value="RELAY">Relay</option><option value="PADDLE">Paddle</option><option value="NETWORK">Network</option><option value="CONFIG">Config</option><option value="WEB">Web</option><option value="SECURITY">Security</option></select></label>
    <textarea id="log" readonly></textarea>
    <button id="copyLogButton">Copy</button><button id="clearLogButton">Clear view</button>
  </fieldset>
  <footer class="pageFooter"><small id="firmwareFooter">Firmware — · Boot —</small></footer>
</main>
<script>
'use strict';
let token=sessionStorage.getItem('shotStopperToken')||'',csrf=sessionStorage.getItem('shotStopperCsrf')||'',lastLog=0,configRevision=0,configLoaded=false,configDirty=false,statusBusy=false,logBusy=false,shotsBusy=false,heartbeatBusy=false,scanBusy=false,scanTimer=0,controlsMutable=false,statusTimezoneOffsetMinutes=0,shotHistory={bootId:0,shots:[]},logEvents=[];
const $=id=>document.getElementById(id);
function pad2(n){return String(n).padStart(2,'0')}
function formatTzLabel(min){const sign=min>=0?'+':'-';const abs=Math.abs(min);return 'UTC'+sign+pad2(Math.floor(abs/60))+':'+pad2(abs%60)}
function formatWallTime(unixSec,tz){const localSec=unixSec+tz*60;const d=new Date(0);d.setUTCSeconds(localSec);return d.getUTCFullYear()+'-'+pad2(d.getUTCMonth()+1)+'-'+pad2(d.getUTCDate())+' '+pad2(d.getUTCHours())+':'+pad2(d.getUTCMinutes())+':'+pad2(d.getUTCSeconds())}
function formatWallTimeLocal(unixLocalSec){const d=new Date(0);d.setUTCSeconds(unixLocalSec);return d.getUTCFullYear()+'-'+pad2(d.getUTCMonth()+1)+'-'+pad2(d.getUTCDate())+' '+pad2(d.getUTCHours())+':'+pad2(d.getUTCMinutes())+':'+pad2(d.getUTCSeconds())}
function formatShotTime(r){if(r.hasWallTime&&r.endedAtLocalSec)return formatWallTimeLocal(r.endedAtLocalSec);return'#'+r.bootId+' · no time'}
function formatShotTimeCsv(r){if(r.hasWallTime&&r.endedAtLocalSec)return formatWallTimeLocal(r.endedAtLocalSec);return''}
function populateTimezoneOptions(){const s=$('timezoneOffsetMinutes');if(s.options.length)return;for(let m=-720;m<=840;m+=60){const o=document.createElement('option');o.value=String(m);o.textContent=formatTzLabel(m);s.appendChild(o)}}
function renderShots(){const body=$('shotRows');body.replaceChildren();const cols=10;if(!shotHistory.shots.length){const row=document.createElement('tr');row.innerHTML='<td colspan="'+cols+'">No recorded shots yet.</td>';body.appendChild(row);return}for(const r of shotHistory.shots){const row=document.createElement('tr');const err=r.errorPct===null||r.errorPct===undefined?'—':r.errorPct.toFixed(1)+'%';row.innerHTML='<td>'+formatShotTime(r)+'</td><td>'+r.durationS.toFixed(1)+'s</td><td>'+r.goalG+'g</td><td>'+(r.actualG===null?'—':r.actualG.toFixed(1)+'g')+'</td><td>'+err+'</td><td class="colFlow">'+(r.avgFlowGS===null?'—':r.avgFlowGS.toFixed(2)+' g/s')+'</td><td>'+(r.firstDropS===null?'—':r.firstDropS.toFixed(1)+'s')+'</td><td>'+r.shotType+'</td><td class="colCut">'+r.cutType+'</td>';const del=document.createElement('td');if(authenticated()){const btn=document.createElement('button');btn.textContent='Delete';btn.onclick=()=>deleteOneShot(r.id);del.appendChild(btn)}else del.textContent='—';row.appendChild(del);body.appendChild(row)}}
function updateFirmwareFooter(){const boot=shotHistory.bootId?('#'+shotHistory.bootId):'—';$('firmwareFooter').textContent='Firmware — · Boot '+boot}
async function refreshShots(){if(shotsBusy)return;shotsBusy=true;try{const d=await api('/api/v1/shots');shotHistory=d;renderShots();updateFirmwareFooter()}catch(e){message(e.message,'error')}finally{shotsBusy=false}}
function exportShotsCsv(){const rows=[['id','boot_id','local_time','has_wall_time','ended_at_unix','timezone_offset_at_commit','duration_s','goal_g','actual_g','error_g','error_pct','offset_g','avg_flow_g_s','first_drop_s','shot_type','cut_type','ended_at_ms']];for(const r of shotHistory.shots)rows.push([r.id,r.bootId,formatShotTimeCsv(r),r.hasWallTime?'1':'0',r.endedAtUnixSec||'',r.timezoneOffsetMinutesAtCommit??'',r.durationS,r.goalG,r.actualG??'',r.errorG??'',r.errorPct??'',r.offsetG,r.avgFlowGS??'',r.firstDropS??'',r.shotType,r.cutType,r.endedAtMs]);const csv=rows.map(c=>c.map(v=>{const s=String(v);return /[",\n]/.test(s)?'"'+s.replace(/"/g,'""')+'"':s}).join(',')).join('\n');const blob=new Blob([csv],{type:'text/csv'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='shot-history.csv';a.click();URL.revokeObjectURL(a.href)}
function formatCurrentTime(t,tz){if(!t||!t.utcSec||!(t.state==='SYNCED'||t.state==='STALE'))return'— (not synced)';return formatWallTime(t.utcSec,tz)}
function formatNtpStatus(t){if(!t)return'Not available';const v=t.activeServer?(' via '+t.activeServer):'';if(t.state==='SYNCED'||t.state==='STALE'){const a=t.lastSyncAgeMs>=36e5?Math.floor(t.lastSyncAgeMs/36e5)+' h':t.lastSyncAgeMs>=6e4?Math.floor(t.lastSyncAgeMs/6e4)+' min':Math.floor(t.lastSyncAgeMs/1e3)+' s';return(t.state==='STALE'?'Stale — ':'Synced — ')+a+' ago; hourly resync'+v}if(t.state==='SYNCING')return'Syncing…'+v;if(t.state==='FAILED')return'Failed; retry every 1 min'+(t.nextRetryInMs?'; next in '+Math.ceil(t.nextRetryInMs/1e3)+' s':'')+v;if(t.state==='OFF')return'Not synced — auto-sync on Wi-Fi'+v;return'Waiting for network'+v}
const RR='?|Pwr|Ext|SW|Panic|IWDT|TWDT|WDT|Sleep|Brn|SDIO|USB|JTAG|eFuse|Glitch|Lock'.split('|');
function updH(h,s){function b(n){return typeof n!=='number'||n<0?'—':n>=1048576?(n/1048576).toFixed(1)+'M':n>=1024?Math.round(n/1024)+'K':n+'b'}const t=(i,v)=>$(i).textContent=v||'—';if(!h){['hCpu','hTemp','hTPeak','hRamT','hRamU','hRamF','hUptime','hResetReason'].forEach(i=>t(i,'—'));return}if(typeof h.uptimeMs==='number'&&h.uptimeMs>=0){let x=~~(h.uptimeMs/1e3),p=[],d=~~(x/86400);x%=86400;const g=~~(x/3600);x%=3600;const m=~~(x/60);if(d)p.push(d+'d');if(d||g)p.push(g+'h');if(d||g||m)p.push(m+'m');p.push(x%60+'s');t('hUptime',p.join(' '))}else t('hUptime','—');t('hResetReason',s?RR[s.resetReasonCode]||'?':'');const w=h.hwmon;if(!w){['hCpu','hTemp','hTPeak','hRamT','hRamU','hRamF'].forEach(i=>t(i,'—'));return}t('hCpu',typeof w.cpuUsagePct==='number'?w.cpuUsagePct+'%':'');t('hTemp',w.tempValid?w.tempC.toFixed(1)+'°C':'');t('hTPeak',w.tempValid?w.tempPeakC.toFixed(1)+'°C':'');t('hRamT',b(w.ramTotalBytes));t('hRamU',b(w.ramUsedBytes));t('hRamF',b(w.ramFreeBytes))}
function message(text,kind=''){const e=$('message');e.textContent=text;e.className=kind}
async function api(path,options={}){options.headers=Object.assign(authenticated()?{'X-Session-Token':token,'X-CSRF-Token':csrf}:{},options.headers||{});if(options.body)options.headers['Content-Type']='application/json';const response=await fetch(path,options);let data={};try{data=await response.json()}catch(_){data={error:'INVALID_RESPONSE'}}if(response.status===401&&authenticated()){logoutLocal();throw new Error('Session expired')}if(!response.ok)throw new Error(data.message||data.error||('HTTP '+response.status));return data}
function authenticated(){return token!==''&&csrf!==''}
function updateAccessMode(){const signedIn=authenticated();document.querySelectorAll('.authenticatedOnly').forEach(e=>e.classList.toggle('hidden',!signedIn));$('loginPanel').classList.toggle('hidden',signedIn);$('readOnlyNotice').classList.toggle('hidden',signedIn)}
function logoutLocal(){token='';csrf='';sessionStorage.removeItem('shotStopperToken');sessionStorage.removeItem('shotStopperCsrf');configLoaded=false;clearTimeout(scanTimer);document.querySelectorAll('input[type=password]').forEach(e=>e.value='');updateAccessMode();setMutable(false);message('Read-only view. Sign in to control the device.','warn');refreshStatus()}
function body(values){return JSON.stringify(values)}function number(id){return Number($(id).value)}function sToMs(id){return Math.round(number(id)*1000)}
function setMutable(enabled){const canEdit=authenticated()&&enabled;controlsMutable=canEdit;document.querySelectorAll('.mutable,#workflowPanel input,#workflowPanel select,#networkPanel input,#accessPointPanel input').forEach(e=>e.disabled=!canEdit);$('configLock').classList.toggle('hidden',!authenticated()||canEdit);$('workflowPanel').classList.toggle('locked',!canEdit);$('networkPanel').classList.toggle('locked',!canEdit);$('accessPointPanel').classList.toggle('locked',!canEdit);$('staNetwork').disabled=!canEdit;updateNetworkPasswordState();updateConfigGroups()}
function markConfigDirty(){if(!authenticated())return;configDirty=true;$('configDirtyHint').classList.remove('hidden')}
function updateConfigGroups(){document.querySelectorAll('.retareOpt').forEach(e=>e.classList.toggle('fieldOff',!$('autoRetare').checked));document.querySelectorAll('.paddleOpt').forEach(e=>e.classList.toggle('fieldOff',!$('paddleReturnReminderBeep').checked))}
function validateConfigClient(){const rg=number('rinseGestureS'),rd=number('rinseDurationS'),wall=number('operationalWallS'),rw=number('retareWindowS'),conf=number('confirmationTimeoutS');if(!(rg<wall))return'Rinse gesture must be less than the CN9 limit.';if(rd>wall)return'Rinse duration must not exceed the CN9 limit.';if(rw+conf>wall)return'Retare window + brew start confirmation must not exceed the CN9 limit.';const minConf=($('autoRetare').checked?rw:0)+3;if(conf<minConf)return'Brew start confirmation must be at least retare window + 3 s.';if($('canTareStartTimer').checked&&!$('autoTare').checked)return'The Bookoo combined command requires automatic tare.';if($('paddleReturnReminderBeep').checked&&number('paddleReturnReminderMaxDurationMin')*60<number('paddleReturnReminderIntervalS'))return'Paddle limit must be at least the reminder interval.';return''}
function configPayload(){return{goalWeightG:number('goalWeightG'),rinseGestureMs:sToMs('rinseGestureS'),rinseDurationMs:sToMs('rinseDurationS'),operationalWallMs:sToMs('operationalWallS'),retareWindowMs:sToMs('retareWindowS'),confirmationTimeoutMs:sToMs('confirmationTimeoutS'),minimumCupWeightG:number('minimumCupWeightG'),retareStabilitySamples:number('retareStabilitySamples'),retareStabilityToleranceG:number('retareStabilityToleranceG'),retareStabilityMaxGapMs:sToMs('retareStabilityMaxGapS'),retareStabilityMinDurationMs:sToMs('retareStabilityMinDurationS'),autoTare:$('autoTare').checked,timerOnly:$('timerOnly').checked,canTareStartTimer:$('canTareStartTimer').checked,brewConfirmationBeep:$('brewConfirmationBeep').checked,paddleReturnReminderBeep:$('paddleReturnReminderBeep').checked,autoRetare:$('autoRetare').checked,paddleReturnReminderIntervalMs:sToMs('paddleReturnReminderIntervalS'),paddleReturnReminderMaxDurationMs:Math.round(number('paddleReturnReminderMaxDurationMin')*60000),timezoneOffsetMinutes:number('timezoneOffsetMinutes'),ntpServerPreset:$('ntpServerPreset').value,ntpServerCustom:$('ntpServerCustom').value.trim()}}
async function saveConfig(){const err=validateConfigClient();if(err){message(err,'error');return}try{await command('/api/v1/config',configPayload());configDirty=false;$('configDirtyHint').classList.add('hidden')}catch(_){}}
function updateStateTone(s){const el=$('state');el.classList.remove('stateReady','stateActive','stateFault');if(s.state==='READY')el.classList.add('stateReady');else if(s.state==='REQUIRES_OFF'||(s.safety&&s.safety.recoveryRequired))el.classList.add('stateFault');else el.classList.add('stateActive')}
function renderLog(){const f=$('logFilter').value;let out='';for(const e of logEvents){if(f&&e.category!==f)continue;out+='['+(e.atMs/1000).toFixed(3)+'] '+e.category+': '+e.message+(e.argument1||e.argument2?' ('+e.argument1+', '+e.argument2+')':'')+'\n'}$('log').value=out;$('log').scrollTop=$('log').scrollHeight}
function updateNetworkPasswordState(){$('staPassword').disabled=!controlsMutable||$('staOpen').checked}
function selectDetectedNetwork(){const o=$('staNetwork').selectedOptions[0];if(!o||!o.value)return;$('staSsid').value=o.value;$('staOpen').checked=o.dataset.open==='true';$('staPassword').value='';updateNetworkPasswordState()}
function showScanResults(d){const select=$('staNetwork');select.replaceChildren();const prompt=document.createElement('option');prompt.value='';prompt.textContent=d.networks.length?'Select a network…':'No networks found';select.appendChild(prompt);for(const n of d.networks){const o=document.createElement('option');o.value=n.ssid;o.dataset.open=String(n.open);o.textContent=n.ssid+' ('+n.rssi+' dBm, channel '+n.channel+(n.open?', open':', secured')+')';select.appendChild(o)}}
async function refreshWifiScan(){if(scanBusy||!token)return;scanBusy=true;try{const d=await api('/api/v1/network/scan');$('scanStatus').textContent=d.state==='READY'?'Ready: '+d.networks.length+' network(s)':d.state;if(d.state==='READY')showScanResults(d);if(d.state==='QUEUED'||d.state==='RUNNING')scanTimer=setTimeout(refreshWifiScan,500)}catch(e){message(e.message,'error')}finally{scanBusy=false}}
async function startWifiScan(){if(scanBusy)return;clearTimeout(scanTimer);$('scanStatus').textContent='Requesting…';try{await api('/api/v1/network/scan',{method:'POST',body:'{}'});scanTimer=setTimeout(refreshWifiScan,100)}catch(e){message(e.message,'error');$('scanStatus').textContent='Error'}}
function loadConfig(c){if(configLoaded&&configRevision===c.revision)return;$('goalWeightG').value=c.goalWeightG;[['rinseGestureS','rinseGestureMs'],['rinseDurationS','rinseDurationMs'],['operationalWallS','operationalWallMs'],['paddleReturnReminderIntervalS','paddleReturnReminderIntervalMs'],['retareWindowS','retareWindowMs'],['confirmationTimeoutS','confirmationTimeoutMs'],['retareStabilityMaxGapS','retareStabilityMaxGapMs'],['retareStabilityMinDurationS','retareStabilityMinDurationMs']].forEach(([ui,api])=>$(ui).value=String(c[api]/1000));$('paddleReturnReminderMaxDurationMin').value=String(c.paddleReturnReminderMaxDurationMs/60000);$('minimumCupWeightG').value=String(c.minimumCupWeightG??10);$('retareStabilitySamples').value=String(c.retareStabilitySamples??3);$('retareStabilityToleranceG').value=String(c.retareStabilityToleranceG??2);populateTimezoneOptions();$('timezoneOffsetMinutes').value=String(c.timezoneOffsetMinutes??0);$('ntpServerPreset').value=c.ntpServerPreset||'pool';$('ntpServerCustom').value=c.ntpServerCustom||'';['autoTare','timerOnly','canTareStartTimer','brewConfirmationBeep','paddleReturnReminderBeep','autoRetare'].forEach(k=>$(k).checked=!!c[k]);$('learnedOffsetG').textContent=typeof c.weightOffsetG==='number'?c.weightOffsetG.toFixed(2)+' g':'—';configRevision=c.revision;configLoaded=true;configDirty=false;$('configDirtyHint').classList.add('hidden');if(typeof c.timezoneOffsetMinutes==='number')statusTimezoneOffsetMinutes=c.timezoneOffsetMinutes;updateConfigGroups()}
function updateShot(s){const live=!!((s.cycle&&s.cycle.active)||s.relayClosed);$('shotIdle').textContent=live?'Shot in progress.':'No active shot in progress.';$('shotIdle').className=live?'ok':'';if(!live){$('shotCurrentWeight').textContent='—';$('shotGoalWeight').textContent='—';$('shotBar').style.width='0%';$('shotPct').textContent='—';$('shotElapsed').textContent='—';$('shotFirstDrop').textContent='—';$('shotRetare').textContent='—';$('shotType').textContent='—';$('shotScale').textContent='—';return}const goal=s.config.goalWeightG;const raw=s.scale.controlAccepted?s.scale.currentWeightG:s.scale.observedWeightG;const wt=typeof raw==='number'?raw:null;$('shotGoalWeight').textContent=String(goal);$('shotCurrentWeight').textContent=wt===null?'—':wt.toFixed(1);const pct=wt===null||goal<=0?0:wt/goal*100;$('shotBar').style.width=Math.min(Math.max(pct,0),150)+'%';$('shotPct').textContent=wt===null?'—':wt.toFixed(1)+' g / '+goal+' g ('+pct.toFixed(0)+'%)';const el=(s.cycle&&s.cycle.active&&s.cycle.elapsedMs)?s.cycle.elapsedMs:(s.relayClosed?s.cn9ElapsedMs:0);$('shotElapsed').textContent=(el/1000).toFixed(1)+' s';$('shotFirstDrop').textContent=(s.cycle&&s.cycle.firstDropElapsedMs)?(s.cycle.firstDropElapsedMs/1000).toFixed(1)+' s':'—';$('shotRetare').textContent=(s.cycle&&s.cycle.active)?(s.cycle.retarePerformed?'Yes':'No'):'—';const st=(s.cycle&&s.cycle.shotType)?s.cycle.shotType:'—';$('shotType').textContent=st==='auto'?'Automatic':st==='timer_only'?'Timer only':st==='manual'?'Manual':st==='rinse'?'Rinse':st;const pr=s.scale.protocol||'none';$('shotScale').textContent=!s.scale.available?'Not connected':pr==='acaia_legacy'?'Acaia (legacy)':pr==='acaia'?'Acaia':pr==='bookoo_generic'?'Bookoo/generic':pr==='felicita'?'Felicita':'Unknown'}
async function refreshStatus(){if(statusBusy)return;statusBusy=true;try{const s=await api('/api/v1/status');const canControl=authenticated()&&s.configMutable,remoteReady=!!s.remoteControlEnabled;$('state').textContent=s.stateLabel+' ('+s.state+')';updateStateTone(s);$('paddle').textContent=s.physicalPaddleOn?'CLOSED (ON)':'OPEN (OFF)';$('relay').textContent=s.relayClosed?'CLOSED (ON)':'OPEN (OFF)';$('safety').textContent=s.safety.state+' — '+s.safety.fault+' — WDT '+(s.safety.taskWatchdogReady?'ready':'FAULT')+' — external '+(s.safety.externalHardware?'present':'not configured')+(s.safety.recoveryRequired?' — local paddle recovery required':'');$('maintenance').textContent=s.maintenance.active?'Reserved, lease '+s.maintenance.leaseId:'Idle';$('health').textContent='loop gap '+s.health.loopMaxGapMs+' ms — heap '+s.health.freeHeapBytes+' B (min '+s.health.minimumFreeHeapBytes+' B) — scale gaps '+s.scale.packetGaps+' — rejected '+s.scale.rejectedPackets+' — reconnects '+s.scale.reconnects+' — last '+s.scale.lastDisconnectReasonName+' — dropped '+s.scale.eventsDropped;updH(s.health,s.safety);$('lastCommand').textContent=s.lastCommand.requestId?s.lastCommand.requestId+' — '+s.lastCommand.state:'None';$('currentTime').textContent=formatCurrentTime(s.time,statusTimezoneOffsetMinutes);$('ntpStatus').textContent=formatNtpStatus(s.time);$('source').textContent=s.controlSource;$('scale').textContent=(s.scale.available?'BLE connected':'BLE unavailable')+' — stream '+s.scale.streamState+' — control '+s.scale.controlState;updateShot(s);$('lastCycle').textContent=s.lastCycle.valid?'Last: '+(s.lastCycle.durationMs/1000).toFixed(1)+' s, '+s.lastCycle.endReason+', '+(s.lastCycle.lastWeightG===null?'no weight':s.lastCycle.lastWeightG.toFixed(1)+' g'):'No previous cycle.';$('networkStatus').textContent='STA: '+s.network.staState+(s.network.staIp?' — '+s.network.staIp:'')+(s.network.wifiConfigured?' — credentials saved':'');$('apStatus').textContent='AP: '+(s.network.apActive?'active':'inactive')+' — MicraShotStopperAP — '+s.network.apIp+' — '+s.network.apClients+' client(s)';$('virtualPaddle').checked=s.virtualPaddleOn;$('virtualPaddleState').textContent=s.virtualPaddleOn?'ON':'OFF';$('controlPolicy').textContent=remoteReady?'Remote CN9 enabled. Paddle priority; Stop opens CN9 only.':'Remote CN9 disabled by policy; monitor, config, and Stop remain.';$('virtualPaddle').disabled=!(remoteReady&&authenticated()&&(canControl||s.virtualPaddleOn));$('rinseButton').disabled=!(remoteReady&&canControl);$('stopButton').disabled=!(authenticated()&&s.relayClosed);$('restartButton').disabled=!canControl;setMutable(s.configMutable);loadConfig(s.config)}catch(e){message(e.message,'error')}finally{statusBusy=false}}
async function refreshLog(){if(logBusy)return;logBusy=true;try{const d=await api('/api/v1/log?after='+lastLog);for(const e of d.events){logEvents.push(e);lastLog=e.sequence}renderLog()}catch(e){message(e.message,'error')}finally{logBusy=false}}
async function heartbeat(){if(heartbeatBusy)return;heartbeatBusy=true;try{await api('/api/v1/heartbeat',{method:'POST',body:'{}'})}catch(e){message(e.message,'error')}finally{heartbeatBusy=false}}
async function clearShotHistory(){if(!confirm('Clear all recorded shot history? This cannot be undone.'))return;try{await api('/api/v1/shots/clear',{method:'POST',body:body({confirm:'CLEAR_SHOT_LOG'})});shotHistory.shots=[];renderShots();message('Shot history cleared.','ok')}catch(e){message(e.message,'error')}}
async function deleteOneShot(id){if(!id||!confirm('Delete this shot record?'))return;try{await api('/api/v1/shots/delete',{method:'POST',body:body({id})});shotHistory.shots=shotHistory.shots.filter(s=>s.id!==id);renderShots();message('Shot deleted.','ok')}catch(e){message(e.message,'error')}}
async function command(path,value={}){try{await api(path,{method:'POST',body:body(value)});message('Request accepted.','ok');setTimeout(refreshStatus,150)}catch(e){message(e.message,'error');refreshStatus()}}
async function doLogin(){try{const d=await api('/api/v1/login',{method:'POST',headers:{},body:body({password:$('loginPassword').value})});sessionStorage.setItem('shotStopperToken',d.token);sessionStorage.setItem('shotStopperCsrf',d.csrf);window.location.reload()}catch(e){message(e.message,'error')}}
$('loginButton').onclick=doLogin;
$('loginPassword').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();doLogin()}};
$('logoutButton').onclick=async()=>{try{await api('/api/v1/logout',{method:'POST',body:'{}'})}catch(_){}logoutLocal()};$('virtualPaddle').onchange=()=>command('/api/v1/control/paddle',{on:$('virtualPaddle').checked});$('rinseButton').onclick=()=>command('/api/v1/control/rinse');$('stopButton').onclick=()=>command('/api/v1/control/stop');$('restartButton').onclick=()=>{if(confirm('Restart the controller?'))command('/api/v1/control/restart')};$('scanNetworkButton').onclick=startWifiScan;$('staNetwork').onchange=selectDetectedNetwork;$('staOpen').onchange=updateNetworkPasswordState;$('saveConfigButton').onclick=saveConfig;$('saveConfigTop').onclick=saveConfig;$('autoRetare').onchange=()=>{updateConfigGroups();markConfigDirty()};$('paddleReturnReminderBeep').onchange=()=>{updateConfigGroups();markConfigDirty()};document.querySelectorAll('#workflowPanel input,#workflowPanel select').forEach(el=>{el.addEventListener('input',markConfigDirty);el.addEventListener('change',markConfigDirty)});$('logFilter').onchange=renderLog;$('syncTimeButton').onclick=()=>command('/api/v1/time/sync');$('resetCalibrationButton').onclick=()=>{if(confirm('Reset the learned stop offset to the default 1.5 g? This cannot be undone.'))command('/api/v1/calibration/reset')};$('saveNetworkButton').onclick=()=>command('/api/v1/network',{action:'save',ssid:$('staSsid').value,password:$('staPassword').value,open:$('staOpen').checked}).finally(()=>{$('staPassword').value='' });$('forgetNetworkButton').onclick=()=>{if(confirm('Forget the STA network and restart?'))command('/api/v1/network',{action:'forget'})};$('changeApPasswordButton').onclick=()=>{const n=$('newApPassword').value;if(n!==$('confirmApPassword').value){message('The new passwords do not match.','error');return}command('/api/v1/access-point/password',{currentPassword:$('currentApPassword').value,newPassword:n}).finally(()=>{['currentApPassword','newApPassword','confirmApPassword'].forEach(id=>$(id).value='')})};$('factoryResetButton').onclick=()=>{if(confirm('Restore all factory settings? This erases Wi-Fi, config, calibration, history, and the AP/UI password, then restarts. This cannot be undone.'))command('/api/v1/factory-reset',{confirm:'ERASE_ALL_SETTINGS'})};$('copyLogButton').onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($('log').value);$('clearLogButton').onclick=()=>{logEvents=[];$('log').value=''};$('exportShotsButton').onclick=exportShotsCsv;$('clearShotsButton').onclick=clearShotHistory;updateAccessMode();setMutable(false);populateTimezoneOptions();updateConfigGroups();refreshStatus();refreshShots();refreshLog();if(authenticated())message('Session active.','ok');setInterval(()=>refreshStatus(),1000);setInterval(()=>refreshLog(),3000);setInterval(()=>refreshShots(),15000);setInterval(()=>{if(authenticated())heartbeat()},10000);
</script>
</body></html>)HTML";

}  // namespace shotstopper
