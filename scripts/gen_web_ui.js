#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const {minify: terserMinify} = require('terser');
const {minify: htmlMinify} = require('html-minifier-terser');
const CleanCSS = require('clean-css');
const zopfli = require('@gfx/zopfli');

const repoRoot = path.resolve(__dirname, '..');
const sourcePath = path.join(repoRoot, 'shotStopper', 'ShotStopperWebAssets.h');
const jsDir = path.join(repoRoot, 'shotStopper', 'web', 'js');
const htmlDir = path.join(repoRoot, 'shotStopper', 'web', 'html');
const appJsPath = path.join(repoRoot, 'shotStopper', 'web', 'app.js');
const cssSourcePath = path.join(repoRoot, 'shotStopper', 'web', 'app.css');
const versionPath = path.join(repoRoot, 'shotStopper', 'ShotStopperVersion.h');
const outputPath =
    path.join(repoRoot, 'shotStopper', 'ShotStopperWebAssetsGzip.h');

const VIEW_NAMES = ['home', 'history', 'diagnostic', 'settings', 'admin'];
const LAZY_PARTIALS = ['history', 'diagnostic', 'settings', 'admin'];
const SECONDARY_VIEWS = ['history', 'diagnostic', 'admin'];

function extractHtml(source) {
  const match = source.match(/R"HTML\(([\s\S]*?)\)HTML"/);
  if (!match) {
    throw new Error('Embedded HTML raw string not found');
  }
  return match[1];
}

function readFirmwareVersion() {
  if (!fs.existsSync(versionPath)) {
    return 'dev';
  }
  const source = fs.readFileSync(versionPath, 'utf8');
  const match = source.match(/FW_VERSION\[\]\s*=\s*"([^"]+)"/);
  return match ? match[1] : 'dev';
}

async function minifyHtml(html) {
  return htmlMinify(html, {
    collapseWhitespace: true,
    conservativeCollapse: true,
    removeComments: true,
    collapseBooleanAttributes: true,
    removeRedundantAttributes: true,
    minifyCSS: false,
    minifyJS: false,
  });
}

function minifyCss(css) {
  const out = new CleanCSS({level: 2}).minify(css);
  if (out.errors && out.errors.length) {
    throw new Error('CSS minify failed: ' + out.errors.join('; '));
  }
  return out.styles;
}

async function minifyJs(js) {
  const result = await terserMinify(js, {
    module: true,
    compress: {passes: 2},
    mangle: true,
    format: {comments: false},
    ecma: 2018,
  });
  if (!result || !result.code) {
    throw new Error('Terser failed to minify Web UI JavaScript');
  }
  return result.code;
}

function gzipBuffer(buffer) {
  return new Promise((resolve, reject) => {
    zopfli.gzip(buffer, {numiterations: 15}, (err, out) => {
      if (err) reject(err);
      else resolve(Buffer.from(out));
    });
  });
}

function formatByteArray(buffer) {
  const lines = [];
  for (let offset = 0; offset < buffer.length; offset += 12) {
    const slice = buffer.subarray(offset, offset + 12);
    const body = Array.from(slice, (byte) =>
      `0x${byte.toString(16).padStart(2, '0')}`
    ).join(', ');
    const suffix = offset + 12 < buffer.length ? ',' : '';
    lines.push(`    ${body}${suffix}`);
  }
  return lines.join('\n');
}

function stampAssetTag(source, assetTag) {
  return source.split('__FW_ASSET_TAG__').join(assetTag);
}

function emitGzipConst(name, buffer) {
  return `constexpr size_t ${name}_LEN = ${buffer.length};
const uint8_t ${name}[] PROGMEM = {
${formatByteArray(buffer)}
};

static_assert(sizeof(${name}) == ${name}_LEN,
              "gzip length mismatch for ${name}");
`;
}

function injectHomePartial(shellHtml, homePartial) {
  const re =
      /(<section id="view-home" class="view" data-view="home">)(<\/section>)/;
  if (!re.test(shellHtml)) {
    throw new Error('Shell missing empty view-home placeholder for home inject');
  }
  return shellHtml.replace(re, `$1${homePartial}$2`);
}

