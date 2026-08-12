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
body{font-family:system-ui,sans-serif;max-width:760px;margin:1rem auto;padding:0 1rem;color:#111}fieldset{margin:1rem 0;padding:1rem}label{display:block;margin:.55rem 0}input,select,button{font:inherit;padding:.4rem}input[type=number],input[type=text],input[type=password],select{width:18rem;max-width:90%}button{margin:.25rem}.row{display:flex;gap:1rem;flex-wrap:wrap}.statusColumn{display:grid;grid-template-columns:minmax(0,1fr);gap:.75rem}.metric{min-width:0;overflow-wrap:anywhere}.error{color:#a00}.ok{color:#075}.warn{color:#8a4b00}.hidden{display:none}#log{box-sizing:border-box;width:100%;height:15rem;font-family:ui-monospace,monospace;white-space:pre;overflow:auto}small{display:block;color:#555}.fieldHint{display:block;font-size:.82em;color:#666;margin:.1rem 0 .2rem;line-height:1.35;max-width:34rem;font-weight:400}.locked{opacity:.55}dt{font-weight:600}dd{margin:0 0 .5rem}.switchRow{display:flex;align-items:center;gap:.7rem;margin:.55rem 0}.switch{position:relative;display:inline-block;width:5.7rem;height:2.15rem}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;inset:0;display:flex;align-items:center;justify-content:space-between;padding:0 .42rem;border-radius:2rem;background:#9b1c1c;color:#fff;font-weight:700;font-size:.72rem;cursor:pointer;transition:.2s}.slider:before{position:absolute;content:"";height:1.65rem;width:1.65rem;left:.25rem;border-radius:50%;background:#fff;transition:.2s}.switch input:checked+.slider{background:#087f23}.switch input:checked+.slider:before{transform:translateX(3.28rem)}.switch input:focus-visible+.slider{outline:3px solid #2563eb;outline-offset:2px}.switch input:disabled+.slider{cursor:not-allowed;opacity:.48}.switchOn{opacity:.45}.switch input:checked+.slider .switchOn{opacity:1}.switch input:checked+.slider .switchOff{opacity:.45}.switchState{min-width:2.3rem;font-weight:700}
#shotTableWrap{overflow:auto;max-height:16rem;margin:.5rem 0}#shotTable{width:100%;border-collapse:collapse;font-size:.85rem}#shotTable th,#shotTable td{border-bottom:1px solid #ddd;padding:.35rem .4rem;text-align:left;white-space:nowrap}#shotTable th{position:sticky;top:0;background:#fff}
</style>
</head>
<body>
<h1>Micra Shot Stopper</h1>
<p id="message">Read-only view. Sign in to control the device.</p>
<section id="loginPanel">
  <label>Administrator password <input id="loginPassword" type="password" maxlength="63" autocomplete="current-password"></label>
  <button id="loginButton">Sign in</button>
</section>
<main id="app">
  <fieldset><legend>Status</legend>
    <div class="statusColumn">
      <div class="metric"><strong>State</strong><div id="state">—</div></div>
      <div class="metric"><strong>Paddle State</strong><div id="paddle">—</div></div>
      <div class="metric"><strong>CN9</strong><div id="relay">—</div></div>
      <div class="metric"><strong>CN9 Safety</strong><div id="safety">—</div></div>
      <div class="metric"><strong>Maintenance</strong><div id="maintenance">—</div></div>
      <div class="metric"><strong>Health</strong><div id="health">—</div></div>
      <div class="metric"><strong>Last command</strong><div id="lastCommand">—</div></div>
      <div class="metric"><strong>Time</strong><div id="timer">—</div></div>
      <div class="metric"><strong>Current time</strong><div id="currentTime">—</div></div>
      <div class="metric"><strong>NTP</strong><div id="ntpStatus">—</div></div>
      <div class="metric"><strong>Source</strong><div id="source">—</div></div>
      <div class="metric"><strong>Scale</strong><div id="scale">—</div></div>
      <div class="metric"><strong>Weight</strong><div id="weight">—</div></div>
      <div class="metric"><strong>Cycle</strong><div id="cycleDebug">—</div></div>
    </div>
    <small id="lastCycle">No previous cycle.</small>
  </fieldset>

  <fieldset class="authenticatedOnly hidden"><legend>Actions</legend>
    <div class="switchRow"><span>Virtual paddle</span><label class="switch" for="virtualPaddle"><input id="virtualPaddle" type="checkbox" role="switch" aria-label="Virtual paddle"><span class="slider"><span class="switchOff">OFF</span><span class="switchOn">ON</span></span></label><span id="virtualPaddleState" class="switchState">OFF</span></div>
    <button id="rinseButton">Start rinse</button>
    <button id="stopButton">Stop shot</button>
    <button id="restartButton">Restart controller</button>
    <small id="controlPolicy">The physical paddle always has priority. Stop only opens CN9.</small>
  </fieldset>

  <fieldset id="workflowPanel"><legend>Workflow</legend>
    <p id="readOnlyNotice">Sign in to edit workflow settings.</p>
    <p id="configLock" class="warn hidden">Configuration is locked while a cycle is active.</p>
    <div class="row">
      <label>Target (g)<input id="goalWeightG" type="number" min="10" max="200" required><small class="fieldHint">Stop weight for automatic brew by weight; CN9 opens near this target minus the learned offset.</small></label>
      <label>Rinse gesture (s)<input id="rinseGestureS" type="number" min="0.1" max="5" step="0.1" required><small class="fieldHint">Maximum paddle ON time that still counts as a rinse when you release the paddle.</small></label>
      <label>Rinse duration (s)<input id="rinseDurationS" type="number" min="0.5" max="10" step="0.1" required><small class="fieldHint">How long CN9 stays closed after a rinse gesture starts.</small></label>
      <label>CN9 limit (s)<input id="operationalWallS" type="number" min="5" max="60" step="1" required><small class="fieldHint">Maximum time CN9 may remain closed in any cycle; firmware also enforces a 60 s hard safety cap.</small></label>
    </div>
    <label><input id="autoTare" type="checkbox"> Automatic tare<small class="fieldHint">Send an initial tare to the scale when an automatic shot starts.</small></label>
    <label><input id="timerOnly" type="checkbox"> Timer only; do not stop by weight<small class="fieldHint">Keep tare and timer but disable weight stop and post-shot offset learning.</small></label>
    <label><input id="canTareStartTimer" type="checkbox"> Bookoo combined command<small class="fieldHint">Use the scale combined tare+start-timer command; requires automatic tare.</small></label>
    <label><input id="brewConfirmationBeep" type="checkbox"> Beep when coffee starts<small class="fieldHint">Beep once when the first coffee drop is detected on the scale, including during the retare window if extraction starts early. No beep on confirmation timeout alone.</small></label>
    <label><input id="autoRetare" type="checkbox"> Enable automatic retare<small class="fieldHint">If a cup is placed after shot start, allow one second tare during the retare window.</small></label>
    <label>Retare window (s)<input id="retareWindowS" type="number" min="0.5" max="10" step="0.1" required><small class="fieldHint">Time after shot start to detect a late cup and retare; ignored when automatic retare is off.</small></label>
    <label>Minimum cup weight (g)<input id="minimumCupWeightG" type="number" min="1" max="500" step="0.1" required><small class="fieldHint">Minimum stable load treated as a cup for retare only; lighter loads are ignored for retare detection.</small></label>
    <label>Retare stable samples<input id="retareStabilitySamples" type="number" min="2" max="10" step="1" required><small class="fieldHint">Consecutive stable readings required before retare; higher values reduce false retares.</small></label>
    <label>Retare stability tolerance (g)<input id="retareStabilityToleranceG" type="number" min="0.1" max="20" step="0.1" required><small class="fieldHint">Maximum weight change allowed between stable readings; lower values require a steadier cup.</small></label>
    <label>Retare sample gap (s)<input id="retareStabilityMaxGapS" type="number" min="0.1" max="5" step="0.1" required><small class="fieldHint">Maximum time between stable readings; longer gaps restart the stability counter.</small></label>
    <label>Retare min stable time (s)<input id="retareStabilityMinDurationS" type="number" min="0" max="2" step="0.1" required><small class="fieldHint">Minimum time a stable load must persist before retare; blocks retare on very fast reading bursts. Use 0 to disable.</small></label>
    <label>Brew start confirmation (s)<input id="confirmationTimeoutS" type="number" min="0.5" max="30" step="0.1" required><small class="fieldHint">Minimum: retare window + 3 s. Confirms extraction started (first drops or timeout). Weight stop stays inhibited until this window ends and retare no longer blocks.</small></label>
    <label><input id="paddleReturnReminderBeep" type="checkbox"> Scale reminder beep until the physical paddle is switched OFF<small class="fieldHint">Repeat a scale beep while the physical paddle stays ON after CN9 has opened.</small></label>
    <label>Paddle reminder interval (s)<input id="paddleReturnReminderIntervalS" type="number" min="5" max="60" step="1" required><small class="fieldHint">Time between paddle-return reminder beeps.</small></label>
    <label>Paddle reminder limit (s)<input id="paddleReturnReminderMaxDurationS" type="number" min="60" max="3600" step="1" required><small class="fieldHint">Stop reminder beeps after this duration even if the paddle remains ON.</small></label>
    <label>Timezone <select id="timezoneOffsetMinutes"></select></label>
    <label>NTP server <select id="ntpServerPreset"><option value="pool">pool.ntp.org</option><option value="google">time.google.com</option><option value="cloudflare">time.cloudflare.com</option><option value="nist">time.nist.gov</option></select></label>
    <label>Custom NTP (optional) <input id="ntpServerCustom" type="text" maxlength="63" placeholder="e.g. ntp.example.com"></label>
    <button class="mutable authenticatedOnly hidden" id="syncTimeButton">Sync now</button>
    <button class="mutable authenticatedOnly hidden" id="saveConfigButton">Save settings</button>
    <button class="mutable authenticatedOnly hidden" id="resetCalibrationButton">Reset learned stop offset (1.5 g)</button>
    <small>Required: rinse gesture &lt; CN9 limit ≤ 60 s; retare window + brew start confirmation ≤ CN9 limit.</small>
  </fieldset>

  <fieldset id="networkPanel" class="authenticatedOnly hidden"><legend>Wi-Fi STA network</legend>
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

  <fieldset id="accessPointPanel" class="authenticatedOnly hidden"><legend>Access point</legend>
    <div id="apStatus">MicraShotStopperAP — 192.168.4.1</div>
    <label>Current password <input id="currentApPassword" type="password" maxlength="63"></label>
    <label>New password <input id="newApPassword" type="password" minlength="8" maxlength="63"></label>
    <label>Confirm new password <input id="confirmApPassword" type="password" minlength="8" maxlength="63"></label>
    <button class="mutable" id="changeApPasswordButton">Change AP/UI password</button>
  </fieldset>

  <fieldset class="authenticatedOnly hidden"><legend>Factory reset</legend>
    <p>Erase every saved setting and restore the stopper to its factory defaults.</p>
    <button class="mutable" id="factoryResetButton">Restore all factory settings</button>
    <small>This removes the STA network, workflow changes, learned calibration, shot history, and the custom AP/UI password, then restarts the controller.</small>
  </fieldset>

  <fieldset><legend>Shot history</legend>
    <div id="shotTableWrap"><table id="shotTable"><thead><tr><th>Time</th><th>Dur</th><th>Goal</th><th>Actual</th><th>Err%</th><th>Flow</th><th>1st drop</th><th>Shot</th><th>Cut</th><th></th></tr></thead><tbody id="shotRows"><tr><td colspan="10">Loading…</td></tr></tbody></table></div>
    <button id="exportShotsButton">Export CSV</button>
    <button class="authenticatedOnly hidden" id="clearShotsButton">Clear history</button>
    <small>Up to 120 shots. Time is fixed when recorded (timezone at that moment). Without NTP sync, time shows as unavailable. <strong>1st drop</strong> is seconds from shot start to the first detected coffee weight increase (same instant as the brew-start beep when enabled).</small>
  </fieldset>

  <fieldset><legend>Log</legend>
    <textarea id="log" readonly></textarea>
    <button id="copyLogButton">Copy</button><button id="clearLogButton">Clear view</button>
  </fieldset>
  <button id="logoutButton" class="authenticatedOnly hidden">Sign out</button>
</main>
<script>
'use strict';
let token=sessionStorage.getItem('shotStopperToken')||'',csrf=sessionStorage.getItem('shotStopperCsrf')||'',lastLog=0,configRevision=0,configLoaded=false,statusBusy=false,logBusy=false,shotsBusy=false,heartbeatBusy=false,scanBusy=false,scanTimer=0,controlsMutable=false,statusTimezoneOffsetMinutes=0,shotHistory={bootId:0,shots:[]};
const $=id=>document.getElementById(id);
function pad2(n){return String(n).padStart(2,'0')}
function formatTzLabel(min){const sign=min>=0?'+':'-';const abs=Math.abs(min);return 'UTC'+sign+pad2(Math.floor(abs/60))+':'+pad2(abs%60)}
function formatWallTime(unixSec,tz){const localSec=unixSec+tz*60;const d=new Date(0);d.setUTCSeconds(localSec);return d.getUTCFullYear()+'-'+pad2(d.getUTCMonth()+1)+'-'+pad2(d.getUTCDate())+' '+pad2(d.getUTCHours())+':'+pad2(d.getUTCMinutes())+':'+pad2(d.getUTCSeconds())}
function formatWallTimeLocal(unixLocalSec){const d=new Date(0);d.setUTCSeconds(unixLocalSec);return d.getUTCFullYear()+'-'+pad2(d.getUTCMonth()+1)+'-'+pad2(d.getUTCDate())+' '+pad2(d.getUTCHours())+':'+pad2(d.getUTCMinutes())+':'+pad2(d.getUTCSeconds())}
function formatShotTime(r){if(r.hasWallTime&&r.endedAtLocalSec)return formatWallTimeLocal(r.endedAtLocalSec);return'#'+r.bootId+' · no time'}
function formatShotTimeCsv(r){if(r.hasWallTime&&r.endedAtLocalSec)return formatWallTimeLocal(r.endedAtLocalSec);return''}
function populateTimezoneOptions(){const s=$('timezoneOffsetMinutes');if(s.options.length)return;for(let m=-720;m<=840;m+=60){const o=document.createElement('option');o.value=String(m);o.textContent=formatTzLabel(m);s.appendChild(o)}}
function renderShots(){const body=$('shotRows');body.replaceChildren();const cols=10;if(!shotHistory.shots.length){const row=document.createElement('tr');row.innerHTML='<td colspan="'+cols+'">No recorded shots yet.</td>';body.appendChild(row);return}for(const r of shotHistory.shots){const row=document.createElement('tr');const err=r.errorPct===null||r.errorPct===undefined?'—':r.errorPct.toFixed(1)+'%';row.innerHTML='<td>'+formatShotTime(r)+'</td><td>'+r.durationS.toFixed(1)+'s</td><td>'+r.goalG+'g</td><td>'+(r.actualG===null?'—':r.actualG.toFixed(1)+'g')+'</td><td>'+err+'</td><td>'+(r.avgFlowGS===null?'—':r.avgFlowGS.toFixed(2)+' g/s')+'</td><td>'+(r.firstDropS===null?'—':r.firstDropS.toFixed(1)+'s')+'</td><td>'+r.shotType+'</td><td>'+r.cutType+'</td>';const del=document.createElement('td');if(authenticated()){const btn=document.createElement('button');btn.textContent='Delete';btn.onclick=()=>deleteOneShot(r.id);del.appendChild(btn)}else del.textContent='—';row.appendChild(del);body.appendChild(row)}}
async function refreshShots(){if(shotsBusy)return;shotsBusy=true;try{const d=await api('/api/v1/shots');shotHistory=d;renderShots()}catch(e){message(e.message,'error')}finally{shotsBusy=false}}
function exportShotsCsv(){const rows=[['id','boot_id','local_time','has_wall_time','ended_at_unix','timezone_offset_at_commit','duration_s','goal_g','actual_g','error_g','error_pct','offset_g','avg_flow_g_s','first_drop_s','shot_type','cut_type','ended_at_ms']];for(const r of shotHistory.shots)rows.push([r.id,r.bootId,formatShotTimeCsv(r),r.hasWallTime?'1':'0',r.endedAtUnixSec||'',r.timezoneOffsetMinutesAtCommit??'',r.durationS,r.goalG,r.actualG??'',r.errorG??'',r.errorPct??'',r.offsetG,r.avgFlowGS??'',r.firstDropS??'',r.shotType,r.cutType,r.endedAtMs]);const csv=rows.map(c=>c.map(v=>{const s=String(v);return /[",\n]/.test(s)?'"'+s.replace(/"/g,'""')+'"':s}).join(',')).join('\n');const blob=new Blob([csv],{type:'text/csv'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='shot-history.csv';a.click();URL.revokeObjectURL(a.href)}
function formatCurrentTime(t,tz){if(!t||!t.utcSec||!(t.state==='SYNCED'||t.state==='STALE'))return'— (not synced)';return formatWallTime(t.utcSec,tz)}
function formatNtpStatus(t){if(!t)return'Not available';const server=t.activeServer?(' via '+t.activeServer):'';if(t.state==='SYNCED'||t.state==='STALE'){const age=t.lastSyncAgeMs>=3600000?Math.floor(t.lastSyncAgeMs/3600000)+' h':t.lastSyncAgeMs>=60000?Math.floor(t.lastSyncAgeMs/60000)+' min':Math.floor(t.lastSyncAgeMs/1000)+' s';return(t.state==='STALE'?'Stale — ':'Synced — ')+age+' ago; resync hourly'+server}if(t.state==='SYNCING')return'Syncing…'+server;if(t.state==='FAILED')return'Failed; retry every 1 min'+(t.nextRetryInMs?'; next in '+Math.ceil(t.nextRetryInMs/1000)+' s':'')+server;if(t.state==='OFF')return'Not synced — auto-sync on Wi-Fi connect'+server;return'Waiting for network'+server}
function message(text,kind=''){const e=$('message');e.textContent=text;e.className=kind}
async function api(path,options={}){options.headers=Object.assign(authenticated()?{'X-Session-Token':token,'X-CSRF-Token':csrf}:{},options.headers||{});if(options.body)options.headers['Content-Type']='application/json';const response=await fetch(path,options);let data={};try{data=await response.json()}catch(_){data={error:'INVALID_RESPONSE'}}if(response.status===401&&authenticated()){logoutLocal();throw new Error('Session expired')}if(!response.ok)throw new Error(data.message||data.error||('HTTP '+response.status));return data}
function authenticated(){return token!==''&&csrf!==''}
function updateAccessMode(){const signedIn=authenticated();document.querySelectorAll('.authenticatedOnly').forEach(e=>e.classList.toggle('hidden',!signedIn));$('loginPanel').classList.toggle('hidden',signedIn);$('readOnlyNotice').classList.toggle('hidden',signedIn)}
function logoutLocal(){token='';csrf='';sessionStorage.removeItem('shotStopperToken');sessionStorage.removeItem('shotStopperCsrf');configLoaded=false;clearTimeout(scanTimer);document.querySelectorAll('input[type=password]').forEach(e=>e.value='');updateAccessMode();setMutable(false);message('Read-only view. Sign in to control the device.','warn');refreshStatus()}
function body(values){return JSON.stringify(values)}function number(id){return Number($(id).value)}function sToMs(id){return Math.round(number(id)*1000)}
function setMutable(enabled){const canEdit=authenticated()&&enabled;controlsMutable=canEdit;document.querySelectorAll('.mutable,#workflowPanel input,#workflowPanel select,#networkPanel input,#accessPointPanel input').forEach(e=>e.disabled=!canEdit);$('configLock').classList.toggle('hidden',!authenticated()||canEdit);$('workflowPanel').classList.toggle('locked',!canEdit);$('networkPanel').classList.toggle('locked',!canEdit);$('accessPointPanel').classList.toggle('locked',!canEdit);$('staNetwork').disabled=!canEdit;updateNetworkPasswordState()}
function updateNetworkPasswordState(){$('staPassword').disabled=!controlsMutable||$('staOpen').checked}
function selectDetectedNetwork(){const o=$('staNetwork').selectedOptions[0];if(!o||!o.value)return;$('staSsid').value=o.value;$('staOpen').checked=o.dataset.open==='true';$('staPassword').value='';updateNetworkPasswordState()}
function showScanResults(d){const select=$('staNetwork');select.replaceChildren();const prompt=document.createElement('option');prompt.value='';prompt.textContent=d.networks.length?'Select a network…':'No networks found';select.appendChild(prompt);for(const n of d.networks){const o=document.createElement('option');o.value=n.ssid;o.dataset.open=String(n.open);o.textContent=n.ssid+' ('+n.rssi+' dBm, channel '+n.channel+(n.open?', open':', secured')+')';select.appendChild(o)}}
async function refreshWifiScan(){if(scanBusy||!token)return;scanBusy=true;try{const d=await api('/api/v1/network/scan');$('scanStatus').textContent=d.state==='READY'?'Ready: '+d.networks.length+' network(s)':d.state;if(d.state==='READY')showScanResults(d);if(d.state==='QUEUED'||d.state==='RUNNING')scanTimer=setTimeout(refreshWifiScan,500)}catch(e){message(e.message,'error')}finally{scanBusy=false}}
async function startWifiScan(){if(scanBusy)return;clearTimeout(scanTimer);$('scanStatus').textContent='Requesting…';try{await api('/api/v1/network/scan',{method:'POST',body:'{}'});scanTimer=setTimeout(refreshWifiScan,100)}catch(e){message(e.message,'error');$('scanStatus').textContent='Error'}}
function loadConfig(c){if(configLoaded&&configRevision===c.revision)return;$('goalWeightG').value=c.goalWeightG;[['rinseGestureS','rinseGestureMs'],['rinseDurationS','rinseDurationMs'],['operationalWallS','operationalWallMs'],['paddleReturnReminderIntervalS','paddleReturnReminderIntervalMs'],['paddleReturnReminderMaxDurationS','paddleReturnReminderMaxDurationMs'],['retareWindowS','retareWindowMs'],['confirmationTimeoutS','confirmationTimeoutMs'],['retareStabilityMaxGapS','retareStabilityMaxGapMs'],['retareStabilityMinDurationS','retareStabilityMinDurationMs']].forEach(([ui,api])=>$(ui).value=String(c[api]/1000));$('minimumCupWeightG').value=String(c.minimumCupWeightG??10);$('retareStabilitySamples').value=String(c.retareStabilitySamples??3);$('retareStabilityToleranceG').value=String(c.retareStabilityToleranceG??2);populateTimezoneOptions();$('timezoneOffsetMinutes').value=String(c.timezoneOffsetMinutes??0);$('ntpServerPreset').value=c.ntpServerPreset||'pool';$('ntpServerCustom').value=c.ntpServerCustom||'';['autoTare','timerOnly','canTareStartTimer','brewConfirmationBeep','paddleReturnReminderBeep','autoRetare'].forEach(k=>$(k).checked=!!c[k]);configRevision=c.revision;configLoaded=true;if(typeof c.timezoneOffsetMinutes==='number')statusTimezoneOffsetMinutes=c.timezoneOffsetMinutes}
async function refreshStatus(){if(statusBusy)return;statusBusy=true;try{const s=await api('/api/v1/status');const canControl=authenticated()&&s.configMutable,remoteReady=!!s.remoteControlEnabled;$('state').textContent=s.stateLabel+' ('+s.state+')';$('paddle').textContent=s.physicalPaddleOn?'CLOSED (ON)':'OPEN (OFF)';$('relay').textContent=s.relayClosed?'CLOSED (ON)':'OPEN (OFF)';$('safety').textContent=s.safety.state+' — '+s.safety.fault+' — WDT '+(s.safety.taskWatchdogReady?'ready':'FAULT')+' — external '+(s.safety.externalHardware?'present':'not configured')+(s.safety.recoveryRequired?' — local paddle recovery required':'');$('maintenance').textContent=s.maintenance.active?'Reserved, lease '+s.maintenance.leaseId:'Idle';$('health').textContent='loop gap '+s.health.loopMaxGapMs+' ms — heap '+s.health.freeHeapBytes+' B (min '+s.health.minimumFreeHeapBytes+' B) — scale gaps '+s.scale.packetGaps+' — rejected '+s.scale.rejectedPackets+' — reconnects '+s.scale.reconnects+' — last '+s.scale.lastDisconnectReasonName+' — dropped '+s.scale.eventsDropped;$('lastCommand').textContent=s.lastCommand.requestId?s.lastCommand.requestId+' — '+s.lastCommand.state:'None';$('timer').textContent=((s.relayClosed?s.cn9ElapsedMs:(s.lastCycle.valid?s.lastCycle.durationMs:0))/1000).toFixed(1)+' s';$('currentTime').textContent=formatCurrentTime(s.time,statusTimezoneOffsetMinutes);$('ntpStatus').textContent=formatNtpStatus(s.time);$('source').textContent=s.controlSource;$('scale').textContent=(s.scale.available?'BLE connected':'BLE unavailable')+' — stream '+s.scale.streamState+' — control '+s.scale.controlState;$('weight').textContent=s.scale.observedWeightG===null?'—':s.scale.observedWeightG.toFixed(1)+' g observed ('+s.scale.observedWeightAgeMs+' ms)'+(!s.scale.controlAccepted?' — rejected for control':' — control '+s.scale.currentWeightG.toFixed(1)+' g');$('cycleDebug').textContent=!s.cycle||!s.cycle.active?'Idle':('id '+s.cycle.id+' — flowDuringRetare '+(s.cycle.flowDuringRetare?'yes':'no')+(s.cycle.firstDropElapsedMs?' — 1st drop '+(s.cycle.firstDropElapsedMs/1000).toFixed(1)+' s':' — no 1st drop yet'));$('lastCycle').textContent=s.lastCycle.valid?'Last: '+(s.lastCycle.durationMs/1000).toFixed(1)+' s, '+s.lastCycle.endReason+', '+(s.lastCycle.lastWeightG===null?'no weight':s.lastCycle.lastWeightG.toFixed(1)+' g'):'No previous cycle.';$('networkStatus').textContent='STA: '+s.network.staState+(s.network.staIp?' — '+s.network.staIp:'')+(s.network.wifiConfigured?' — credentials saved':'');$('apStatus').textContent='AP: '+(s.network.apActive?'active':'inactive')+' — MicraShotStopperAP — '+s.network.apIp+' — '+s.network.apClients+' client(s)';$('virtualPaddle').checked=s.virtualPaddleOn;$('virtualPaddleState').textContent=s.virtualPaddleOn?'ON':'OFF';$('controlPolicy').textContent=remoteReady?'Remote CN9 control is enabled. The physical paddle always has priority; Stop only opens CN9.':'Remote CN9 actuation is disabled by firmware policy; monitoring, configuration, and emergency Stop remain available.';$('virtualPaddle').disabled=!(remoteReady&&authenticated()&&(canControl||s.virtualPaddleOn));$('rinseButton').disabled=!(remoteReady&&canControl);$('stopButton').disabled=!(authenticated()&&s.relayClosed);$('restartButton').disabled=!canControl;setMutable(s.configMutable);loadConfig(s.config)}catch(e){message(e.message,'error')}finally{statusBusy=false}}
async function refreshLog(){if(logBusy)return;logBusy=true;try{const d=await api('/api/v1/log?after='+lastLog);for(const e of d.events){$('log').value+='['+(e.atMs/1000).toFixed(3)+'] '+e.category+': '+e.message+(e.argument1||e.argument2?' ('+e.argument1+', '+e.argument2+')':'')+'\n';lastLog=e.sequence}$('log').scrollTop=$('log').scrollHeight}catch(e){message(e.message,'error')}finally{logBusy=false}}
async function heartbeat(){if(heartbeatBusy)return;heartbeatBusy=true;try{await api('/api/v1/heartbeat',{method:'POST',body:'{}'})}catch(e){message(e.message,'error')}finally{heartbeatBusy=false}}
async function clearShotHistory(){if(!confirm('Clear all recorded shot history? This cannot be undone.'))return;try{await api('/api/v1/shots/clear',{method:'POST',body:body({confirm:'CLEAR_SHOT_LOG'})});shotHistory.shots=[];renderShots();message('Shot history cleared.','ok')}catch(e){message(e.message,'error')}}
async function deleteOneShot(id){if(!id||!confirm('Delete this shot record?'))return;try{await api('/api/v1/shots/delete',{method:'POST',body:body({id})});shotHistory.shots=shotHistory.shots.filter(s=>s.id!==id);renderShots();message('Shot deleted.','ok')}catch(e){message(e.message,'error')}}
async function command(path,value={}){try{await api(path,{method:'POST',body:body(value)});message('Request accepted.','ok');setTimeout(refreshStatus,150)}catch(e){message(e.message,'error');refreshStatus()}}
$('loginButton').onclick=async()=>{try{const d=await api('/api/v1/login',{method:'POST',headers:{},body:body({password:$('loginPassword').value})});sessionStorage.setItem('shotStopperToken',d.token);sessionStorage.setItem('shotStopperCsrf',d.csrf);window.location.reload()}catch(e){message(e.message,'error')}};
$('logoutButton').onclick=async()=>{try{await api('/api/v1/logout',{method:'POST',body:'{}'})}catch(_){}logoutLocal()};$('virtualPaddle').onchange=()=>command('/api/v1/control/paddle',{on:$('virtualPaddle').checked});$('rinseButton').onclick=()=>command('/api/v1/control/rinse');$('stopButton').onclick=()=>command('/api/v1/control/stop');$('restartButton').onclick=()=>{if(confirm('Restart the controller?'))command('/api/v1/control/restart')};$('scanNetworkButton').onclick=startWifiScan;$('staNetwork').onchange=selectDetectedNetwork;$('staOpen').onchange=updateNetworkPasswordState;$('saveConfigButton').onclick=()=>command('/api/v1/config',{goalWeightG:number('goalWeightG'),rinseGestureMs:sToMs('rinseGestureS'),rinseDurationMs:sToMs('rinseDurationS'),operationalWallMs:sToMs('operationalWallS'),retareWindowMs:sToMs('retareWindowS'),confirmationTimeoutMs:sToMs('confirmationTimeoutS'),minimumCupWeightG:number('minimumCupWeightG'),retareStabilitySamples:number('retareStabilitySamples'),retareStabilityToleranceG:number('retareStabilityToleranceG'),retareStabilityMaxGapMs:sToMs('retareStabilityMaxGapS'),retareStabilityMinDurationMs:sToMs('retareStabilityMinDurationS'),autoTare:$('autoTare').checked,timerOnly:$('timerOnly').checked,canTareStartTimer:$('canTareStartTimer').checked,brewConfirmationBeep:$('brewConfirmationBeep').checked,paddleReturnReminderBeep:$('paddleReturnReminderBeep').checked,autoRetare:$('autoRetare').checked,paddleReturnReminderIntervalMs:sToMs('paddleReturnReminderIntervalS'),paddleReturnReminderMaxDurationMs:sToMs('paddleReturnReminderMaxDurationS'),timezoneOffsetMinutes:number('timezoneOffsetMinutes'),ntpServerPreset:$('ntpServerPreset').value,ntpServerCustom:$('ntpServerCustom').value.trim()});$('syncTimeButton').onclick=()=>command('/api/v1/time/sync');$('resetCalibrationButton').onclick=()=>{if(confirm('Reset the learned stop offset to the default 1.5 g? This cannot be undone.'))command('/api/v1/calibration/reset')};$('saveNetworkButton').onclick=()=>command('/api/v1/network',{action:'save',ssid:$('staSsid').value,password:$('staPassword').value,open:$('staOpen').checked}).finally(()=>{$('staPassword').value='' });$('forgetNetworkButton').onclick=()=>{if(confirm('Forget the STA network and restart?'))command('/api/v1/network',{action:'forget'})};$('changeApPasswordButton').onclick=()=>{const n=$('newApPassword').value;if(n!==$('confirmApPassword').value){message('The new passwords do not match.','error');return}command('/api/v1/access-point/password',{currentPassword:$('currentApPassword').value,newPassword:n}).finally(()=>{['currentApPassword','newApPassword','confirmApPassword'].forEach(id=>$(id).value='')})};$('factoryResetButton').onclick=()=>{if(confirm('Restore every stopper setting to factory defaults? This erases Wi-Fi credentials, workflow changes, learned calibration, and the custom AP/UI password, then restarts the controller. This cannot be undone.'))command('/api/v1/factory-reset',{confirm:'ERASE_ALL_SETTINGS'})};$('copyLogButton').onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($('log').value);$('clearLogButton').onclick=()=>{$('log').value=''};$('exportShotsButton').onclick=exportShotsCsv;$('clearShotsButton').onclick=clearShotHistory;updateAccessMode();setMutable(false);populateTimezoneOptions();refreshStatus();refreshShots();refreshLog();if(authenticated())message('Session active.','ok');setInterval(()=>refreshStatus(),1000);setInterval(()=>refreshLog(),3000);setInterval(()=>refreshShots(),15000);setInterval(()=>{if(authenticated())heartbeat()},10000);
</script>
</body></html>)HTML";

}  // namespace shotstopper
