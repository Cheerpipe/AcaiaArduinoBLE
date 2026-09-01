'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
let webhookDirty=false;
export function applyStatus(s){
  R.applyAdminStatus(s);
  const w=s&&s.webhooks;
  if(!w)return;
  if(!webhookDirty){
    $('webhookEnabled').checked=!!w.enabled;
    $('webhookUrl').value=w.url||'';
    $('webhookBrewState').checked=!!w.brewState;
    $('webhookFirstDrop').checked=!!w.firstDrop;
    $('webhookEnd').checked=!!w.end;
  }
  const result=w.sending?'Sending…':w.lastAttemptAtMs?(w.lastSuccess?'Last delivery succeeded'+(w.lastHttpStatus?' (HTTP '+w.lastHttpStatus+')':''):'Last delivery failed'+(w.lastHttpStatus?' (HTTP '+w.lastHttpStatus+')':'')+(w.lastError?' · error '+w.lastError:'')):'No delivery attempted yet.';
  $('webhookStatus').textContent=result+' Sent '+(w.sent||0)+', dropped '+(w.dropped||0)+'.';
}
export function init(){
  if(ready)return;
  ready=true;
  R.registerViewStatus('admin',applyStatus);
  
  R.ensureBleCompanionPanel();
  const webhookPanel=document.createElement('fieldset');
  webhookPanel.id='webhookPanel';
  webhookPanel.innerHTML='<legend>Webhooks</legend><label><input id="webhookEnabled" type="checkbox" role="switch"> Enable webhooks</label><label>Webhook URL <input id="webhookUrl" type="url" maxlength="191" placeholder="http://192.168.1.10:8123/path" autocomplete="off" spellcheck="false"><small class="fieldHint">HTTP only. Events are asynchronous and are not retried.</small></label><details><summary>Events</summary><label><input id="webhookBrewState" type="checkbox" checked> Brew state<small class="fieldHint">Brewing waits for a new scale sample when available and is always queued within 2 seconds; idle is sent after the shot.</small></label><label><input id="webhookFirstDrop" type="checkbox" checked> First drop</label><label><input id="webhookEnd" type="checkbox" checked> End<small class="fieldHint">Sent after drip delay with final time and weight.</small></label></details><div id="webhookStatus">—</div><div class="btnBar"><button id="saveWebhookButton" class="btnGlyph mutable btnInvert" type="button"><span class="g">✓</span><span class="t">Save</span></button><button id="testWebhookButton" class="btnGlyph mutable" type="button"><span class="g">↗</span><span class="t">Send test</span></button></div><small id="webhookDirtyHint" class="warn hidden">Unsaved changes</small>';
  $('devicePasswordPanel').before(webhookPanel);
  $('bleCompanionEnabled').onchange=R.setBleCompanionEnabled;
  $('bleScanIntensity').onchange=R.setBleScanIntensity;
  const unlock=()=>{const pw=$('adminUnlockPassword').value;if(!pw){R.showFieldError('adminUnlockPassword','Device password is required.');return}R.clearFieldErrors();R.api('/api/v1/admin/unlock',{method:'POST',body:R.body({password:pw})}).then(()=>{$('adminUnlockPassword').value='';R.noteReachOk();R.message('Administration unlocked.','ok');return R.refreshStatus()}).catch(e=>R.message(R.formatCommandError('Could not unlock administration.',e),'error'))};
  $('adminUnlockButton').onclick=unlock;
  $('adminUnlockPassword').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();unlock()}});
  $('adminLockButton').onclick=R.lockAdmin;
  $('restartButton').onclick=()=>{if(confirm('Restart the controller?'))R.command('/api/v1/control/restart')};
  $('scanNetworkButton').onclick=R.startWifiScan;
  $('staNetwork').onchange=R.selectDetectedNetwork;
  $('staOpen').onchange=R.updateNetworkPasswordState;
  $('staIpMode').onchange=R.updateStaticIpFieldsState;
  $('saveDateTimeButton').onclick=R.saveDateTimeConfig;
  document.querySelectorAll('#dateTimePanel input,#dateTimePanel select').forEach(el=>{
    const fn=()=>R.markDateTimeDirty();
    el.addEventListener('input',fn);el.addEventListener('change',fn);
  });
  $('syncTimeButton').onclick=()=>R.command('/api/v1/time/sync');
  const webhookChanged=()=>{webhookDirty=true;$('webhookDirtyHint').classList.remove('hidden')};
  ['webhookEnabled','webhookUrl','webhookBrewState','webhookFirstDrop','webhookEnd'].forEach(id=>{const el=$(id);el.addEventListener('input',webhookChanged);el.addEventListener('change',webhookChanged)});
  $('saveWebhookButton').onclick=()=>{
    const url=$('webhookUrl').value.trim(),enabled=$('webhookEnabled').checked;
    if((enabled||url)&&!/^http:\/\/[^\s/@#"\\]+(?:\/[^\s#"\\]*)?$/i.test(url)){R.showFieldError('webhookUrl','Enter an HTTP URL. HTTPS is not supported.');return}
    R.clearFieldErrors();
    R.command('/api/v1/webhooks',{action:'save',enabled,url,brewState:$('webhookBrewState').checked,firstDrop:$('webhookFirstDrop').checked,end:$('webhookEnd').checked},true,'Webhook settings saved.').then(()=>{webhookDirty=false;$('webhookDirtyHint').classList.add('hidden')}).catch(e=>R.message(R.formatCommandError('Could not save webhook settings.',e),'error'));
  };
  $('testWebhookButton').onclick=()=>R.command('/api/v1/webhooks',{action:'test'},false,'Webhook test queued.');
  $('saveNetworkButton').onclick=()=>{const err=R.validateNetworkClient();if(err){R.showFieldError(err.id,err.msg);return}R.clearFieldErrors();const payload=R.networkSavePayload(),sleepOnly=!!payload._noReconnectWait,staticMode=$('staIpMode').value==='static';if(!sleepOnly&&!confirm(staticMode?'Save static IP and restart? Open the new IP if this page does not return.':'Save Wi-Fi and restart?'))return;R.command('/api/v1/network',payload).finally(()=>{$('staPassword').value='';if(!sleepOnly)R.resetNetworkAddressLoaded()})};
  $('forgetNetworkButton').onclick=()=>{if(confirm('Forget the STA network and restart?'))R.command('/api/v1/network',{action:'forget'}).finally(()=>R.resetNetworkAddressLoaded())};
  $('changeDevicePasswordButton').onclick=()=>{const err=R.validateDevicePasswordClient();if(err){R.showFieldError(err.id,err.msg);return}R.clearFieldErrors();R.command('/api/v1/device/password',{newPassword:$('newDevicePassword').value}).finally(()=>{['newDevicePassword','confirmDevicePassword'].forEach(id=>$(id).value='')})};
  $('otaVerifyButton').onclick=R.otaUpload;
  $('otaFlashButton').onclick=R.otaFlash;
  $('otaDiscardButton').onclick=R.otaDiscard;
  $('factoryResetButton').onclick=()=>{if(confirm('Restore all factory settings? This erases Wi-Fi, config, calibration, history, and the device password, then restarts. This cannot be undone.'))R.command('/api/v1/factory-reset',{confirm:'ERASE_ALL_SETTINGS'})};
  R.populateTimezoneOptions();
  const themeSel=$('uiTheme');
  if(themeSel){
    try{const v=localStorage.getItem('ssTh');themeSel.value=['auto','light','dark'].includes(v)?v:'auto'}catch(_){themeSel.value='auto'}
  }

  
  // networkAddressLoaded reset on save/forget handled below

}
export function activate(){}
