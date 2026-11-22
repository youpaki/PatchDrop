#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>
#include "Fonts.h"

// PatchLink visual direction: NEO HALFTONE HAZARD.
// Near-black canvas, halftone dot fields, one electric yellow accent,
// Archivo Black display / Space Grotesk UI / JetBrains Mono values.
namespace design {
static const juce::Colour bg        { 0xff0b0b0c };
static const juce::Colour panel     { 0xff121214 };
static const juce::Colour card      { 0xff151517 };
static const juce::Colour raised    { 0xff1c1c1f };
static const juce::Colour line      { 0xff2a2a2e };
static const juce::Colour yellow    { 0xffffe600 };
static const juce::Colour yellowDim { 0xff6b6400 };
static const juce::Colour text      { 0xfff5f5f0 };
static const juce::Colour secondary { 0xff9a9aa0 };
static const juce::Colour danger    { 0xffff3b30 };
static const juce::Colour ok        { 0xff3ddc84 };
static const juce::Colour warn      { 0xffffe600 };
// signal colors on canvas: audio yellow, midi white, control amber dashed
static const juce::Colour audio     { 0xffffe600 };
static const juce::Colour midi      { 0xfff5f5f0 };
static const juce::Colour control   { 0xffffb300 };

static constexpr int simpleRowH = 52;
static constexpr int nodeW = 160, nodeH = 56;
static constexpr int minTouch = 28;
static constexpr int corner = 3;

// Back-compat shorthands used across the UI
static const juce::Colour canvas = bg;
} // namespace design

// Halftone dot fields + hazard stripes, cached per size.
struct Halftone
{
    static void dots(juce::Graphics& g, juce::Rectangle<int> area, juce::Colour dot,
                     int gap = 16, float radius = 1.5f)
    {
        if (area.isEmpty()) return;
        auto key = std::make_tuple(area.getWidth(), area.getHeight(), gap,
                                   (int)(radius * 10), (int)dot.getARGB());
        auto it = cache.find(key);
        if (it == cache.end())
        {
            juce::Image img(juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
            juce::Graphics gg(img);
            gg.setColour(dot);
            for (int y = gap / 2; y < area.getHeight(); y += gap)
                for (int x = gap / 2; x < area.getWidth(); x += gap)
                    gg.fillEllipse((float)x - radius, (float)y - radius, radius * 2, radius * 2);
            it = cache.emplace(key, std::move(img)).first;
            if (cache.size() > 6) cache.clear();
        }
        g.drawImageAt(it->second, area.getX(), area.getY());
    }

    static void stripes(juce::Graphics& g, juce::Rectangle<int> area, int stripeW = 14)
    {
        if (area.isEmpty()) return;
        g.saveState();
        g.reduceClipRegion(area);
        g.setColour(juce::Colours::black);
        g.fillRect(area);
        g.setColour(design::yellow);
        for (int x = -area.getHeight(); x < area.getRight() + area.getHeight(); x += stripeW * 2)
        {
            juce::Path p;
            p.addQuadrilateral((float)x, (float)area.getBottom(), (float)(x + stripeW), (float)area.getBottom(),
                               (float)(x + stripeW + area.getHeight()), (float)area.getY(),
                               (float)(x + area.getHeight()), (float)area.getY());
            g.fillPath(p);
        }
        g.restoreState();
    }

private:
    inline static std::map<std::tuple<int, int, int, int, int>, juce::Image> cache;
};

// Hazard look: square yellow/black buttons, square checkboxes, yellow scrollbars.
struct HazardLookAndFeel : public juce::LookAndFeel_V4
{
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool hover, bool isDown) override
    {
        auto r = b.getLocalBounds().toFloat();
        bool hot = isDown || (b.getToggleState() && b.getClickingTogglesState());
        g.setColour(hot ? design::yellow : design::card);
        g.fillRoundedRectangle(r, design::corner);
        g.setColour(hot ? design::yellow : (hover ? design::yellow : design::line));
        g.drawRoundedRectangle(r, design::corner, hover || hot ? 2.0f : 1.0f);
        if (hot) return;
        // top hazard tick on primary-ish buttons
        if (b.getButtonText().isNotEmpty() && hover)
        {
            g.setColour(design::yellow);
            g.fillRect(r.getX(), r.getY(), 14.0f, 2.0f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool isDown) override
    {
        bool hot = isDown || (b.getToggleState() && b.getClickingTogglesState());
        g.setColour(hot ? juce::Colours::black : design::text);
        g.setFont(PatchFonts::ui(13.0f));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
    }
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds();
        auto box = r.removeFromLeft(16).reduced(1);
        g.setColour(design::card);
        g.fillRect(box);
        g.setColour(b.getToggleState() ? design::yellow : (hover ? design::secondary : design::line));
        g.drawRect(box, b.getToggleState() ? 2 : 1);
        if (b.getToggleState())
        {
            g.setColour(design::yellow);
            g.fillRect(box.reduced(4));
        }
        g.setColour(design::text);
        g.setFont(PatchFonts::ui(12.0f));
        g.drawText(b.getButtonText(), r.reduced(6, 0), juce::Justification::centredLeft, true);
    }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h,
                       bool vertical, int thumbStart, int thumbSize, bool, bool) override
    {
        g.setColour(design::bg);
        g.fillRect(x, y, w, h);
        g.setColour(design::yellowDim);
        if (vertical) g.fillRect(x + w / 2 - 1, y + thumbStart, 2, thumbSize);
        else g.fillRect(x + thumbStart, y + h / 2 - 1, thumbSize, 2);
    }
};
