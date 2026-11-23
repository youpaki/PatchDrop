#include "Controls.h"

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour colour, float stroke)
{
    if (icon == Icon::none || r.isEmpty()) return;

    g.setColour(colour);
    const auto x = r.getX();
    const auto y = r.getY();
    const auto w = r.getWidth();
    const auto h = r.getHeight();
    const auto cx = r.getCentreX();
    const auto cy = r.getCentreY();
    const auto right = r.getRight();
    const auto bottom = r.getBottom();
    juce::Path p;
    juce::PathStrokeType line(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    switch (icon)
    {
        case Icon::plus:
            p.startNewSubPath(cx, y + 2); p.lineTo(cx, bottom - 2);
            p.startNewSubPath(x + 2, cy); p.lineTo(right - 2, cy);
            g.strokePath(p, line);
            break;
        case Icon::link:
        {
            auto a = juce::Rectangle<float>(x + 1, cy - 4, w * 0.58f, 8);
            auto b = juce::Rectangle<float>(right - w * 0.58f - 1, cy - 4, w * 0.58f, 8);
            g.drawRoundedRectangle(a, 4.0f, stroke);
            g.drawRoundedRectangle(b, 4.0f, stroke);
            g.drawLine(cx - 3, cy, cx + 3, cy, stroke);
            break;
        }
        case Icon::grid:
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 2; ++col)
                    g.drawRoundedRectangle(x + 2 + col * (w * 0.5f), y + 2 + row * (h * 0.5f),
                                           w * 0.34f, h * 0.34f, 1.5f, stroke);
            break;
        case Icon::list:
            for (int row = 0; row < 3; ++row)
            {
                const auto yy = y + 3 + row * (h - 6) * 0.5f;
                g.fillEllipse(x + 1, yy - 1, 2, 2);
                g.drawLine(x + 6, yy, right - 1, yy, stroke);
            }
            break;
        case Icon::library:
            g.drawRoundedRectangle(x + 2, y + 2, w - 4, h - 4, 2.0f, stroke);
            g.drawLine(x + 5, y + 2, x + 5, bottom - 2, stroke);
            g.drawLine(x + 9, y + 6, right - 5, y + 6, stroke);
            g.drawLine(x + 9, y + 10, right - 5, y + 10, stroke);
            break;
        case Icon::scan:
        case Icon::search:
            g.drawEllipse(x + 1, y + 1, w * 0.62f, h * 0.62f, stroke);
            g.drawLine(x + w * 0.56f, y + h * 0.56f, right - 1, bottom - 1, stroke + 0.2f);
            if (icon == Icon::scan)
            {
                g.drawLine(x + 4, cy - 1, x + w * 0.55f, cy - 1, stroke);
                g.drawLine(x + w * 0.33f, y + 4, x + w * 0.33f, y + h * 0.55f, stroke);
            }
            break;
        case Icon::share:
            g.drawLine(x + 4, cy, right - 4, y + 3, stroke);
            g.drawLine(x + 4, cy, right - 4, bottom - 3, stroke);
            g.fillEllipse(x + 1, cy - 3, 6, 6);
            g.fillEllipse(right - 6, y, 6, 6);
            g.fillEllipse(right - 6, bottom - 6, 6, 6);
            break;
        case Icon::code:
            p.startNewSubPath(x + w * 0.36f, y + 2); p.lineTo(x + 2, cy); p.lineTo(x + w * 0.36f, bottom - 2);
            p.startNewSubPath(right - w * 0.36f, y + 2); p.lineTo(right - 2, cy); p.lineTo(right - w * 0.36f, bottom - 2);
            g.strokePath(p, line);
            break;
        case Icon::importFile:
        case Icon::exportFile:
        {
            const bool in = icon == Icon::importFile;
            const auto arrowY = in ? y + 7 : y + 2;
            p.startNewSubPath(cx, y + 1); p.lineTo(cx, bottom - 7);
            p.startNewSubPath(cx - 4, arrowY + (in ? 0 : 5)); p.lineTo(cx, arrowY + (in ? 4 : 0)); p.lineTo(cx + 4, arrowY + (in ? 0 : 5));
            g.strokePath(p, line);
            p.clear();
            p.startNewSubPath(x + 2, bottom - 6); p.lineTo(x + 2, bottom - 2); p.lineTo(right - 2, bottom - 2); p.lineTo(right - 2, bottom - 6);
            g.strokePath(p, line);
            break;
        }
        case Icon::open:
            for (int row = 0; row < 3; ++row)
            {
                const auto yy = y + 3 + row * (h - 6) * 0.5f;
                const auto knobX = row == 1 ? x + w * 0.65f : (row == 0 ? x + w * 0.38f : x + w * 0.52f);
                g.drawLine(x + 1, yy, right - 1, yy, stroke);
                g.fillRoundedRectangle(knobX - 2, yy - 2.5f, 4, 5, 1.0f);
            }
            break;
        case Icon::power:
            p.addArc(x + 2, y + 3, w - 4, h - 4, 0.72f, juce::MathConstants<float>::twoPi - 0.72f, true);
            p.startNewSubPath(cx, y + 1); p.lineTo(cx, cy + 1);
            g.strokePath(p, line);
            break;
        case Icon::more:
            for (int i = 0; i < 3; ++i) g.fillEllipse(x + 2 + i * (w - 4) * 0.5f - 1.5f, cy - 1.5f, 3, 3);
            break;
        case Icon::close:
            g.drawLine(x + 3, y + 3, right - 3, bottom - 3, stroke);
            g.drawLine(right - 3, y + 3, x + 3, bottom - 3, stroke);
            break;
        case Icon::trash:
            g.drawRoundedRectangle(x + 4, y + 5, w - 8, h - 7, 1.5f, stroke);
            g.drawLine(x + 2, y + 4, right - 2, y + 4, stroke);
            g.drawLine(cx - 3, y + 2, cx + 3, y + 2, stroke);
            g.drawLine(cx - 2, y + 8, cx - 2, bottom - 4, stroke);
            g.drawLine(cx + 2, y + 8, cx + 2, bottom - 4, stroke);
            break;
        case Icon::arrowUp:
        case Icon::arrowDown:
        {
            const float sign = icon == Icon::arrowUp ? 1.0f : -1.0f;
            p.startNewSubPath(x + 3, cy - sign * 3); p.lineTo(cx, cy + sign * 3); p.lineTo(right - 3, cy - sign * 3);
            g.strokePath(p, line);
            break;
        }
        case Icon::copy:
            g.drawRoundedRectangle(x + 5, y + 1, w - 7, h - 7, 2.0f, stroke);
            p.startNewSubPath(x + 3, y + 4); p.lineTo(x + 3, bottom - 2); p.lineTo(right - 6, bottom - 2);
            g.strokePath(p, line);
            break;
        case Icon::check:
            theme::drawCheck(g, r.reduced(3, 4), colour, stroke);
            break;
        case Icon::folder:
            p.startNewSubPath(x + 1, y + 4); p.lineTo(x + w * 0.42f, y + 4); p.lineTo(x + w * 0.52f, y + 7);
            p.lineTo(right - 1, y + 7); p.lineTo(right - 1, bottom - 2); p.lineTo(x + 1, bottom - 2); p.closeSubPath();
            g.strokePath(p, line);
            break;
        case Icon::chevronRight:
            p.startNewSubPath(cx - 2, y + 3); p.lineTo(cx + 2, cy); p.lineTo(cx - 2, bottom - 3);
            g.strokePath(p, line);
            break;
        case Icon::warning:
            p.startNewSubPath(cx, y + 1); p.lineTo(right - 1, bottom - 2); p.lineTo(x + 1, bottom - 2); p.closeSubPath();
            g.strokePath(p, line);
            g.drawLine(cx, y + 6, cx, cy + 2, stroke);
            g.fillEllipse(cx - 1, bottom - 6, 2, 2);
            break;
        case Icon::none: break;
    }
}

