'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
export function applyStatus(s){R.applySettingsStatus(s)}
export function init(){
  if(ready)return;
  ready=true;
  R.registerViewStatus('settings',applyStatus);
  
  $('saveConfigButton').onclick=R.saveMachineConfig;
  if($('presetNewBtn'))$('presetNewBtn').onclick=()=>{if(R.brewDirty&&!confirm('You have unsaved preset changes. Discard them and create a new preset?'))return;R.invalidateSettingsHydration();R.command('/api/v1/presets',{action:'new'})};
  if($('presetDupBtn'))$('presetDupBtn').onclick=()=>{if(R.brewDirty&&!confirm('You have unsaved preset changes. Discard them and duplicate?'))return;R.invalidateSettingsHydration();R.command('/api/v1/presets',{action:'duplicate',id:R.presetState.selectedId||R.presetState.activeId})};
  if($('saveBrewPresetButton'))$('saveBrewPresetButton').onclick=R.saveBrewPreset;
  if($('presetResetBtn'))$('presetResetBtn').onclick=()=>{const p=R.selectedPreset();if(!p||!p.isFactory)return;if(!confirm('Reset this factory preset to its default values?'))return;if(R.brewDirty)R.clearBrewDirty();R.invalidateSettingsHydration();R.command('/api/v1/presets',{action:'restore_factory_values',id:p.id})};
  if($('presetDeleteBtn'))$('presetDeleteBtn').onclick=()=>{const p=R.selectedPreset();if(!p||p.isFactory)return;if(confirm('Delete this preset?'))R.command('/api/v1/presets',{action:'delete',id:p.id})};
  if($('scalePreference'))$('scalePreference').onchange=()=>{R.updateScalePreferenceOptions();R.markConfigDirty()};
  $('forgetPairedScale').onclick=R.forgetPairedScale;
  if($('preferredScaleSelect'))$('preferredScaleSelect').onchange=()=>{R.selectPreferredScale();R.updateScalePreferenceOptions()};
  $('autoTare').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()};
  $('autoRetare').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()};
  $('avoidBbwShotWithoutScale').onchange=()=>{R.updateConfigGroups();R.syncHomeGuardSwitchesFromSettings();R.markConfigDirty()};
  $('soundAlertsEnabled').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()};
  $('paddleReturnReminderBeep').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()};
  $('alertOutputChannel').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()};
  $('fastExtractionGuardEnabled').onchange=()=>{R.updateConfigGroups();R.syncHomeGuardSwitchesFromSettings()};
  if($('avoidAccidentalTouchEnabled'))$('avoidAccidentalTouchEnabled').onchange=()=>{R.syncHomeGuardSwitchesFromSettings()};
  $('slowExtractionGuardEnabled').onchange=()=>{R.updateConfigGroups();R.syncHomeGuardSwitchesFromSettings()};
  $('autoToManualGuardLimitMode').onchange=()=>{R.updateConfigGroups()};
  $('autoToManualGuardEnabled').onchange=()=>{R.syncHomeGuardSwitchesFromSettings()};
  if($('cupProtectionEnabled'))$('cupProtectionEnabled').onchange=()=>{R.updateConfigGroups();R.syncHomeGuardSwitchesFromSettings()};
  $('operationalWallS').addEventListener('input',R.updateConfigGroups);
  document.querySelectorAll('#workflowPanel input,#workflowPanel select').forEach(el=>{
    if(el.id==='preferredScaleSelect')return;
    const brewIds=['brewByWeight','goalWeightG','operationalWallS','bbwProtectionS','weightOffsetBaselineG','cupProtectionEnabled','stopIfCupRemoved','requireCupToStart','fastExtractionGuardEnabled','avoidAccidentalTouchEnabled','maxRecoveryWeightG','minBrewTimeS','slowExtractionGuardEnabled','minRecoveryWeightG','maxBrewTimeS','autoToManualGuardEnabled','autoToManualGuardLimitMode','autoToManualGuardManualLimitS','autoToManualGuardBaselineS'];
    const fn=()=>brewIds.includes(el.id)?R.markBrewDirty():R.markConfigDirty();
    el.addEventListener('input',fn);el.addEventListener('change',fn);
  });
  $('resetCalibrationButton').onclick=()=>{if(R.brewDirty){R.message('Save the preset first so the baseline is stored.','warn');return}const b=R.number('weightOffsetBaselineG');if(!Number.isFinite(b)){R.message('Set a valid offset baseline first.','warn');return}if(confirm('Reset learned stop offset to '+b.toFixed(2)+' g?'))R.command('/api/v1/calibration/reset')};
  $('resetGuardSamplesButton').onclick=()=>{if(R.brewDirty){R.message('Save the preset first so the baseline is stored.','warn');return}const s=R.number('autoToManualGuardBaselineS');if(!Number.isFinite(s)){R.message('Set a valid A→M baseline first.','warn');return}if(confirm('Reset A→M samples to 5×'+s+' s?'))R.command('/api/v1/calibration/reset-guard-samples')};

  
}
export function activate(){}
