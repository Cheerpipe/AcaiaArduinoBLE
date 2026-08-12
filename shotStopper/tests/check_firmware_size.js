'use strict';

const fs = require('fs');
const path = require('path');

const DEFAULT_OTA_APP_LIMIT = 1310720;
const binPath = process.argv[2] ||
  path.resolve(__dirname, '..', '..', 'build', 'esp32', 'shotStopper.ino.bin');

if (!fs.existsSync(binPath)) {
  console.log(`Firmware size check skipped: ${binPath} not found`);
  process.exit(0);
}

const size = fs.statSync(binPath).size;
if (size > DEFAULT_OTA_APP_LIMIT) {
  throw new Error(
    `Application image is ${size} bytes; default OTA slot allows ${DEFAULT_OTA_APP_LIMIT}. ` +
      'Compile with PartitionScheme=min_spiffs (see README).'
  );
}

console.log(`Application image: ${size} bytes (default OTA limit ${DEFAULT_OTA_APP_LIMIT})`);