PlButton::PlButton(juce::String text, Icon iconToUse, Style initialStyle)
    : juce::Button(text), style(initialStyle), icon(iconToUse)
{
    setButtonText(text);
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PlButton::setStyle(Style newStyle)
{
    style = newStyle;
    repaint();
}

void PlButton::setSelected(bool selected)
{
    setToggleState(selected, juce::dontSendNotification);
    repaint();
}

void PlButton::mouseEnter(const juce::MouseEvent& e)
{
    juce::Button::mouseEnter(e);
    hoverTarget = isEnabled() ? 1.0f : 0.0f;
    startTimerHz(60);
}

void PlButton::mouseExit(const juce::MouseEvent& e)
{
    juce::Button::mouseExit(e);
    hoverTarget = 0.0f;
    startTimerHz(60);
}

void PlButton::timerCallback()
{
    const auto delta = hoverTarget - hover;
    hover += delta * 0.28f;
    if (std::abs(delta) < 0.015f)
    {
        hover = hoverTarget;
        stopTimer();
    }
    repaint();
}

void PlButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    if (highlighted && hoverTarget < 0.5f) { hoverTarget = 1.0f; startTimerHz(60); }
    const auto amount = juce::jmax(hover, highlighted ? 0.35f : 0.0f);
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const auto selected = getToggleState();
    juce::Colour fg = theme::text;
    juce::Colour bg = theme::card;
    juce::Colour border = theme::lineSoft;

    if (style == Style::primary)
    {
        bg = theme::accent.interpolatedWith(theme::accentHover, amount);
        fg = theme::background;
        border = bg;
    }
    else if (style == Style::ghost)
    {
        bg = selected ? theme::accent.withAlpha(0.13f) : theme::card.withAlpha(amount * 0.65f);
        border = selected ? theme::accent.withAlpha(0.30f) : juce::Colours::transparentBlack;
        fg = selected ? theme::accent : theme::muted.interpolatedWith(theme::text, amount);
    }
    else if (style == Style::danger)
    {
        bg = theme::danger.withAlpha(0.08f + amount * 0.08f);
        border = theme::danger.withAlpha(0.35f);
        fg = theme::danger;
    }
    else
    {
        bg = theme::card.interpolatedWith(theme::raised, amount);
        border = theme::lineSoft.interpolatedWith(theme::line, amount);
        fg = theme::muted.interpolatedWith(theme::text, amount);
    }

    if (down) bg = bg.darker(0.10f);
    if (!isEnabled()) { bg = bg.withMultipliedAlpha(0.45f); fg = fg.withAlpha(0.40f); border = border.withAlpha(0.4f); }

    g.setColour(bg);
    g.fillRoundedRectangle(r, theme::smallRadius);
    g.setColour(border);
    g.drawRoundedRectangle(r, theme::smallRadius, 1.0f);

    const auto buttonFont = theme::font(12.5f, true);
    const bool hasText = getButtonText().isNotEmpty();
    const int iconSize = hasText ? 16 : 18;
    if (icon != Icon::none && hasText)
    {
        const int gap = 7;
        const int textWidth = juce::jmin(juce::jmax(0, getWidth() - 20 - iconSize - gap),
                                         juce::roundToInt(std::ceil(buttonFont.getStringWidthFloat(getButtonText()))));
        const int totalWidth = iconSize + gap + textWidth;
        auto content = getLocalBounds().withSizeKeepingCentre(totalWidth, getHeight());
        auto ir = content.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize).toFloat();
        drawIcon(g, icon, ir, fg);
        content.removeFromLeft(gap);
        g.setColour(fg);
        g.setFont(buttonFont);
        g.drawText(getButtonText(), content, juce::Justification::centredLeft, true);
    }
    else if (icon != Icon::none)
    {
        drawIcon(g, icon, getLocalBounds().toFloat().withSizeKeepingCentre((float)iconSize, (float)iconSize), fg);
    }
    else
    {
        g.setColour(fg);
        g.setFont(buttonFont);
        g.drawText(getButtonText(), getLocalBounds().reduced(10, 0), juce::Justification::centred, true);
    }

    if (hasKeyboardFocus(false))
    {
        g.setColour(theme::accent.withAlpha(0.65f));
        g.drawRoundedRectangle(r.expanded(2.0f), theme::smallRadius + 2.0f, 1.0f);
    }
}

