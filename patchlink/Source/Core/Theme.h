#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>
#include "Fonts.h"

namespace theme
{
// A quiet, neutral surface system. Accent is reserved for signal and primary actions.
inline const juce::Colour background  { 0xff101418 };
inline const juce::Colour surface     { 0xff161b20 };
inline const juce::Colour panel       { 0xff1a2026 };
inline const juce::Colour card        { 0xff1d242b };
inline const juce::Colour cardHover   { 0xff232c34 };
inline const juce::Colour raised      { 0xff27313a };
inline const juce::Colour line        { 0xff303a44 };
inline const juce::Colour lineSoft    { 0xff252d35 };
inline const juce::Colour text        { 0xffedf2f4 };
inline const juce::Colour muted       { 0xff98a5af };
inline const juce::Colour faint       { 0xff65737f };
inline const juce::Colour accent      { 0xff79ddbd };
inline const juce::Colour accentHover { 0xff9ae8d0 };
inline const juce::Colour accentDark  { 0xff173c33 };
inline const juce::Colour danger      { 0xffff837c };
inline const juce::Colour warning     { 0xffe6b96f };
inline const juce::Colour blue        { 0xff83aff5 };

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
