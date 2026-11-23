#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>
#include "Fonts.h"

namespace theme
{
struct Palette
{
    const char* name;
    juce::uint32 background;
    juce::uint32 surface;
    juce::uint32 panel;
    juce::uint32 card;
    juce::uint32 cardHover;
    juce::uint32 raised;
    juce::uint32 line;
    juce::uint32 lineSoft;
    juce::uint32 text;
    juce::uint32 muted;
    juce::uint32 faint;
    juce::uint32 accent;
    juce::uint32 accentHover;
    juce::uint32 accentDark;
    juce::uint32 danger;
    juce::uint32 warning;
    juce::uint32 blue;
};

inline constexpr int themeCount = 4;

inline constexpr Palette kPalettes[themeCount] = {
    {
        "Obsidian Amber",
        0xff0d0d0f, 0xff131316, 0xff17171b, 0xff1c1c22, 0xff23232b, 0xff292932,
        0xff353540, 0xff24242c,
        0xfff3f0ea, 0xff9e9b93, 0xff68655e,
        0xfff07e3e, 0xffff9559, 0xff3a2012,
        0xfff25f5c, 0xffe8b85a, 0xff68a8f8
    },
    {
        "Carbon Iris",
        0xff0a0b0e, 0xff101218, 0xff151820, 0xff191d27, 0xff202532, 0xff262c3b,
        0xff323a4d, 0xff222836,
        0xffeff1f7, 0xff949cb0, 0xff5f677a,
        0xff7d8cff, 0xff9ca7ff, 0xff1c2147,
        0xffff6b72, 0xffe8b968, 0xff63b3ff
    },
    {
        "Tape Vermilion",
        0xff111112, 0xff171719, 0xff1c1c1e, 0xff222225, 0xff29292d, 0xff303035,
        0xff3d3d43, 0xff2c2c31,
        0xfff5f4f0, 0xffa3a19a, 0xff6e6c66,
        0xfff05444, 0xffff6f61, 0xff3d1815,
        0xffff5c5c, 0xffebb85e, 0xff7ab0f5
    },
    {
        "Phosphor Console",
        0xff0c0e0d, 0xff121514, 0xff161a18, 0xff1b201e, 0xff222825, 0xff272f2b,
        0xff333d38, 0xff232a26,
        0xffedf2ee, 0xff95a39b, 0xff606e66,
        0xff5ce08a, 0xff7eeaa4, 0xff143622,
        0xfff26b66, 0xffe8bd66, 0xff72b2f5
    }
};

inline int currentThemeIndex = 0;

// Pro-audio hardware surface system. Defaults to Obsidian Amber.
inline juce::Colour background  { 0xff0d0d0f };
inline juce::Colour surface     { 0xff131316 };
inline juce::Colour panel       { 0xff17171b };
inline juce::Colour card        { 0xff1c1c22 };
inline juce::Colour cardHover   { 0xff23232b };
inline juce::Colour raised      { 0xff292932 };
inline juce::Colour line        { 0xff353540 };
inline juce::Colour lineSoft    { 0xff24242c };
inline juce::Colour text        { 0xfff3f0ea };
inline juce::Colour muted       { 0xff9e9b93 };
inline juce::Colour faint       { 0xff68655e };
inline juce::Colour accent      { 0xfff07e3e };
inline juce::Colour accentHover { 0xffff9559 };
inline juce::Colour accentDark  { 0xff3a2012 };
inline juce::Colour danger      { 0xfff25f5c };
inline juce::Colour warning     { 0xffe8b85a };
inline juce::Colour blue        { 0xff68a8f8 };

inline const char* themeName(int idx)
{
    return kPalettes[juce::jlimit(0, themeCount - 1, idx)].name;
}

inline juce::File themeConfigFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("PatchLink")
               .getChildFile("ui_theme.txt");
}

inline void applyTheme(int idx, bool persist = true)
{
    currentThemeIndex = (idx >= 0 && idx < themeCount) ? idx : 0;
    const auto& p = kPalettes[currentThemeIndex];
    background  = juce::Colour(p.background);
    surface     = juce::Colour(p.surface);
    panel       = juce::Colour(p.panel);
    card        = juce::Colour(p.card);
    cardHover   = juce::Colour(p.cardHover);
    raised      = juce::Colour(p.raised);
    line        = juce::Colour(p.line);
    lineSoft    = juce::Colour(p.lineSoft);
    text        = juce::Colour(p.text);
    muted       = juce::Colour(p.muted);
    faint       = juce::Colour(p.faint);
    accent      = juce::Colour(p.accent);
    accentHover = juce::Colour(p.accentHover);
    accentDark  = juce::Colour(p.accentDark);
    danger      = juce::Colour(p.danger);
    warning     = juce::Colour(p.warning);
    blue        = juce::Colour(p.blue);

    if (persist)
    {
        const auto file = themeConfigFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::String(currentThemeIndex));
    }
}

inline void loadSavedTheme()
{
    const auto file = themeConfigFile();
    if (!file.existsAsFile())
        return;

    const auto raw = file.loadFileAsString().trim();
    if (raw.isEmpty())
        return;

    const auto idx = raw.getIntValue();
    if (idx >= 0 && idx < themeCount)
        applyTheme(idx, false);
}

constexpr float radius = 8.0f;
constexpr float smallRadius = 5.0f;
constexpr int headerHeight = 66;
constexpr int toolbarHeight = 62;
constexpr int footerHeight = 32;

inline juce::Font font(float size, bool semibold = false)
{
    auto f = PatchFonts::ui(size);
    return semibold ? f.withStyle(juce::Font::FontStyleFlags::bold) : f;
}

