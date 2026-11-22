#pragma once
// Legacy include path — the system now lives in Hazard.h (palette + halftone +
// look-and-feel) and Fonts.h (embedded typefaces). Shims kept for call sites.
#include "Hazard.h"
#include "Fonts.h"

namespace design {
inline juce::Font uiFont(float size = 13.0f, bool = false) { return PatchFonts::ui(size); }
inline juce::Font monoFont(float size = 11.0f) { return PatchFonts::mono(size); }
inline juce::Font displayFont(float size = 34.0f) { return PatchFonts::display(size); }
} // namespace design
