'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false,obs=0;
function armSentinel(){const el=$('shotLogSentinel');if(!el||obs)return;obs=new IntersectionObserver(es=>{if(es.some(e=>e.isIntersecting))R.loadMoreShots()},{rootMargin:'240px'});obs.observe(el)}
export function applyStatus(){}
export function init(){if(ready)return;ready=true;$('exportShotsButton').onclick=R.exportShotsCsv;$('clearShotsButton').onclick=R.clearShotHistory;armSentinel()}
export function activate(){armSentinel()}
