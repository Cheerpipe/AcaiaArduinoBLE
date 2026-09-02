'use strict';

import * as R from '/js/runtime.js?v=__FW_ASSET_TAG__';

const THEME_KEY='ssTh',THEME_MODES=['auto','light','dark'];
function themeMode(){try{const v=localStorage.getItem(THEME_KEY);return THEME_MODES.includes(v)?v:'auto'}catch(_){return'auto'}}
function paintTheme(m){const h=document.documentElement,c=m==='auto'?'light dark':m;h.classList.toggle('theme-dark',m==='dark');h.classList.toggle('theme-light',m==='light');h.style.colorScheme=c;const e=document.querySelector('meta[name="color-scheme"]');if(e)e.content=c;const s=document.getElementById('uiTheme');if(s)s.value=m}
function setTheme(m){if(!THEME_MODES.includes(m))m='auto';try{localStorage.setItem(THEME_KEY,m)}catch(_){}paintTheme(m)}
paintTheme(themeMode());
document.addEventListener('change',e=>{if(e.target&&e.target.id==='uiTheme')setTheme(e.target.value)});
const diagnosticNav=document.querySelector('[data-route="/diagnostic"]');
if(diagnosticNav)diagnosticNav.classList.add('hidden');

const assetQuery = new URL(import.meta.url).search;
const htmlCache = new Map();
const jsMods = new Map();
const htmlLoaded = new Set(['home']);
const viewLoads = new Map();
const SECONDARY = new Set(['stats', 'diagnostic', 'admin']);

let secondaryViews = null;
let logTimer = 0;
let shotsTimer = 0;
let routeSeq = 0;
let activeView = '';

const ROUTES = {
  '/': 'home',
  '/settings': 'settings',
  '/stats': 'stats',
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
  if (name === 'stats') {
    const mod = jsMods.get('stats');
    if (mod && mod.activate) mod.activate();
    // Status unlocks controlsMutable (rating stars, delete, export). Without
    // this, a cold /stats load leaves stars disabled until another view polls.
    R.loadStatus();
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
  if (view === 'diagnostic') {
    try {
      const access = await R.api('/api/v1/status/home');
      const visible = access && access.diagnosticPageVisible === true;
      if (diagnosticNav) diagnosticNav.classList.toggle('hidden', !visible);
      if (!visible) {
        view = 'home';
        target = '/';
      }
    } catch (_) {
      view = 'home';
      target = '/';
    }
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

function setNav(open) {
  document.body.classList.toggle('navOpen', !!open);
  const btn = document.getElementById('navToggle');
  if (btn) {
    btn.type = 'button';
    btn.setAttribute('aria-expanded', open ? 'true' : 'false');
  }
}

function navigate(path) {
  const known = knownPath(path);
  const target = known ? known : '/';
  setNav(false);
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
const pageNav = document.querySelector('.pageNav');
if (pageNav) {
  pageNav.setAttribute('aria-label', 'Primary');
  pageNav.addEventListener('click', (e) => {
    if (e.target === pageNav) setNav(false);
  });
}
const msgEl = document.getElementById('message');
if (msgEl) {
  msgEl.setAttribute('role', 'status');
  msgEl.setAttribute('aria-live', 'polite');
}
setNav(false);
const navToggle = document.getElementById('navToggle');
if (navToggle) {
  navToggle.addEventListener('click', () => {
    setNav(!document.body.classList.contains('navOpen'));
  });
}
if (window.matchMedia) {
  const desktopNav = window.matchMedia('(min-width: 700px)');
  const closeDesktopNav = (e) => {
    if (e.matches) setNav(false);
  };
  if (desktopNav.addEventListener) {
    desktopNav.addEventListener('change', closeDesktopNav);
  } else if (desktopNav.addListener) {
    desktopNav.addListener(closeDesktopNav);
  }
}
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') setNav(false);
});
window.addEventListener('popstate', () => {
  setNav(false);
  renderRoute(location.pathname);
});
document.addEventListener('visibilitychange', () => R.armStatusTimer());
document.addEventListener('pointerdown', R.noteWebUiInteraction, true);
document.addEventListener('click', R.noteWebUiInteraction, true);
document.addEventListener('input', R.noteWebUiInteraction, true);
document.addEventListener('change', R.noteWebUiInteraction, true);
document.addEventListener('keydown', R.noteWebUiInteraction, true);

R.setMutable(false);
R.claimWebUiOwnership();
