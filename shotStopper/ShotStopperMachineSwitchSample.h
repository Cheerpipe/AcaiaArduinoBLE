#pragma once

// =============================================================================
// Machine — shared switch sample (internal to specializations)
// =============================================================================
// WHAT: Debounced on/edge flags. Paddle debounce and momentary synthesis both
//       write these; the paddle/momentary translators read them.
//
// BOUNDARY: Internal machine sample only. Stopper / brew / cup / scale / guards
// must NEVER touch this state. They read UserIntent / MachineIntention from
// the façade. Do not add brew or scale fields here.

bool rawPaddleOn = false;
bool paddleOn = false;
bool paddleTurnedOn = false;
bool paddleTurnedOff = false;
uint32_t rawPaddleChangedAtMs = 0;
