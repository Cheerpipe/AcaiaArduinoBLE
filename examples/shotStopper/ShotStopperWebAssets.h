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
body{font-family:system-ui,sans-serif;max-width:760px;margin:1rem auto;padding:0 1rem;color:#111}fieldset{margin:1rem 0;padding:1rem}label{display:block;margin:.55rem 0}input,select,button{font:inherit;padding:.4rem}input[type=number],input[type=text],input[type=password],select{width:18rem;max-width:90%}button{margin:.25rem}.row{display:flex;gap:1rem;flex-wrap:wrap}.metric{min-width:10rem}.error{color:#a00}.ok{color:#075}.warn{color:#8a4b00}.hidden{display:none}#log{box-sizing:border-box;width:100%;height:15rem;font-family:ui-monospace,monospace;white-space:pre;overflow:auto}small{display:block;color:#555}.locked{opacity:.55}dt{font-weight:600}dd{margin:0 0 .5rem}.switchRow{display:flex;align-items:center;gap:.7rem;margin:.55rem 0}.switch{position:relative;display:inline-block;width:5.7rem;height:2.15rem}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;inset:0;display:flex;align-items:center;justify-content:space-between;padding:0 .42rem;border-radius:2rem;background:#9b1c1c;color:#fff;font-weight:700;font-size:.72rem;cursor:pointer;transition:.2s}.slider:before{position:absolute;content:"";height:1.65rem;width:1.65rem;left:.25rem;border-radius:50%;background:#fff;transition:.2s}.switch input:checked+.slider{background:#087f23}.switch input:checked+.slider:before{transform:translateX(3.28rem)}.switch input:focus-visible+.slider{outline:3px solid #2563eb;outline-offset:2px}.switch input:disabled+.slider{cursor:not-allowed;opacity:.48}.switchOn{opacity:.45}.switch input:checked+.slider .switchOn{opacity:1}.switch input:checked+.slider .switchOff{opacity:.45}.switchState{min-width:2.3rem;font-weight:700}
</style>
</head>
<body>
<h1>Micra Shot Stopper</h1>
<p id="message">Sign in to view and control the device.</p>
<section id="loginPanel">
  <label>Administrator password <input id="loginPassword" type="password" maxlength="63" autocomplete="current-password"></label>
  <button id="loginButton">Sign in</button>
</section>
<main id="app" class="hidden">
  <fieldset><legend>Status</legend>
    <div class="row">
      <div class="metric"><strong>State</strong><div id="state">—</div></div>
      <div class="metric"><strong>CN9</strong><div id="relay">—</div></div>
      <div class="metric"><strong>Time</strong><div id="timer">—</div></div>
      <div class="metric"><strong>Source</strong><div id="source">—</div></div>
      <div class="metric"><strong>Scale</strong><div id="scale">—</div></div>
      <div class="metric"><strong>Weight</strong><div id="weight">—</div></div>
    </div>
    <small id="lastCycle">No previous cycle.</small>
  </fieldset>

  <fieldset><legend>Actions</legend>
    <div class="switchRow"><span>Virtual paddle</span><label class="switch" for="virtualPaddle"><input id="virtualPaddle" type="checkbox" role="switch" aria-label="Virtual paddle"><span class="slider"><span class="switchOff">OFF</span><span class="switchOn">ON</span></span></label><span id="virtualPaddleState" class="switchState">OFF</span></div>
    <button id="rinseButton">Start rinse</button>
    <button id="stopButton">Stop shot</button>
    <button id="restartButton">Restart controller</button>
    <small>The physical paddle always has priority. Stop only opens CN9.</small>
  </fieldset>

  <fieldset id="workflowPanel"><legend>Workflow</legend>
    <p id="configLock" class="warn hidden">Configuration is locked while a cycle is active.</p>
    <div class="row">
      <label>Target (g)<input id="goalWeightG" type="number" min="10" max="200" required></label>
      <label>Rinse gesture (ms)<input id="rinseGestureMs" type="number" min="100" max="5000" required></label>
      <label>Rinse duration (ms)<input id="rinseDurationMs" type="number" min="500" max="10000" required></label>
      <label>Brew confirmation (ms)<input id="brewConfirmMs" type="number" min="500" max="10000" required></label>
      <label>Minimum auto-stop (ms)<input id="minAutoStopMs" type="number" min="1000" max="30000" required></label>
      <label>CN9 limit (ms)<input id="operationalWallMs" type="number" min="5000" max="50000" required></label>
    </div>
    <label><input id="autoTare" type="checkbox"> Automatic tare</label>
    <label><input id="timerOnly" type="checkbox"> Timer only; do not stop by weight</label>
    <label><input id="canTareStartTimer" type="checkbox"> Bookoo combined command</label>
    <label><input id="brewConfirmationBeep" type="checkbox"> Beep when brew is confirmed</label>
    <button class="mutable" id="saveConfigButton">Save workflow</button>
    <button class="mutable" id="resetCalibrationButton">Reset learned stop offset (1.5 g)</button>
    <small>Required: rinse gesture &lt; brew confirmation &lt; minimum auto-stop &lt; CN9 limit ≤ 50,000 ms.</small>
  </fieldset>

  <fieldset id="networkPanel"><legend>Wi-Fi STA network</legend>
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

  <fieldset id="accessPointPanel"><legend>Access point</legend>
    <div id="apStatus">MicraShotStopperAP — 192.168.4.1</div>
    <label>Current password <input id="currentApPassword" type="password" maxlength="63"></label>
    <label>New password <input id="newApPassword" type="password" minlength="8" maxlength="63"></label>
    <label>Confirm new password <input id="confirmApPassword" type="password" minlength="8" maxlength="63"></label>
    <button class="mutable" id="changeApPasswordButton">Change AP/UI password</button>
  </fieldset>

  <fieldset><legend>Log</legend>
    <textarea id="log" readonly></textarea>
    <button id="copyLogButton">Copy</button><button id="clearLogButton">Clear view</button>
  </fieldset>
  <button id="logoutButton">Sign out</button>
</main>
<script>
'use strict';
let token='',csrf='',lastLog=0,configRevision=0,configLoaded=false,statusBusy=false,logBusy=false,heartbeatBusy=false,scanBusy=false,scanTimer=0,controlsMutable=false;
const $=id=>document.getElementById(id);
function message(text,kind=''){const e=$('message');e.textContent=text;e.className=kind}
async function api(path,options={}){options.headers=Object.assign({'X-Session-Token':token,'X-CSRF-Token':csrf},options.headers||{});if(options.body)options.headers['Content-Type']='application/json';const response=await fetch(path,options);let data={};try{data=await response.json()}catch(_){data={error:'INVALID_RESPONSE'}}if(response.status===401){logoutLocal();throw new Error('Session expired')}if(!response.ok)throw new Error(data.message||data.error||('HTTP '+response.status));return data}
function logoutLocal(){token='';csrf='';configLoaded=false;lastLog=0;clearTimeout(scanTimer);document.querySelectorAll('input[type=password]').forEach(e=>e.value='');$('log').value='';$('app').classList.add('hidden');$('loginPanel').classList.remove('hidden');message('Signed out.','warn')}
function body(values){return JSON.stringify(values)}function number(id){return Number($(id).value)}
function setMutable(enabled){controlsMutable=enabled;document.querySelectorAll('.mutable,#workflowPanel input,#networkPanel input,#accessPointPanel input').forEach(e=>e.disabled=!enabled);$('configLock').classList.toggle('hidden',enabled);$('workflowPanel').classList.toggle('locked',!enabled);$('networkPanel').classList.toggle('locked',!enabled);$('accessPointPanel').classList.toggle('locked',!enabled);$('staNetwork').disabled=!enabled;updateNetworkPasswordState()}
function updateNetworkPasswordState(){$('staPassword').disabled=!controlsMutable||$('staOpen').checked}
function selectDetectedNetwork(){const o=$('staNetwork').selectedOptions[0];if(!o||!o.value)return;$('staSsid').value=o.value;$('staOpen').checked=o.dataset.open==='true';$('staPassword').value='';updateNetworkPasswordState()}
function showScanResults(d){const select=$('staNetwork');select.replaceChildren();const prompt=document.createElement('option');prompt.value='';prompt.textContent=d.networks.length?'Select a network…':'No networks found';select.appendChild(prompt);for(const n of d.networks){const o=document.createElement('option');o.value=n.ssid;o.dataset.open=String(n.open);o.textContent=n.ssid+' ('+n.rssi+' dBm, channel '+n.channel+(n.open?', open':', secured')+')';select.appendChild(o)}}
async function refreshWifiScan(){if(scanBusy||!token)return;scanBusy=true;try{const d=await api('/api/v1/network/scan');$('scanStatus').textContent=d.state==='READY'?'Ready: '+d.networks.length+' network(s)':d.state;if(d.state==='READY')showScanResults(d);if(d.state==='QUEUED'||d.state==='RUNNING')scanTimer=setTimeout(refreshWifiScan,500)}catch(e){message(e.message,'error')}finally{scanBusy=false}}
async function startWifiScan(){if(scanBusy)return;clearTimeout(scanTimer);$('scanStatus').textContent='Requesting…';try{await api('/api/v1/network/scan',{method:'POST',body:'{}'});scanTimer=setTimeout(refreshWifiScan,100)}catch(e){message(e.message,'error');$('scanStatus').textContent='Error'}}
function loadConfig(c){if(configLoaded&&configRevision===c.revision)return;['goalWeightG','rinseGestureMs','rinseDurationMs','brewConfirmMs','minAutoStopMs','operationalWallMs'].forEach(k=>$(k).value=c[k]);['autoTare','timerOnly','canTareStartTimer','brewConfirmationBeep'].forEach(k=>$(k).checked=!!c[k]);configRevision=c.revision;configLoaded=true}
async function refreshStatus(){if(statusBusy)return;statusBusy=true;try{const s=await api('/api/v1/status');$('state').textContent=s.stateLabel+' ('+s.state+')';$('relay').textContent=s.relayClosed?'CLOSED':'open';$('timer').textContent=((s.relayClosed?s.cn9ElapsedMs:(s.lastCycle.valid?s.lastCycle.durationMs:0))/1000).toFixed(1)+' s';$('source').textContent=s.controlSource;$('scale').textContent=s.scale.available?'Connected':'Unavailable';$('weight').textContent=s.scale.currentWeightG===null?'—':s.scale.currentWeightG.toFixed(1)+' g ('+s.scale.weightAgeMs+' ms)';$('lastCycle').textContent=s.lastCycle.valid?'Last: '+(s.lastCycle.durationMs/1000).toFixed(1)+' s, '+s.lastCycle.endReason+', '+(s.lastCycle.lastWeightG===null?'no weight':s.lastCycle.lastWeightG.toFixed(1)+' g'):'No previous cycle.';$('networkStatus').textContent='STA: '+s.network.staState+(s.network.staIp?' — '+s.network.staIp:'')+(s.network.wifiConfigured?' — credentials saved':'');$('apStatus').textContent='AP: '+(s.network.apActive?'active':'inactive')+' — MicraShotStopperAP — '+s.network.apIp+' — '+s.network.apClients+' client(s)';$('virtualPaddle').checked=s.virtualPaddleOn;$('virtualPaddleState').textContent=s.virtualPaddleOn?'ON':'OFF';$('virtualPaddle').disabled=!(s.configMutable||s.virtualPaddleOn);$('rinseButton').disabled=!s.configMutable;$('stopButton').disabled=!s.relayClosed;$('restartButton').disabled=!s.configMutable;setMutable(s.configMutable);loadConfig(s.config)}catch(e){message(e.message,'error')}finally{statusBusy=false}}
async function refreshLog(){if(logBusy)return;logBusy=true;try{const d=await api('/api/v1/log?after='+lastLog);for(const e of d.events){$('log').value+='['+(e.atMs/1000).toFixed(3)+'] '+e.category+': '+e.message+(e.argument1||e.argument2?' ('+e.argument1+', '+e.argument2+')':'')+'\n';lastLog=e.sequence}$('log').scrollTop=$('log').scrollHeight}catch(e){message(e.message,'error')}finally{logBusy=false}}
async function heartbeat(){if(heartbeatBusy)return;heartbeatBusy=true;try{await api('/api/v1/heartbeat',{method:'POST',body:'{}'})}catch(e){message(e.message,'error')}finally{heartbeatBusy=false}}
async function command(path,value={}){try{await api(path,{method:'POST',body:body(value)});message('Request accepted.','ok');setTimeout(refreshStatus,150)}catch(e){message(e.message,'error');refreshStatus()}}
$('loginButton').onclick=async()=>{try{const d=await api('/api/v1/login',{method:'POST',headers:{},body:body({password:$('loginPassword').value})});token=d.token;csrf=d.csrf;$('loginPassword').value='';$('loginPanel').classList.add('hidden');$('app').classList.remove('hidden');message('Session active.','ok');await refreshStatus();await refreshLog()}catch(e){message(e.message,'error')}};
$('logoutButton').onclick=async()=>{try{await api('/api/v1/logout',{method:'POST',body:'{}'})}catch(_){}logoutLocal()};$('virtualPaddle').onchange=()=>command('/api/v1/control/paddle',{on:$('virtualPaddle').checked});$('rinseButton').onclick=()=>command('/api/v1/control/rinse');$('stopButton').onclick=()=>command('/api/v1/control/stop');$('restartButton').onclick=()=>{if(confirm('Restart the controller?'))command('/api/v1/control/restart')};$('scanNetworkButton').onclick=startWifiScan;$('staNetwork').onchange=selectDetectedNetwork;$('staOpen').onchange=updateNetworkPasswordState;$('saveConfigButton').onclick=()=>command('/api/v1/config',{goalWeightG:number('goalWeightG'),rinseGestureMs:number('rinseGestureMs'),rinseDurationMs:number('rinseDurationMs'),brewConfirmMs:number('brewConfirmMs'),minAutoStopMs:number('minAutoStopMs'),operationalWallMs:number('operationalWallMs'),autoTare:$('autoTare').checked,timerOnly:$('timerOnly').checked,canTareStartTimer:$('canTareStartTimer').checked,brewConfirmationBeep:$('brewConfirmationBeep').checked});$('resetCalibrationButton').onclick=()=>{if(confirm('Reset the learned stop offset to the default 1.5 g? This cannot be undone.'))command('/api/v1/calibration/reset')};$('saveNetworkButton').onclick=()=>command('/api/v1/network',{action:'save',ssid:$('staSsid').value,password:$('staPassword').value,open:$('staOpen').checked}).finally(()=>{$('staPassword').value='' });$('forgetNetworkButton').onclick=()=>{if(confirm('Forget the STA network and restart?'))command('/api/v1/network',{action:'forget'})};$('changeApPasswordButton').onclick=()=>{const n=$('newApPassword').value;if(n!==$('confirmApPassword').value){message('The new passwords do not match.','error');return}command('/api/v1/access-point/password',{currentPassword:$('currentApPassword').value,newPassword:n}).finally(()=>{['currentApPassword','newApPassword','confirmApPassword'].forEach(id=>$(id).value='')})};$('copyLogButton').onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($('log').value);$('clearLogButton').onclick=()=>{$('log').value=''};setInterval(()=>{if(token)refreshStatus()},1000);setInterval(()=>{if(token)refreshLog()},3000);setInterval(()=>{if(token)heartbeat()},10000);
</script>
</body></html>)HTML";

}  // namespace shotstopper
