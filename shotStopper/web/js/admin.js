'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
export function applyStatus(s){R.applyAdminStatus(s)}
export function init(){
  if(ready)return;
  ready=true;
  R.registerViewStatus('admin',applyStatus);
  
  R.ensureBleCompanionPanel();
  $('bleCompanionEnabled').onchange=R.setBleCompanionEnabled;
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
  $('saveNetworkButton').onclick=()=>{const err=R.validateNetworkClient();if(err){R.showFieldError(err.id,err.msg);return}R.clearFieldErrors();const staticMode=$('staIpMode').value==='static';if(!confirm(staticMode?'Save static IP and restart? Then open that IP within 3 min.':'Save Wi-Fi and restart? Open this UI within 3 min to confirm.'))return;R.command('/api/v1/network',R.networkSavePayload()).finally(()=>{$('staPassword').value='';R.resetNetworkAddressLoaded()})};
  $('forgetNetworkButton').onclick=()=>{if(confirm('Forget the STA network and restart?'))R.command('/api/v1/network',{action:'forget'}).finally(()=>R.resetNetworkAddressLoaded())};
  $('changeDevicePasswordButton').onclick=()=>{const err=R.validateDevicePasswordClient();if(err){R.showFieldError(err.id,err.msg);return}R.clearFieldErrors();R.command('/api/v1/device/password',{newPassword:$('newDevicePassword').value}).finally(()=>{['newDevicePassword','confirmDevicePassword'].forEach(id=>$(id).value='')})};
  $('otaVerifyButton').onclick=R.otaUpload;
  $('otaFlashButton').onclick=R.otaFlash;
  $('otaDiscardButton').onclick=R.otaDiscard;
  $('factoryResetButton').onclick=()=>{if(confirm('Restore all factory settings? This erases Wi-Fi, config, calibration, history, and the device password, then restarts. This cannot be undone.'))R.command('/api/v1/factory-reset',{confirm:'ERASE_ALL_SETTINGS'})};
  R.populateTimezoneOptions();

  
  // networkAddressLoaded reset on save/forget handled below

}
export function activate(){}