function inlineHomeModule(appSrc, homeSrc, assetTag) {
  const stamped = stampAssetTag(homeSrc, assetTag);
  const body = stamped
      .replace(/^['"]use strict['"];\s*/m, '')
      .replace(/import\s+\*\s+as\s+R\s+from\s+['"][^'"]+['"];\s*/m, '')
      .replace(/export\s+function\s+/g, 'function ');
  const iife =
      `const __homeModule=(()=>{${body}\nreturn{init,applyStatus,activate};})();`;
  if (!appSrc.includes('__homeModule')) {
    throw new Error('app.js must reference __homeModule for home cold path');
  }
  const marker = /import \* as R from ['"][^'"]+['"];\s*/;
  if (!marker.test(appSrc)) {
    throw new Error('app.js runtime import not found');
  }
  return appSrc.replace(marker, (m) => m + iife + '\n');
}

function buildSecondaryJs(viewJsRaw, assetTag) {
  const parts = [
    `'use strict';`,
    `import * as R from '/js/runtime.js?v=${assetTag}';`,
    `const $=R.$;`,
  ];
  for (const name of SECONDARY_VIEWS) {
    let body = stampAssetTag(viewJsRaw[name], assetTag);
    body = body
        .replace(/^['"]use strict['"];\s*/m, '')
        .replace(/import\s+\*\s+as\s+R\s+from\s+['"][^'"]+['"];\s*/m, '')
        .replace(/const\s+\$\s*=\s*R\.\$;\s*/m, '')
        .replace(/export\s+function\s+applyStatus/g, `function ${name}ApplyStatus`)
        .replace(/export\s+function\s+init/g, `function ${name}Init`)
        .replace(/export\s+function\s+activate/g, `function ${name}Activate`)
        .replace(/\blet ready=/g, `let ${name}Ready=`)
        .replace(/\bif\(ready\)/g, `if(${name}Ready)`)
        .replace(/\bready=true/g, `${name}Ready=true`)
        .replace(
            new RegExp(
                `registerViewStatus\\('${name}',applyStatus\\)`, 'g'),
            `registerViewStatus('${name}',${name}ApplyStatus)`);
    parts.push(`// ${name}`, body);
  }
  parts.push('export const views={');
  for (const name of SECONDARY_VIEWS) {
    parts.push(
        `  ${name}:{init:${name}Init,applyStatus:${name}ApplyStatus,activate:${name}Activate},`);
  }
  parts.push('};');
  return parts.join('\n');
}

async function generate() {
  const source = fs.readFileSync(sourcePath, 'utf8');
  const cssSource = fs.readFileSync(cssSourcePath, 'utf8');
  const version = readFirmwareVersion();

  const shellHtmlRaw = extractHtml(source);
  const partialsRaw = {};
  for (const name of VIEW_NAMES) {
    const partialPath = path.join(htmlDir, `${name}.html`);
    if (!fs.existsSync(partialPath)) {
      throw new Error(`Missing HTML partial: ${partialPath}`);
    }
    partialsRaw[name] = fs.readFileSync(partialPath, 'utf8');
  }

  const appJsRaw = fs.readFileSync(appJsPath, 'utf8');
  const runtimeRaw = fs.readFileSync(path.join(jsDir, 'runtime.js'), 'utf8');
  const viewJsRaw = {};
  for (const name of VIEW_NAMES) {
    const viewPath = path.join(jsDir, `${name}.js`);
    if (!fs.existsSync(viewPath)) {
      throw new Error(`Missing JS view module: ${viewPath}`);
    }
    viewJsRaw[name] = fs.readFileSync(viewPath, 'utf8');
  }

  // Fingerprint unstamped sources so import ?v= tags stay stable and match
  // WEB_UI_ASSET_TAG embedded in the firmware header.
  const hash = crypto.createHash('sha256');
  hash.update(shellHtmlRaw);
  for (const name of VIEW_NAMES) hash.update(partialsRaw[name]);
  hash.update(appJsRaw);
  hash.update(runtimeRaw);
  for (const name of VIEW_NAMES) hash.update(viewJsRaw[name]);
  hash.update(cssSource);
  const assetTag = hash.digest('hex').slice(0, 8);

  const partials = {};
  for (const name of VIEW_NAMES) {
    partials[name] = await minifyHtml(partialsRaw[name]);
  }

  let shellHtml = await minifyHtml(
      stampAssetTag(shellHtmlRaw, assetTag)
          .split('__FW_VERSION__')
          .join(`${version}.${assetTag}`));
  shellHtml = injectHomePartial(shellHtml, partials.home);

  const appWithHome =
      inlineHomeModule(stampAssetTag(appJsRaw, assetTag), viewJsRaw.home, assetTag);
  const appJs = await minifyJs(appWithHome);
  const runtimeJs = await minifyJs(stampAssetTag(runtimeRaw, assetTag));
  const secondaryJs =
      await minifyJs(buildSecondaryJs(viewJsRaw, assetTag));
  const settingsJs =
      await minifyJs(stampAssetTag(viewJsRaw.settings, assetTag));
  const css = minifyCss(cssSource);

  return finish({
    shellHtml,
    partials,
    appJs,
    runtimeJs,
    secondaryJs,
    settingsJs,
    css,
    assetTag,
    version,
  });
}

async function finish({shellHtml, partials, appJs, runtimeJs, secondaryJs,
                       settingsJs, css, assetTag, version}) {
  const shellGzip = await gzipBuffer(Buffer.from(shellHtml, 'utf8'));
  const cssGzip = await gzipBuffer(Buffer.from(css, 'utf8'));
  const appJsGzip = await gzipBuffer(Buffer.from(appJs, 'utf8'));
  const runtimeGzip = await gzipBuffer(Buffer.from(runtimeJs, 'utf8'));
  const secondaryGzip = await gzipBuffer(Buffer.from(secondaryJs, 'utf8'));
  const settingsGzip = await gzipBuffer(Buffer.from(settingsJs, 'utf8'));
  const partialGzip = {};
  for (const name of LAZY_PARTIALS) {
    partialGzip[name] =
        await gzipBuffer(Buffer.from(partials[name], 'utf8'));
  }

  const cacheVersion = `${version}.${assetTag}`;
  let body = `#pragma once

#include <pgmspace.h>
#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

// Generated by scripts/gen_web_ui.js — do not edit.

constexpr char WEB_UI_ASSET_TAG[] = "${assetTag}";
constexpr char WEB_UI_ETAG[] = ${JSON.stringify('"' + cacheVersion + '"')};

${emitGzipConst('SHOT_STOPPER_WEB_UI_GZIP', shellGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_JS_GZIP', appJsGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_RUNTIME_GZIP', runtimeGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_CSS_GZIP', cssGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_SECONDARY_GZIP', secondaryGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP', settingsGzip)}
`;

  for (const name of LAZY_PARTIALS) {
    const upper = name.toUpperCase();
    body += emitGzipConst(
        `SHOT_STOPPER_WEB_PARTIAL_${upper}_GZIP`, partialGzip[name]);
  }

  body += `
}  // namespace shotstopper
`;

  fs.writeFileSync(outputPath, body);

  let combined = shellGzip.length + appJsGzip.length + runtimeGzip.length +
      cssGzip.length + secondaryGzip.length + settingsGzip.length;
  for (const name of LAZY_PARTIALS) {
    combined += partialGzip[name].length;
  }

  return {
    html: shellHtml,
    js: appJs,
    runtimeJs,
    secondaryJs,
    settingsJs,
    css,
    partials,
    gzip: shellGzip,
    jsGzip: appJsGzip,
    runtimeGzip,
    secondaryGzip,
    settingsGzip,
    partialGzip,
    cssGzip,
    assetTag,
    cacheVersion,
    etag: `"${cacheVersion}"`,
    combined,
    outputPath,
    sourcePath,
    appJsPath,
    cssSourcePath,
    version,
    VIEW_NAMES,
    LAZY_PARTIALS,
    SECONDARY_VIEWS,
  };
}

module.exports = {
  extractHtml,
  minifyHtml,
  minifyJs,
  minifyCss,
  generate,
  sourcePath,
  appJsPath: appJsPath,
  jsSourcePath: appJsPath,
  cssSourcePath,
  outputPath,
  VIEW_NAMES,
  LAZY_PARTIALS,
  SECONDARY_VIEWS,
  htmlDir,
  jsDir,
};

if (require.main === module) {
  generate()
      .then((result) => {
        const parts = [
          `shell ${result.gzip.length} B`,
          `app.js ${result.jsGzip.length} B`,
          `runtime ${result.runtimeGzip.length} B`,
          `css ${result.cssGzip.length} B`,
          `secondary.js ${result.secondaryGzip.length} B`,
          `settings.js ${result.settingsGzip.length} B`,
        ];
        for (const name of LAZY_PARTIALS) {
          parts.push(`${name}.html ${result.partialGzip[name].length} B`);
        }
        console.log(
            `Generated ${result.outputPath} ` +
                `(${parts.join(', ')}, combined ${result.combined} B gzip, ` +
                `v=${result.cacheVersion})`);
      })
      .catch((error) => {
        console.error(error);
        process.exitCode = 1;
      });
}
