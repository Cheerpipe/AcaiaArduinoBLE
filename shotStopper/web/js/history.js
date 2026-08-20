'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false;
export function applyStatus(){}
export function init(){
  if(ready)return;
  ready=true;
  
  $('exportShotsButton').onclick=R.exportShotsCsv;
  $('clearShotsButton').onclick=R.clearShotHistory;

}
export function activate(){}