Toast::Toast()
{
    setVisible(false);
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void Toast::show(const juce::String& text, bool isError)
{
    message = text;
    error = isError;
    elapsed = 0;
    opacity = 0.0f;
    setVisible(true);
    startTimerHz(60);
}

void Toast::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::raised.withAlpha(opacity));
    g.fillRoundedRectangle(r, theme::radius);
    g.setColour((error ? theme::danger : theme::accent).withAlpha(opacity * 0.55f));
    g.drawRoundedRectangle(r, theme::radius, 1.0f);
    g.fillRoundedRectangle(r.getX() + 12, r.getCentreY() - 2, 4, 4, 2.0f);
    g.setColour(theme::text.withAlpha(opacity));
    g.setFont(theme::font(12.5f));
    g.drawText(message, getLocalBounds().reduced(26, 0), juce::Justification::centredLeft, true);
}

void Toast::mouseDown(const juce::MouseEvent&)
{
    elapsed = 3000;
}

void Toast::timerCallback()
{
    elapsed += 16;
    const auto target = elapsed < 2700 ? 1.0f : 0.0f;
    opacity += (target - opacity) * (target > opacity ? 0.20f : 0.12f);
    if (elapsed >= 3300 && opacity < 0.02f)
    {
        stopTimer();
        setVisible(false);
        return;
    }
    repaint();
}

ModalOverlay::ModalOverlay()
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
    startTimerHz(60);
}

ModalOverlay::~ModalOverlay() { stopTimer(); }

void ModalOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.56f * reveal));
    const auto card = getCardBounds().toFloat();
    for (int i = 1; i <= 4; ++i)
    {
        const auto spread = (float)i * 3.5f;
        g.setColour(juce::Colours::black.withAlpha(0.11f * reveal));
        g.fillRoundedRectangle(card.expanded(spread, spread).translated(0.0f, (float)i * 2.0f),
                               12.0f + spread * 0.4f);
    }
    g.setColour(theme::surface.withAlpha(reveal));
    g.fillRoundedRectangle(card, 12.0f);
    g.setColour(theme::line.withAlpha(reveal));
    g.drawRoundedRectangle(card.reduced(0.5f), 12.0f, 1.0f);
}

juce::Rectangle<int> ModalOverlay::getCardBounds() const
{
    const int w = juce::jmin(720, getWidth() - 48);
    const int h = juce::jmin(620, getHeight() - 48);
    return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
}

void ModalOverlay::resized() { layoutCard(getCardBounds()); }

void ModalOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (!getCardBounds().contains(e.getPosition()) && onDismiss) onDismiss();
}

bool ModalOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onDismiss) onDismiss();
        return true;
    }
    return false;
}

void ModalOverlay::timerCallback()
{
    reveal += (1.0f - reveal) * 0.24f;
    if (reveal > 0.995f) { reveal = 1.0f; stopTimer(); }
    repaint();
}
