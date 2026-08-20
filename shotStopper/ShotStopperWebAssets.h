#pragma once

#include <pgmspace.h>

namespace shotstopper {

// Editable Web UI shell (nav + view placeholders). View bodies live in
// shotStopper/web/html/*.html. scripts/gen_web_ui.js gzips everything into
// ShotStopperWebAssetsGzip.h for the firmware.
const char SHOT_STOPPER_WEB_UI[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Micra Shot Stopper</title><link rel="stylesheet" href="/app.css?v=__FW_VERSION__"></head><body><h1 class="brand"><a href="/" data-route="/">Micra Shot Stopper</a></h1><main id="app"><nav class="pageNav" aria-label="Primary"><a href="/" data-route="/">Home</a> · <a href="/settings" data-route="/settings">Settings</a> · <a href="/history" data-route="/history">History</a> · <a href="/admin" data-route="/admin">Admin</a> · <a href="/diagnostic" data-route="/diagnostic">Diagnostic</a></nav><p id="message" role="status" aria-live="polite"></p><section id="view-home" class="view" data-view="home"></section><section id="view-history" class="view" data-view="history"></section><section id="view-diagnostic" class="view" data-view="diagnostic"></section><section id="view-settings" class="view" data-view="settings"></section><section id="view-admin" class="view" data-view="admin"></section><footer class="pageFooter"><small id="firmwareFooter">Firmware — · Boot —</small></footer></main><script type="module" src="/app.js?v=__FW_VERSION__"></script></body></html>)HTML";

}  // namespace shotstopper
