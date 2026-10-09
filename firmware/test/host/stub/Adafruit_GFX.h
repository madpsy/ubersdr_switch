#pragma once
// Host-side Adafruit GFX stand-in. A GFXfont carries rough metrics (average glyph
// advance and cap height) so the firmware's "largest font that fits" logic behaves
// as it does on the panel.
#include "Arduino.h"
struct GFXfont { const char *name; int charW; int height; };
