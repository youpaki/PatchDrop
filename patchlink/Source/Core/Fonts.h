#pragma once
#include <JuceHeader.h>

// Embedded OFL typefaces (see Assets/Fonts). Cached shared instances.
struct PatchFonts
{
    static juce::Typeface::Ptr displayFace()
    {
        static juce::Typeface::Ptr f = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ArchivoBlackRegular_ttf, BinaryData::ArchivoBlackRegular_ttfSize);
        return f;
    }
    static juce::Typeface::Ptr uiFace()
    {
        static juce::Typeface::Ptr f = juce::Typeface::createSystemTypefaceFor(
            BinaryData::SpaceGroteskVariable_ttf, BinaryData::SpaceGroteskVariable_ttfSize);
        return f;
    }
    static juce::Typeface::Ptr monoFace()
    {
        static juce::Typeface::Ptr f = juce::Typeface::createSystemTypefaceFor(
            BinaryData::JetBrainsMonoVariable_ttf, BinaryData::JetBrainsMonoVariable_ttfSize);
        return f;
    }
    static juce::Font display(float size) { return juce::Font(displayFace()).withHeight(size); }
    static juce::Font ui(float size) { return juce::Font(uiFace()).withHeight(size); }
    static juce::Font mono(float size) { return juce::Font(monoFace()).withHeight(size); }
};
