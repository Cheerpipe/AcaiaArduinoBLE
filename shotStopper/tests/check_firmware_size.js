'use strict';

const fs = require('fs');
const path = require('path');

// OTA app slot sizes for the FQBNs in scripts/shotstopper_board.sh.
const OTA_APP_LIMITS = {
  n16r8: 3145728, // app3M_fat9M_16MB (3 MB)
  n8r4: 3342336 // default_8MB (0x330000)
};
const DEFAULT_ARCH = 'n16r8';

const binPath = process.argv[2] ||
  path.resolve(__dirname, '..', '..', 'build', 'n16r8', 'shotStopper.ino.bin');

function archFromBinPath(filePath) {
  const normalized = filePath.replace(/\\/g, '/');
  if (normalized.includes('/n8r4/')) {
    return 'n8r4';
  }
  if (normalized.includes('/n16r8/')) {
    return 'n16r8';
  }
  return DEFAULT_ARCH;
}

if (!fs.existsSync(binPath)) {
  console.log(`Firmware size check skipped: ${binPath} not found`);
  process.exit(0);
}

const arch = archFromBinPath(binPath);
const limit = OTA_APP_LIMITS[arch];
const size = fs.statSync(binPath).size;
if (size > limit) {
  throw new Error(
    `Application image is ${size} bytes; ${arch} OTA slot allows ${limit}.`
  );
}

console.log(`Application image: ${size} bytes (${arch} OTA limit ${limit})`);
