'use strict';
import * as R from './runtime.js?v=__FW_ASSET_TAG__';
const $=R.$;
let ready=false,obs=0;
function armSentinel(){const el=$('shotLogSentinel');if(!el||obs)return;obs=new IntersectionObserver(es=>{if(es.some(e=>e.isIntersecting))R.loadMoreShots()},{rootMargin:'240px'});obs.observe(el)}
export function applyStatus(){}
export function init(){if(ready)return;ready=true;$('exportShotsButton').onclick=R.exportShotsCsv;$('clearShotsButton').onclick=R.clearShotHistory;const h=$('shotSort');if(h){h.innerHTML='<button id="sortDateButton" type="button" aria-pressed="true">Date</button><button id="sortRatingButton" type="button">Rating</button><button id="sortDirButton" type="button" aria-label="Newest first">↓</button>';$('sortDateButton').onclick=()=>R.setShotSort('date');$('sortRatingButton').onclick=()=>R.setShotSort('rating');$('sortDirButton').onclick=()=>R.toggleShotSortDir();R.syncShotSortButtons()}armSentinel()}
export function activate(){armSentinel()}
