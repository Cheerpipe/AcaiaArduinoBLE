'use strict';

import * as R from '/js/runtime.js?v=__FW_ASSET_TAG__';

const assetQuery = new URL(import.meta.url).search;
const htmlCache = new Map();
const jsMods = new Map();
const htmlLoaded = new Set(['home']);
const viewLoads = new Map();
const SECONDARY = new Set(['history', 'diagnostic', 'admin']);

let secondaryViews = null;
let logTimer = 0;
let shotsTimer = 0;
let routeSeq = 0;
let activeView = '';

const ROUTES = {
  '/': 'home',
  '/settings': 'settings',
  '/history': 'history',
  '/admin': 'admin',
  '/diagnostic': 'diagnostic',
  '/log': 'diagnostic',
};

function knownPath(pathname) {
  const p = (pathname || '/').replace(/\/+$/, '') || '/';
  return Object.prototype.hasOwnProperty.call(ROUTES, p) ? p : null;
}

function viewToPath(view) {
  for (const [p, v] of Object.entries(ROUTES)) {
    if (v === view) return p;
  }
  return '/';
}

async function loadPartial(name) {
  if (htmlCache.has(name)) return htmlCache.get(name);
  const response = await fetch('/partials/' + name + '.html' + assetQuery);
  if (!response.ok) throw new Error('Failed to load view markup');
  const text = await response.text();
  htmlCache.set(name, text);
  return text;
}

async function ensureView(name) {
  if (jsMods.has(name) && htmlLoaded.has(name)) return jsMods.get(name);
  if (viewLoads.has(name)) return viewLoads.get(name);

  const load = (async () => {
    const section = document.getElementById('view-' + name);
    if (!section) throw new Error('Missing view section: ' + name);

    if (name === 'home') {
      if (!jsMods.has('home')) {
        jsMods.set('home', __homeModule);
        if (__homeModule.init) __homeModule.init();
      }
      return jsMods.get('home');
    }

    if (!htmlLoaded.has(name)) {
      section.innerHTML = await loadPartial(name);
      htmlLoaded.add(name);
    }
    if (!jsMods.has(name)) {
      let mod;
      if (SECONDARY.has(name)) {
        if (!secondaryViews) {
          const sec = await import('/js/secondary.js' + assetQuery);
          secondaryViews = sec.views;
        }
        mod = secondaryViews[name];
        if (!mod) throw new Error('Missing secondary view: ' + name);
      } else {
        mod = await import('/js/' + name + '.js' + assetQuery);
      }
      jsMods.set(name, mod);
      if (mod.init) mod.init();
    }
    return jsMods.get(name);
  })();

  viewLoads.set(name, load);
  try {
    return await load;
  } catch (e) {
    viewLoads.delete(name);
    throw e;
  }
}

R.setEnsureViewHook(ensureView);

function stopExtraPolls() {
  clearInterval(logTimer);
  clearInterval(shotsTimer);
  logTimer = shotsTimer = 0;
}

function startView(name) {
  if (!R.webUiPollingActive()) return;
  clearInterval(logTimer);
  clearInterval(shotsTimer);
  logTimer = shotsTimer = 0;
  if (name === 'home' || name === 'settings' || name === 'admin' ||
      name === 'diagnostic') {
    R.loadStatus();
    R.armStatusTimer();
  }
  if (name === 'diagnostic') {
    R.loadLog();
    logTimer = setInterval(() => {
      if (!document.hidden) R.refreshLog();
    }, 4e3);
  }
  if (name === 'history') {
    R.loadShots();
    shotsTimer = setInterval(() => {
      if (!document.hidden) R.refreshShots();
    }, 2e4);
  }
}

async function renderRoute(pathname) {
  const seq = ++routeSeq;
  const known = knownPath(pathname);
  let view = 'home';
  let target = '/';
  if (known) {
    view = ROUTES[known];
    target = known;
  }
  if (location.pathname !== target) history.replaceState({}, '', target);

  try {
    await ensureView(view);
  } catch (e) {
    R.message(e && e.message ? e.message : 'Unable to load view', 'error');
    return;
  }
  if (seq !== routeSeq) return;

  if (view === activeView) {
    startView(view);
    return;
  }
  R.stopViewPolls();
  activeView = view;
  R.setActiveView(view);
  document.querySelectorAll('.view').forEach(
      (el) => el.classList.toggle('hidden', el.dataset.view !== view));
  document.querySelectorAll('.pageNav a').forEach(
      (a) => a.classList.toggle(
          'active', a.getAttribute('data-route') === viewToPath(view)));
  startView(view);
}

function navigate(path) {
  const known = knownPath(path);
  const target = known ? known : '/';
  if (location.pathname !== target) history.pushState({}, '', target);
  renderRoute(target);
}

R.setViewPollHooks({
  stop: stopExtraPolls,
  start: startView,
  route: (pathname) => { renderRoute(pathname || location.pathname); },
});
R.setRouteRenderer(() => {
  if (activeView) startView(activeView);
  else renderRoute(location.pathname);
});

document.querySelectorAll('a[data-route]').forEach((a) => {
  a.addEventListener('click', (e) => {
    e.preventDefault();
    navigate(a.getAttribute('data-route') || '/');
  });
});
window.addEventListener('popstate', () => renderRoute(location.pathname));
document.addEventListener('visibilitychange', () => R.armStatusTimer());
document.addEventListener('pointerdown', R.noteWebUiInteraction, true);
document.addEventListener('click', R.noteWebUiInteraction, true);
document.addEventListener('input', R.noteWebUiInteraction, true);
document.addEventListener('change', R.noteWebUiInteraction, true);
document.addEventListener('keydown', R.noteWebUiInteraction, true);

R.setMutable(false);
renderRoute(location.pathname).finally(() => {
  R.claimWebUiOwnership();
});
