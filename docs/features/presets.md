# Presets

Brew recipes are stored as **presets**. Factory **Single** and **Double**
ship by default (Double is active). You can create untitled presets,
duplicate, rename, save, or delete customs—you cannot delete the last
remaining preset.

## What lives on a preset

Per-preset brew settings include target weight, Max BBW time, BBW
protection, baseline and learned stop offset, and the Fast / Slow / A→M
guard parameters. Machine-level settings (paddle feel, tare, cup
thresholds, alerts, scales, Wi-Fi) are shared across presets.

## In the Web UI

**Settings → Brew** shows the preset cards. Load a card to make it active;
edit and **Save preset** to persist. **Home → Quick Settings** can turn
brew by weight off for the session (Manual) without writing
`brewByWeight=false` into the saved recipe.

The active preset id survives reboot. Learned offset stays with that
preset when you switch away and back; **Reset learned stop offset**
restores that preset’s baseline.

Related: [Brew by weight](brew-by-weight.md),
[Fast extraction guard](fast-extraction-guard.md),
[Slow extraction guard](slow-extraction-guard.md),
[A→M time guard](auto-to-manual.md),
[Cup protection](cup-protection.md).
