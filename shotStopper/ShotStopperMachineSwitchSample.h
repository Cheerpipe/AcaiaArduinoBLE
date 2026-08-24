#pragma once

// Shared switch sample. Paddle debounce and momentary synthesis both write
// these; the translator reads them. Stopper/brew never touch this state.

bool rawPaddleOn = false;
bool paddleOn = false;
bool paddleTurnedOn = false;
bool paddleTurnedOff = false;
uint32_t rawPaddleChangedAtMs = 0;
