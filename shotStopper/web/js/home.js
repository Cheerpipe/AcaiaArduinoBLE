'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
export function applyStatus(s){R.applyHomeStatus(s);const link=document.querySelector('[data-route="/diagnostic"]');if(link&&typeof s.diagnosticPageVisible==='boolean')link.classList.toggle('hidden',!s.diagnosticPageVisible)}
export function init(){
  if(ready)return;
  ready=true;
  R.registerViewStatus('home',applyStatus);
  $('rinseButton').onclick=()=>R.command('/api/v1/control/rinse');
  $('stopButton').onclick=()=>$('stopButton').dataset.mode==='stop'?R.command('/api/v1/control/stop'):R.command('/api/v1/control/paddle',{on:true});
  $('clearLastShotButton').onclick=R.clearLastShot;
  [['overrideIdleLink','off'],['overrideBrewingLink','on']].forEach(([i,s])=>{const a=$(i);if(!a)return;a.onclick=e=>{e.preventDefault();if(a.getAttribute('aria-disabled')==='true')return;R.command('/api/v1/control/state-override',{state:s})}});
  if($('homeBrewByWeight'))$('homeBrewByWeight').onchange=R.persistHomeBrewByWeight;
  if($('homeNoScaleBbwEnabled'))$('homeNoScaleBbwEnabled').onchange=R.persistHomeNoScaleBbw;
  if($('homeFastExtractionGuardEnabled'))$('homeFastExtractionGuardEnabled').onchange=()=>R.persistHomeGuard('homeFastExtractionGuardEnabled','homeFastExtractionGuardEnabledState','fastExtractionGuardEnabled',1);
  if($('homeAvoidAccidentalTouchEnabled'))$('homeAvoidAccidentalTouchEnabled').onchange=()=>R.persistHomeGuard('homeAvoidAccidentalTouchEnabled','homeAvoidAccidentalTouchEnabledState','avoidAccidentalTouchEnabled',1);
  if($('homeSlowExtractionGuardEnabled'))$('homeSlowExtractionGuardEnabled').onchange=()=>R.persistHomeGuard('homeSlowExtractionGuardEnabled','homeSlowExtractionGuardEnabledState','slowExtractionGuardEnabled',1);
  if($('homeAutoToManualGuardEnabled'))$('homeAutoToManualGuardEnabled').onchange=()=>R.persistHomeGuard('homeAutoToManualGuardEnabled','homeAutoToManualGuardEnabledState','autoToManualGuardEnabled',1);
  if($('homeCupProtectionEnabled'))$('homeCupProtectionEnabled').onchange=()=>R.persistHomeGuard('homeCupProtectionEnabled','homeCupProtectionEnabledState','cupProtectionEnabled',1);
}
export function activate(){}
