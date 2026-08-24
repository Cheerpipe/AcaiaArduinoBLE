#pragma once

// =============================================================================
// Machine — shared activator sample (internal to specializations)
// =============================================================================
// WHAT: Debounced on/edge flags for the physical activator GPIO. Paddle debounce
//       and momentary synthesis both write these; translators read them.
//
// BOUNDARY: Internal machine sample only. Stopper / brew / cup / scale / guards
// must NEVER touch this state. They read UserIntent / MachineIntention from
// the façade. Do not add brew or scale fields here.

bool rawActivatorOn = false;
bool activatorOn = false;
bool activatorTurnedOn = false;
bool activatorTurnedOff = false;
uint32_t rawActivatorChangedAtMs = 0;
