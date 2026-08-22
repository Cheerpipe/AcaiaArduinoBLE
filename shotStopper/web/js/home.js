'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
export function applyStatus(s){R.applyHomeStatus(s)}
export function init(){
  if(ready)return;
  ready=true;
  R.registerViewStatus('home',applyStatus);
  
  $('rinseButton').onclick=()=>R.command('/api/v1/control/rinse');
  if($('homeAdminLock'))$('homeAdminLock').onclick=R.lockAdmin;
  $('stopButton').onclick=()=>$('stopButton').dataset.mode==='stop'?R.command('/api/v1/control/stop'):R.command('/api/v1/control/paddle',{on:true});
  $('clearLastShotButton').onclick=R.clearLastShot;
  if($('homeBrewByWeight'))$('homeBrewByWeight').onchange=R.persistHomeBrewByWeight;
  if($('homeAvoidBbwShotWithoutScale'))$('homeAvoidBbwShotWithoutScale').onchange=()=>R.persistHomeGuard('homeAvoidBbwShotWithoutScale','homeAvoidBbwShotWithoutScaleState','avoidBbwShotWithoutScale',0);
  if($('homeFastExtractionGuardEnabled'))$('homeFastExtractionGuardEnabled').onchange=()=>R.persistHomeGuard('homeFastExtractionGuardEnabled','homeFastExtractionGuardEnabledState','fastExtractionGuardEnabled',1);
  if($('homeAvoidAccidentalTouchEnabled'))$('homeAvoidAccidentalTouchEnabled').onchange=()=>R.persistHomeGuard('homeAvoidAccidentalTouchEnabled','homeAvoidAccidentalTouchEnabledState','avoidAccidentalTouchEnabled',1);
  if($('homeSlowExtractionGuardEnabled'))$('homeSlowExtractionGuardEnabled').onchange=()=>R.persistHomeGuard('homeSlowExtractionGuardEnabled','homeSlowExtractionGuardEnabledState','slowExtractionGuardEnabled',1);
  if($('homeAutoToManualGuardEnabled'))$('homeAutoToManualGuardEnabled').onchange=()=>R.persistHomeGuard('homeAutoToManualGuardEnabled','homeAutoToManualGuardEnabledState','autoToManualGuardEnabled',1);
  if($('homeCupProtectionEnabled'))$('homeCupProtectionEnabled').onchange=()=>R.persistHomeGuard('homeCupProtectionEnabled','homeCupProtectionEnabledState','cupProtectionEnabled',1);

  
}
export function activate(){}
