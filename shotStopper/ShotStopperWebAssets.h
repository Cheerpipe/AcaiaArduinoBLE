#pragma once

#include <pgmspace.h>

namespace shotstopper {

// Editable Web UI shell (nav + empty view placeholders). View bodies live in
// shotStopper/web/html/*.html. scripts/gen_web_ui.js minifies, embeds home into
// the shell, bundles home/secondary JS, Zopfli-gzips, and writes
// ShotStopperWebAssetsGzip.h for the firmware.
const char SHOT_STOPPER_WEB_UI[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Micra Shot Stopper</title><link rel="stylesheet" href="/app.css?v=__FW_VERSION__"></head><body><header class="topBar"><h1 class="brand"><a href="/" data-route="/">Micra Shot Stopper</a></h1><nav class="pageNav"><a href="/" data-route="/">Home</a><a href="/settings" data-route="/settings">Settings</a><a href="/history" data-route="/history">History</a><a href="/admin" data-route="/admin">Admin</a><a href="/diagnostic" data-route="/diagnostic">Diagnostic</a></nav><button class="navToggle" id="navToggle" aria-label="Menu">☰</button></header><main id="app"><p id="message"></p><section id="view-home" class="view" data-view="home"></section><section id="view-history" class="view" data-view="history"></section><section id="view-diagnostic" class="view" data-view="diagnostic"></section><section id="view-settings" class="view" data-view="settings"></section><section id="view-admin" class="view" data-view="admin"></section><footer class="pageFooter"><small id="firmwareFooter">Firmware — · Boot —</small><br><small><a href="https://github.com/Cheerpipe/AcaiaArduinoBLE" target="_blank">GitHub</a> · Hecho por <a href="https://github.com/Cheerpipe" target="_blank">Cheerpipe</a></small></footer></main><script type="module" src="/app.js?v=__FW_VERSION__"></script></body></html>)HTML";

}  // namespace shotstopper
