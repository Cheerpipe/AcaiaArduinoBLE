#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');
const {minify: terserMinify} = require('terser');

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

function collapseMarkup(markup) {
  return markup.replace(/>\s+</g, '><');
}

function minifyHtml(html) {
  html = html.replace(/<!--[\s\S]*?-->/g, '');
  const pieces = [];
  const re =
      /(<script\b[^>]*>)([\s\S]*?)(<\/script>)|(<style\b[^>]*>)([\s\S]*?)(<\/style>)/gi;
  let last = 0;
  let match;
  while ((match = re.exec(html))) {
    pieces.push(collapseMarkup(html.slice(last, match.index)));
    if (match[1]) {
      pieces.push(match[1], match[2], match[3]);
    } else {
      pieces.push(match[4], match[5], match[6]);
    }
    last = match.index + match[0].length;
  }
  pieces.push(collapseMarkup(html.slice(last)));
  return pieces.join('').trim();
}

function minifyCss(css) {
  return css
      .replace(/\/\*[\s\S]*?\*\//g, '')
      .replace(/\s+/g, ' ')
      .replace(/\s*([{}:;,])\s*/g, '$1')
      .replace(/;}/g, '}')
      .trim();
}

async function minifyJs(js) {
  const result = await terserMinify(js, {
    module: true,
    compress: true,
    mangle: true,
    format: {comments: false},
    ecma: 2018,
  });
  if (!result || !result.code) {
    throw new Error('Terser failed to minify Web UI JavaScript');
  }
  return result.code;
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

  const appJs = await minifyJs(stampAssetTag(appJsRaw, assetTag));
  const runtimeJs = await minifyJs(stampAssetTag(runtimeRaw, assetTag));
  const viewJs = {};
  for (const name of VIEW_NAMES) {
    viewJs[name] = await minifyJs(stampAssetTag(viewJsRaw[name], assetTag));
  }
  const css = minifyCss(cssSource);
  const shellHtml = minifyHtml(
      stampAssetTag(shellHtmlRaw, assetTag)
          .split('__FW_VERSION__')
          .join(`${version}.${assetTag}`));
  const partials = {};
  for (const name of VIEW_NAMES) {
    partials[name] = minifyHtml(partialsRaw[name]);
  }

  return finish({
    shellHtml,
    partials,
    appJs,
    runtimeJs,
    viewJs,
    css,
    assetTag,
    version,
  });
}

function finish({shellHtml, partials, appJs, runtimeJs, viewJs, css, assetTag,
                 version}) {
  const shellGzip = zlib.gzipSync(Buffer.from(shellHtml, 'utf8'), {level: 9});
  const cssGzip = zlib.gzipSync(Buffer.from(css, 'utf8'), {level: 9});
  const appJsGzip = zlib.gzipSync(Buffer.from(appJs, 'utf8'), {level: 9});
  const runtimeGzip =
      zlib.gzipSync(Buffer.from(runtimeJs, 'utf8'), {level: 9});
  const partialGzip = {};
  const viewGzip = {};
  for (const name of VIEW_NAMES) {
    partialGzip[name] =
        zlib.gzipSync(Buffer.from(partials[name], 'utf8'), {level: 9});
    viewGzip[name] =
        zlib.gzipSync(Buffer.from(viewJs[name], 'utf8'), {level: 9});
  }

  const cacheVersion = `${version}.${assetTag}`;
  let body = `#pragma once

#include <pgmspace.h>
#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

// Generated by scripts/gen_web_ui.js — do not edit.

constexpr char WEB_UI_ASSET_TAG[] = "${assetTag}";

${emitGzipConst('SHOT_STOPPER_WEB_UI_GZIP', shellGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_JS_GZIP', appJsGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_RUNTIME_GZIP', runtimeGzip)}
${emitGzipConst('SHOT_STOPPER_WEB_CSS_GZIP', cssGzip)}
`;

  for (const name of VIEW_NAMES) {
    const upper = name.toUpperCase();
    body += emitGzipConst(
        `SHOT_STOPPER_WEB_PARTIAL_${upper}_GZIP`, partialGzip[name]);
    body += emitGzipConst(
        `SHOT_STOPPER_WEB_VIEW_${upper}_GZIP`, viewGzip[name]);
  }

  body += `
}  // namespace shotstopper
`;

  fs.writeFileSync(outputPath, body);

  let combined = shellGzip.length + appJsGzip.length + runtimeGzip.length +
      cssGzip.length;
  for (const name of VIEW_NAMES) {
    combined += partialGzip[name].length + viewGzip[name].length;
  }

  return {
    html: shellHtml,
    js: appJs,
    runtimeJs,
    viewJs,
    css,
    partials,
    gzip: shellGzip,
    jsGzip: appJsGzip,
    runtimeGzip,
    viewGzip,
    partialGzip,
    cssGzip,
    assetTag,
    cacheVersion,
    combined,
    outputPath,
    sourcePath,
    appJsPath,
    cssSourcePath,
    version,
    VIEW_NAMES,
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
        ];
        for (const name of VIEW_NAMES) {
          parts.push(
              `${name}.html ${result.partialGzip[name].length} B`,
              `${name}.js ${result.viewGzip[name].length} B`);
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
