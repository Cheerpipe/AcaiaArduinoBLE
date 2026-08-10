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
body{font-family:system-ui,sans-serif;max-width:760px;margin:1rem auto;padding:0 1rem;color:#111}fieldset{margin:1rem 0;padding:1rem}label{display:block;margin:.55rem 0}input,select,button{font:inherit;padding:.4rem}input[type=number],input[type=text],input[type=password],select{width:18rem;max-width:90%}button{margin:.25rem}.row{display:flex;gap:1rem;flex-wrap:wrap}.statusColumn{display:grid;grid-template-columns:minmax(0,1fr);gap:.75rem}.metric{min-width:0;overflow-wrap:anywhere}.error{color:#a00}.ok{color:#075}.warn{color:#8a4b00}.hidden{display:none}#log{box-sizing:border-box;width:100%;height:15rem;font-family:ui-monospace,monospace;white-space:pre;overflow:auto}small{display:block;color:#555}.locked{opacity:.55}dt{font-weight:600}dd{margin:0 0 .5rem}.switchRow{display:flex;align-items:center;gap:.7rem;margin:.55rem 0}.switch{position:relative;display:inline-block;width:5.7rem;height:2.15rem}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;inset:0;display:flex;align-items:center;justify-content:space-between;padding:0 .42rem;border-radius:2rem;background:#9b1c1c;color:#fff;font-weight:700;font-size:.72rem;cursor:pointer;transition:.2s}.slider:before{position:absolute;content:"";height:1.65rem;width:1.65rem;left:.25rem;border-radius:50%;background:#fff;transition:.2s}.switch input:checked+.slider{background:#087f23}.switch input:checked+.slider:before{transform:translateX(3.28rem)}.switch input:focus-visible+.slider{outline:3px solid #2563eb;outline-offset:2px}.switch input:disabled+.slider{cursor:not-allowed;opacity:.48}.switchOn{opacity:.45}.switch input:checked+.slider .switchOn{opacity:1}.switch input:checked+.slider .switchOff{opacity:.45}.switchState{min-width:2.3rem;font-weight:700}
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
      <div class="metric"><strong>Source</strong><div id="source">—</div></div>
      <div class="metric"><strong>Scale</strong><div id="scale">—</div></div>
      <div class="metric"><strong>Weight</strong><div id="weight">—</div></div>
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
      <label>Target (g)<input id="goalWeightG" type="number" min="10" max="200" required></label>
      <label>Rinse gesture (ms)<input id="rinseGestureMs" type="number" min="100" max="5000" required></label>
      <label>Rinse duration (ms)<input id="rinseDurationMs" type="number" min="500" max="10000" required></label>
      <label>Brew confirmation (ms)<input id="brewConfirmMs" type="number" min="500" max="10000" required></label>
      <label>Minimum auto-stop (ms)<input id="minAutoStopMs" type="number" min="1000" max="30000" required></label>
      <label>CN9 limit (ms)<input id="operationalWallMs" type="number" min="5000" max="60000" required></label>
    </div>
    <label><input id="autoTare" type="checkbox"> Automatic tare</label>
    <label><input id="timerOnly" type="checkbox"> Timer only; do not stop by weight</label>
    <label><input id="canTareStartTimer" type="checkbox"> Bookoo combined command</label>
    <label><input id="brewConfirmationBeep" type="checkbox"> Beep when brew is confirmed</label>
    <label><input id="paddleReturnReminderBeep" type="checkbox"> Scale reminder beep until the physical paddle is switched OFF</label>
    <button class="mutable authenticatedOnly hidden" id="saveConfigButton">Save workflow</button>
    <button class="mutable authenticatedOnly hidden" id="resetCalibrationButton">Reset learned stop offset (1.5 g)</button>
    <small>Required: rinse gesture &lt; brew confirmation &lt; minimum auto-stop &lt; CN9 limit ≤ 60,000 ms.</small>
  </fieldset>

  <fieldset id="networkPanel" class="authenticatedOnly hidden"><legend>Wi-Fi STA network</legend>
    <div id="networkStatus">—</div>
    <button class="mutable" id="scanNetworkButton">Scan networks</button>
    <span id="scanStatus"></span>
    <label>Detected network <select id="staNetwork"><option value="">Scan and select…</option></select></label>
    <label>Selected SSID or hidden network <input id="staSsid" type="text" maxlength="32"></label>
    <label>Password <input id="staPassword" type="password" maxlength="63" autocomplete="new-password"></label>
    <label><input id="staOpen" type="checkbox"> Open network</label>
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
    <small>This removes the STA network, workflow changes, learned calibration, and the custom AP/UI password, then restarts the controller.</small>
  </fieldset>

  <fieldset><legend>Log</legend>
    <textarea id="log" readonly></textarea>
    <button id="copyLogButton">Copy</button><button id="clearLogButton">Clear view</button>
  </fieldset>
  <button id="logoutButton" class="authenticatedOnly hidden">Sign out</button>
</main>
<script>
'use strict';
let token=sessionStorage.getItem('shotStopperToken')||'',csrf=sessionStorage.getItem('shotStopperCsrf')||'',lastLog=0,configRevision=0,configLoaded=false,statusBusy=false,logBusy=false,heartbeatBusy=false,scanBusy=false,scanTimer=0,controlsMutable=false;
const $=id=>document.getElementById(id);
function message(text,kind=''){const e=$('message');e.textContent=text;e.className=kind}
async function api(path,options={}){options.headers=Object.assign(authenticated()?{'X-Session-Token':token,'X-CSRF-Token':csrf}:{},options.headers||{});if(options.body)options.headers['Content-Type']='application/json';const response=await fetch(path,options);let data={};try{data=await response.json()}catch(_){data={error:'INVALID_RESPONSE'}}if(response.status===401&&authenticated()){logoutLocal();throw new Error('Session expired')}if(!response.ok)throw new Error(data.message||data.error||('HTTP '+response.status));return data}
function authenticated(){return token!==''&&csrf!==''}
function updateAccessMode(){const signedIn=authenticated();document.querySelectorAll('.authenticatedOnly').forEach(e=>e.classList.toggle('hidden',!signedIn));$('loginPanel').classList.toggle('hidden',signedIn);$('readOnlyNotice').classList.toggle('hidden',signedIn)}
function logoutLocal(){token='';csrf='';sessionStorage.removeItem('shotStopperToken');sessionStorage.removeItem('shotStopperCsrf');configLoaded=false;clearTimeout(scanTimer);document.querySelectorAll('input[type=password]').forEach(e=>e.value='');updateAccessMode();setMutable(false);message('Read-only view. Sign in to control the device.','warn');refreshStatus()}
function body(values){return JSON.stringify(values)}function number(id){return Number($(id).value)}
function setMutable(enabled){const canEdit=authenticated()&&enabled;controlsMutable=canEdit;document.querySelectorAll('.mutable,#workflowPanel input,#networkPanel input,#accessPointPanel input').forEach(e=>e.disabled=!canEdit);$('configLock').classList.toggle('hidden',!authenticated()||canEdit);$('workflowPanel').classList.toggle('locked',!canEdit);$('networkPanel').classList.toggle('locked',!canEdit);$('accessPointPanel').classList.toggle('locked',!canEdit);$('staNetwork').disabled=!canEdit;updateNetworkPasswordState()}
function updateNetworkPasswordState(){$('staPassword').disabled=!controlsMutable||$('staOpen').checked}
function selectDetectedNetwork(){const o=$('staNetwork').selectedOptions[0];if(!o||!o.value)return;$('staSsid').value=o.value;$('staOpen').checked=o.dataset.open==='true';$('staPassword').value='';updateNetworkPasswordState()}
function showScanResults(d){const select=$('staNetwork');select.replaceChildren();const prompt=document.createElement('option');prompt.value='';prompt.textContent=d.networks.length?'Select a network…':'No networks found';select.appendChild(prompt);for(const n of d.networks){const o=document.createElement('option');o.value=n.ssid;o.dataset.open=String(n.open);o.textContent=n.ssid+' ('+n.rssi+' dBm, channel '+n.channel+(n.open?', open':', secured')+')';select.appendChild(o)}}
async function refreshWifiScan(){if(scanBusy||!token)return;scanBusy=true;try{const d=await api('/api/v1/network/scan');$('scanStatus').textContent=d.state==='READY'?'Ready: '+d.networks.length+' network(s)':d.state;if(d.state==='READY')showScanResults(d);if(d.state==='QUEUED'||d.state==='RUNNING')scanTimer=setTimeout(refreshWifiScan,500)}catch(e){message(e.message,'error')}finally{scanBusy=false}}
async function startWifiScan(){if(scanBusy)return;clearTimeout(scanTimer);$('scanStatus').textContent='Requesting…';try{await api('/api/v1/network/scan',{method:'POST',body:'{}'});scanTimer=setTimeout(refreshWifiScan,100)}catch(e){message(e.message,'error');$('scanStatus').textContent='Error'}}
function loadConfig(c){if(configLoaded&&configRevision===c.revision)return;['goalWeightG','rinseGestureMs','rinseDurationMs','brewConfirmMs','minAutoStopMs','operationalWallMs'].forEach(k=>$(k).value=c[k]);['autoTare','timerOnly','canTareStartTimer','brewConfirmationBeep','paddleReturnReminderBeep'].forEach(k=>$(k).checked=!!c[k]);configRevision=c.revision;configLoaded=true}
async function refreshStatus(){if(statusBusy)return;statusBusy=true;try{const s=await api('/api/v1/status');const canControl=authenticated()&&s.configMutable,remoteReady=!!s.remoteControlEnabled;$('state').textContent=s.stateLabel+' ('+s.state+')';$('paddle').textContent=s.physicalPaddleOn?'CLOSED (ON)':'OPEN (OFF)';$('relay').textContent=s.relayClosed?'CLOSED (ON)':'OPEN (OFF)';$('safety').textContent=s.safety.state+' — '+s.safety.fault+' — WDT '+(s.safety.taskWatchdogReady?'ready':'FAULT')+' — external '+(s.safety.externalHardware?'present':'not configured')+(s.safety.recoveryRequired?' — local paddle recovery required':'');$('maintenance').textContent=s.maintenance.active?'Reserved, lease '+s.maintenance.leaseId:'Idle';$('health').textContent='loop gap '+s.health.loopMaxGapMs+' ms — heap '+s.health.freeHeapBytes+' B (min '+s.health.minimumFreeHeapBytes+' B) — scale gaps '+s.scale.packetGaps+' — rejected '+s.scale.rejectedPackets+' — reconnects '+s.scale.reconnects+' — last '+s.scale.lastDisconnectReasonName+' — dropped '+s.scale.eventsDropped;$('lastCommand').textContent=s.lastCommand.requestId?s.lastCommand.requestId+' — '+s.lastCommand.state:'None';$('timer').textContent=((s.relayClosed?s.cn9ElapsedMs:(s.lastCycle.valid?s.lastCycle.durationMs:0))/1000).toFixed(1)+' s';$('source').textContent=s.controlSource;$('scale').textContent=(s.scale.available?'BLE connected':'BLE unavailable')+' — stream '+s.scale.streamState+' — control '+s.scale.controlState;$('weight').textContent=s.scale.observedWeightG===null?'—':s.scale.observedWeightG.toFixed(1)+' g observed ('+s.scale.observedWeightAgeMs+' ms)'+(!s.scale.controlAccepted?' — rejected for control':' — control '+s.scale.currentWeightG.toFixed(1)+' g');$('lastCycle').textContent=s.lastCycle.valid?'Last: '+(s.lastCycle.durationMs/1000).toFixed(1)+' s, '+s.lastCycle.endReason+', '+(s.lastCycle.lastWeightG===null?'no weight':s.lastCycle.lastWeightG.toFixed(1)+' g'):'No previous cycle.';$('networkStatus').textContent='STA: '+s.network.staState+(s.network.staIp?' — '+s.network.staIp:'')+(s.network.wifiConfigured?' — credentials saved':'');$('apStatus').textContent='AP: '+(s.network.apActive?'active':'inactive')+' — MicraShotStopperAP — '+s.network.apIp+' — '+s.network.apClients+' client(s)';$('virtualPaddle').checked=s.virtualPaddleOn;$('virtualPaddleState').textContent=s.virtualPaddleOn?'ON':'OFF';$('controlPolicy').textContent=remoteReady?'Remote CN9 control is enabled. The physical paddle always has priority; Stop only opens CN9.':'Remote CN9 actuation is disabled by firmware policy; monitoring, configuration, and emergency Stop remain available.';$('virtualPaddle').disabled=!(remoteReady&&authenticated()&&(canControl||s.virtualPaddleOn));$('rinseButton').disabled=!(remoteReady&&canControl);$('stopButton').disabled=!(authenticated()&&s.relayClosed);$('restartButton').disabled=!canControl;setMutable(s.configMutable);loadConfig(s.config)}catch(e){message(e.message,'error')}finally{statusBusy=false}}
async function refreshLog(){if(logBusy)return;logBusy=true;try{const d=await api('/api/v1/log?after='+lastLog);for(const e of d.events){$('log').value+='['+(e.atMs/1000).toFixed(3)+'] '+e.category+': '+e.message+(e.argument1||e.argument2?' ('+e.argument1+', '+e.argument2+')':'')+'\n';lastLog=e.sequence}$('log').scrollTop=$('log').scrollHeight}catch(e){message(e.message,'error')}finally{logBusy=false}}
async function heartbeat(){if(heartbeatBusy)return;heartbeatBusy=true;try{await api('/api/v1/heartbeat',{method:'POST',body:'{}'})}catch(e){message(e.message,'error')}finally{heartbeatBusy=false}}
async function command(path,value={}){try{await api(path,{method:'POST',body:body(value)});message('Request accepted.','ok');setTimeout(refreshStatus,150)}catch(e){message(e.message,'error');refreshStatus()}}
$('loginButton').onclick=async()=>{try{const d=await api('/api/v1/login',{method:'POST',headers:{},body:body({password:$('loginPassword').value})});sessionStorage.setItem('shotStopperToken',d.token);sessionStorage.setItem('shotStopperCsrf',d.csrf);window.location.reload()}catch(e){message(e.message,'error')}};
$('logoutButton').onclick=async()=>{try{await api('/api/v1/logout',{method:'POST',body:'{}'})}catch(_){}logoutLocal()};$('virtualPaddle').onchange=()=>command('/api/v1/control/paddle',{on:$('virtualPaddle').checked});$('rinseButton').onclick=()=>command('/api/v1/control/rinse');$('stopButton').onclick=()=>command('/api/v1/control/stop');$('restartButton').onclick=()=>{if(confirm('Restart the controller?'))command('/api/v1/control/restart')};$('scanNetworkButton').onclick=startWifiScan;$('staNetwork').onchange=selectDetectedNetwork;$('staOpen').onchange=updateNetworkPasswordState;$('saveConfigButton').onclick=()=>command('/api/v1/config',{goalWeightG:number('goalWeightG'),rinseGestureMs:number('rinseGestureMs'),rinseDurationMs:number('rinseDurationMs'),brewConfirmMs:number('brewConfirmMs'),minAutoStopMs:number('minAutoStopMs'),operationalWallMs:number('operationalWallMs'),autoTare:$('autoTare').checked,timerOnly:$('timerOnly').checked,canTareStartTimer:$('canTareStartTimer').checked,brewConfirmationBeep:$('brewConfirmationBeep').checked,paddleReturnReminderBeep:$('paddleReturnReminderBeep').checked});$('resetCalibrationButton').onclick=()=>{if(confirm('Reset the learned stop offset to the default 1.5 g? This cannot be undone.'))command('/api/v1/calibration/reset')};$('saveNetworkButton').onclick=()=>command('/api/v1/network',{action:'save',ssid:$('staSsid').value,password:$('staPassword').value,open:$('staOpen').checked}).finally(()=>{$('staPassword').value='' });$('forgetNetworkButton').onclick=()=>{if(confirm('Forget the STA network and restart?'))command('/api/v1/network',{action:'forget'})};$('changeApPasswordButton').onclick=()=>{const n=$('newApPassword').value;if(n!==$('confirmApPassword').value){message('The new passwords do not match.','error');return}command('/api/v1/access-point/password',{currentPassword:$('currentApPassword').value,newPassword:n}).finally(()=>{['currentApPassword','newApPassword','confirmApPassword'].forEach(id=>$(id).value='')})};$('factoryResetButton').onclick=()=>{if(confirm('Restore every stopper setting to factory defaults? This erases Wi-Fi credentials, workflow changes, learned calibration, and the custom AP/UI password, then restarts the controller. This cannot be undone.'))command('/api/v1/factory-reset',{confirm:'ERASE_ALL_SETTINGS'})};$('copyLogButton').onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($('log').value);$('clearLogButton').onclick=()=>{$('log').value=''};updateAccessMode();setMutable(false);refreshStatus();refreshLog();if(authenticated())message('Session active.','ok');setInterval(()=>refreshStatus(),1000);setInterval(()=>refreshLog(),3000);setInterval(()=>{if(authenticated())heartbeat()},10000);
</script>
</body></html>)HTML";

}  // namespace shotstopper
