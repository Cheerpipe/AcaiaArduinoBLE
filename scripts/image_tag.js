#!/usr/bin/env node
'use strict';

// Reads a compiled ESP32-S3 application image and reports the Shot Stopper
// identity the controller will look for during an OTA verify.
//
//   node scripts/image_tag.js <image.bin> [--expect-arch n16r8] [--json]
//
// Exits non-zero when the file is not a usable Shot Stopper image, so the build
// can fail loudly rather than shipping a binary the controller would reject.

const fs = require('fs');
const crypto = require('crypto');

// Must stay in sync with FW_IMAGE_TAG_STRING (scripts/gen_version.sh) and the
// parser in shotStopper/ShotStopperOta.cpp.
const TAG_PREFIX = 'SHOTSTOPPER_FW_TAG_V1|';
const TAG_TERMINATOR = '|END';
const TAG_MAX_BYTES = 192;

const ESP_IMAGE_MAGIC = 0xe9;
const ESP_CHIP_ID_ESP32S3 = 0x0009;
const APP_DESC_OFFSET = 32;
const APP_DESC_MAGIC = 0xabcd5432;
const APP_DESC_PROJECT_NAME_OFFSET = APP_DESC_OFFSET + 48;
// esp_image_header_t.hash_appended: when set, the last 32 bytes are the
// SHA-256 of everything before them. This is the same digest the controller
// recomputes in esp_ota_end(), so checking it here catches a truncated or
// corrupted artifact before megabytes go over the air.
const IMAGE_HASH_APPENDED_OFFSET = 23;
const IMAGE_HASH_BYTES = 32;
// Arduino-ESP32 cores built by esp32-arduino-lib-builder share that project
// name. Native IDF firmware (./scripts/build-idf) uses CMake project(shotstopper).
// Either name proves a Shot Stopper-capable ESP32-S3 image; the tag below is
// what identifies the sketch.
const EXPECTED_PROJECT_NAMES = new Set(['arduino-lib-builder', 'shotstopper']);

function readCString(buffer, offset, capacity) {
  const end = Math.min(offset + capacity, buffer.length);
  let stop = end;
  for (let i = offset; i < end; i += 1) {
    if (buffer[i] === 0) {
      stop = i;
      break;
    }
  }
  return buffer.toString('latin1', offset, stop);
}

function parseTagBody(body) {
  const fields = {arch: '', ver: '', packed: ''};
  for (const part of body.split('|')) {
    const eq = part.indexOf('=');
    if (eq <= 0) continue;
    const key = part.slice(0, eq);
    if (Object.prototype.hasOwnProperty.call(fields, key)) {
      fields[key] = part.slice(eq + 1);
    }
  }
  if (!fields.arch || !fields.ver || !/^[0-9]+$/.test(fields.packed)) {
    return null;
  }
  return fields;
}

// Scans every occurrence rather than stopping at the first one: an image also
// contains the literals its own OTA code compares against.
function findImageTag(buffer) {
  const needle = Buffer.from(TAG_PREFIX, 'latin1');
  let from = 0;
  for (;;) {
    const at = buffer.indexOf(needle, from);
    if (at < 0) return null;
    from = at + 1;
    const bodyStart = at + needle.length;
    const window = buffer.toString(
        'latin1', bodyStart, Math.min(bodyStart + TAG_MAX_BYTES, buffer.length));
    const stop = window.indexOf(TAG_TERMINATOR);
    if (stop <= 0) continue;
    const parsed = parseTagBody(window.slice(0, stop));
    if (parsed !== null) return parsed;
  }
}

function inspectImage(filePath) {
  const buffer = fs.readFileSync(filePath);
  const problems = [];
  if (buffer.length < APP_DESC_OFFSET + 256) {
    problems.push('el archivo es demasiado corto para ser una imagen de aplicación');
    return {problems, buffer};
  }
  if (buffer[0] !== ESP_IMAGE_MAGIC) {
    problems.push(`falta la magia 0xE9 de imagen ESP (byte 0 = 0x${
        buffer[0].toString(16)})`);
  }
  const chipId = buffer.readUInt16LE(12);
  if (chipId !== ESP_CHIP_ID_ESP32S3) {
    problems.push(`chip incorrecto: 0x${chipId.toString(16).padStart(4, '0')} (se espera ESP32-S3 0x0009)`);
  }
  const descMagic = buffer.readUInt32LE(APP_DESC_OFFSET);
  if (descMagic !== APP_DESC_MAGIC) {
    problems.push('el descriptor de aplicación no tiene la magia esperada');
  }
  const projectName =
      readCString(buffer, APP_DESC_PROJECT_NAME_OFFSET, 32);
  if (!EXPECTED_PROJECT_NAMES.has(projectName)) {
    problems.push(`project_name inesperado: '${projectName}'`);
  }
  if (buffer[IMAGE_HASH_APPENDED_OFFSET] !== 1) {
    problems.push(
        'la imagen no lleva SHA-256 al final, así que no se puede comprobar ' +
        'que esté completa');
  } else {
    const body = buffer.subarray(0, buffer.length - IMAGE_HASH_BYTES);
    const expected = buffer.subarray(buffer.length - IMAGE_HASH_BYTES);
    const actual = crypto.createHash('sha256').update(body).digest();
    if (!actual.equals(expected)) {
      problems.push(
          'el SHA-256 final no coincide: la imagen está truncada o corrupta');
    }
  }
  const tag = findImageTag(buffer);
  if (tag === null) {
    problems.push(
        `no se encontró el marcador ${TAG_PREFIX}… (¿se compiló sin scripts/build?)`);
  }
  return {problems, tag, sizeBytes: buffer.length, projectName};
}

function main(argv) {
  let filePath = '';
  let expectArch = '';
  let asJson = false;
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--json') {
      asJson = true;
    } else if (arg === '--expect-arch') {
      expectArch = argv[i + 1] || '';
      i += 1;
    } else if (arg.startsWith('--expect-arch=')) {
      expectArch = arg.slice('--expect-arch='.length);
    } else if (!filePath) {
      filePath = arg;
    } else {
      process.stderr.write(`Argumento inesperado: ${arg}\n`);
      return 2;
    }
  }
  if (!filePath) {
    process.stderr.write(
        'Uso: node scripts/image_tag.js <imagen.bin> [--expect-arch <arq>] [--json]\n');
    return 2;
  }
  if (!fs.existsSync(filePath)) {
    process.stderr.write(`No existe ${filePath}\n`);
    return 1;
  }

  const result = inspectImage(filePath);
  if (expectArch && result.tag && result.tag.arch !== expectArch) {
    result.problems.push(
        `la imagen declara arch=${result.tag.arch} pero se esperaba ${expectArch}`);
  }

  if (result.problems.length > 0) {
    process.stderr.write(`Imagen no válida: ${filePath}\n`);
    for (const problem of result.problems) {
      process.stderr.write(`  - ${problem}\n`);
    }
    return 1;
  }

  if (asJson) {
    process.stdout.write(JSON.stringify({
      arch: result.tag.arch,
      version: result.tag.ver,
      packed: Number(result.tag.packed),
      sizeBytes: result.sizeBytes,
    }) + '\n');
  } else {
    process.stdout.write(
        `${result.tag.arch} ${result.tag.ver} ${result.tag.packed} ${result.sizeBytes}\n`);
  }
  return 0;
}

if (require.main === module) {
  process.exit(main(process.argv.slice(2)));
}

module.exports = {findImageTag, inspectImage, TAG_PREFIX, TAG_TERMINATOR};