inline juce::Font mono(float size = 11.0f) { return PatchFonts::mono(size); }

inline void drawCheck(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness = 1.8f)
{
    juce::Path p;
    p.startNewSubPath(r.getX(), r.getCentreY());
    p.lineTo(r.getX() + r.getWidth() * 0.36f, r.getBottom());
    p.lineTo(r.getRight(), r.getY());
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        applyPalette();
    }

    void applyPalette()
    {
        setColour(juce::DocumentWindow::backgroundColourId, background);
        setColour(juce::DialogWindow::backgroundColourId, background);
        setColour(juce::AlertWindow::backgroundColourId, surface);
        setColour(juce::AlertWindow::textColourId, text);
        setColour(juce::AlertWindow::outlineColourId, line);
        setColour(juce::TextEditor::backgroundColourId, background);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.28f));
        setColour(juce::TextEditor::highlightedTextColourId, text);
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::focusedOutlineColourId, accent.withAlpha(0.75f));
        setColour(juce::CaretComponent::caretColourId, accent);
        setColour(juce::Label::textColourId, text);
        setColour(juce::Label::textWhenEditingColourId, text);
        setColour(juce::Label::backgroundWhenEditingColourId, background);
        setColour(juce::Label::outlineWhenEditingColourId, accent.withAlpha(0.65f));
        setColour(juce::ListBox::backgroundColourId, background);
        setColour(juce::ListBox::outlineColourId, lineSoft);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::headerTextColourId, muted);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, raised);
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::ScrollBar::thumbColourId, faint);
        setColour(juce::ScrollBar::trackColourId, background);
        setColour(juce::ProgressBar::backgroundColourId, lineSoft);
        setColour(juce::ProgressBar::foregroundColourId, accent);
        setColour(juce::TabbedComponent::backgroundColourId, background);
        setColour(juce::TabbedComponent::outlineColourId, lineSoft);
        setColour(juce::TabbedButtonBar::tabOutlineColourId, lineSoft);
        setColour(juce::TabbedButtonBar::tabTextColourId, muted);
        setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
        setColour(juce::TabbedButtonBar::frontTextColourId, text);
        setColour(juce::TooltipWindow::backgroundColourId, raised);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, line);
    }

    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(13.0f, true); }
    juce::Font getPopupMenuFont() override { return font(13.0f); }
    juce::Font getAlertWindowMessageFont() override { return font(14.0f); }
    juce::Font getAlertWindowTitleFont() override { return font(18.0f, true); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                              bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = base;
        if (down) c = c.darker(0.12f);
        else if (hover) c = c.brighter(0.10f);
        g.setColour(c);
        g.fillRoundedRectangle(r, smallRadius);
        g.setColour(b.findColour(juce::TextButton::buttonOnColourId).isTransparent() ? lineSoft : line);
        g.drawRoundedRectangle(r, smallRadius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        g.setFont(font(13.0f, true));
        g.setColour(b.findColour(down || b.getToggleState() ? juce::TextButton::textColourOnId
                                                           : juce::TextButton::textColourOffId));
        g.drawText(b.getButtonText(), b.getLocalBounds().reduced(10, 0), juce::Justification::centred, true);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto area = b.getLocalBounds();
        auto box = area.removeFromLeft(18).withSizeKeepingCentre(16, 16).toFloat();
        g.setColour(b.getToggleState() ? accent : (hover ? raised : card));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(b.getToggleState() ? accent : line);
        g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
        if (b.getToggleState())
            drawCheck(g, box.reduced(4.0f, 3.0f), background, 1.7f);
        g.setFont(font(12.5f));
        g.setColour(b.isEnabled() ? text : faint);
        g.drawText(b.getButtonText(), area.reduced(7, 0), juce::Justification::centredLeft, true);
    }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                       bool vertical, int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override
    {
        juce::ignoreUnused(mouseOver);
        g.setColour(mouseDown ? muted : faint.withAlpha(0.72f));
        if (vertical)
            g.fillRoundedRectangle((float)x + width * 0.5f - 2.0f, (float)y + thumbStart,
                                   4.0f, (float)thumbSize, 2.0f);
        else
            g.fillRoundedRectangle((float)x + thumbStart, (float)y + height * 0.5f - 2.0f,
                                   (float)thumbSize, 4.0f, 2.0f);
    }

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String&) override
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
        g.setColour(lineSoft);
        g.fillRoundedRectangle(r, (float)height * 0.5f);
        if (progress > 0.0)
        {
            g.setColour(accent);
            g.fillRoundedRectangle(r.withWidth((float)juce::jlimit(0.0, 1.0, progress) * r.getWidth()),
                                   (float)height * 0.5f);
        }
        g.setColour(muted);
        g.setFont(mono(10.0f));
        g.drawText(juce::String(juce::roundToInt(progress * 100.0)) + "%", bar.getLocalBounds(),
                   juce::Justification::centredRight);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& ed) override
    {
        auto r = juce::Rectangle<float>(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f);
        g.setColour(ed.hasKeyboardFocus(true) ? accent.withAlpha(0.65f) : line);
        g.drawRoundedRectangle(r, smallRadius, 1.0f);
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& ed) override
    {
        g.setColour(ed.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, smallRadius);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.setColour(panel);
        g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, radius);
        g.setColour(line);
        g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, radius, 1.0f);
    }

    juce::Rectangle<int> getTooltipBounds(const juce::String& tip, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override
    {
        return LookAndFeel_V4::getTooltipBounds(tip, screenPos, parentArea).expanded(4, 2);
    }
};
} // namespace theme
