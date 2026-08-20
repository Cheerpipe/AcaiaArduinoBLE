'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
export function applyStatus(s){R.applyDiagnosticStatus(s)}
export function init(){
  if(ready)return;
  ready=true;
  R.registerViewStatus('diagnostic',applyStatus);
  if($('serialDebugOutput'))$('serialDebugOutput').onchange=()=>R.command('/api/v1/config',R.withBaseRev({serialDebugOutput:!!$('serialDebugOutput').checked}));
  if($('ringRetainLogLevel'))$('ringRetainLogLevel').onchange=()=>R.command('/api/v1/config',R.withBaseRev({ringRetainLogLevel:$('ringRetainLogLevel').value||'none'}));
  $('logFilter').onchange=R.renderLog;
  $('logLevelFilter').onchange=R.renderLog;
  $('copyLogButton').onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($('log').value);
  $('clearLogButton').onclick=()=>R.clearLogView();
}
export function activate(){}
