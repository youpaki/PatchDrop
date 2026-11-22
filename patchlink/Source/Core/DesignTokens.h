#pragma once
#include <JuceHeader.h>

// PatchLink visual direction (2026): flat dark, one amber accent, mono for values.
// Simple rows 52px, patcher nodes 160x56, UI zoom 70-200%, thin 3px meters.
namespace design {
// Canvas & surfaces
static const juce::Colour canvas      { 0xff101014 };
static const juce::Colour card        { 0xff17181e };
static const juce::Colour raised       { 0xff23252f };
static const juce::Colour line        { 0xff2b2e3a };
// Signal colors: audio amber, midi mint, sidechain/control violet
static const juce::Colour audio       { 0xffffb224 };
static const juce::Colour midi        { 0xff3ddc84 };
static const juce::Colour control     { 0xff8b7cff };
// Text + states
static const juce::Colour text        { 0xfff2f3f5 };
static const juce::Colour secondary   { 0xff9aa0b2 };
static const juce::Colour danger      { 0xffef6461 };
static const juce::Colour ok          { 0xff3ddc84 };
static const juce::Colour warn        { 0xfff5b84b };

static constexpr int simpleRowH = 52;
static constexpr int nodeW = 160, nodeH = 56;
static constexpr int minTouch = 28;

inline juce::Font uiFont(float size = 13.0f, bool bold = false)
{
    return juce::Font(juce::Font::getDefaultSansSerifFontName(), size,
                      bold ? juce::Font::bold : juce::Font::plain);
}
inline juce::Font monoFont(float size = 11.0f)
{
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
}
} // namespace design
