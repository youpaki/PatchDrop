#pragma once

#include <JuceHeader.h>
#include "../Core/Theme.h"

enum class Icon
{
    none, plus, link, grid, list, library, scan, share, code, importFile, exportFile,
    open, power, more, close, trash, arrowUp, arrowDown, search, copy, check,
    folder, chevronRight, warning
};

void drawIcon(juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour, float stroke = 1.6f);

class PlButton final : public juce::Button,
                       private juce::Timer
{
public:
    enum class Style { secondary, primary, ghost, danger };

    explicit PlButton(juce::String text = {}, Icon = Icon::none, Style = Style::secondary);

    void setStyle(Style);
    void setSelected(bool);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void()> onDragStart;

private:
    Style style;
    Icon icon;
    float hover = 0.0f;
    float hoverTarget = 0.0f;
    bool dragTriggered = false;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlButton)
};

class Toast final : public juce::Component,
                    private juce::Timer
{
public:
    Toast();
    void show(const juce::String&, bool isError = false);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::String message;
    bool error = false;
    float opacity = 0.0f;
    int elapsed = 0;
    void timerCallback() override;
};

class ModalOverlay : public juce::Component,
                     private juce::Timer
{
public:
    ModalOverlay();
    ~ModalOverlay() override;

    std::function<void()> onDismiss;
    void paint(juce::Graphics&) override;
    void resized() override;
    virtual juce::Rectangle<int> getCardBounds() const;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

protected:
    float reveal = 0.0f;
    virtual void layoutCard(juce::Rectangle<int>) = 0;
    void timerCallback() override;
};
