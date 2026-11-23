#include "ChainView.h"
#include <cmath>
#include <map>

class ChainView::Slot final : public juce::Component,
                              private juce::Timer
{
public:
    Slot(Content& contentOwner, const PatchLinkProcessor::LoadedNode& node, int index);
    ~Slot() override { stopTimer(); }

    void setNodeAndIndex(const PatchLinkProcessor::LoadedNode& n, int index);

    PatchLinkProcessor::LoadedNode node;
    float visualY = 0.0f;
    bool hasVisualY = false;

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void refreshEnabled();

private:
    Content& owner;
    PlButton power { "", Icon::power, PlButton::Style::ghost };
    PlButton open { "Open", Icon::open, PlButton::Style::secondary };
    PlButton more { "", Icon::more, PlButton::Style::ghost };
    bool enabled = true;
    bool dragCandidate = false;
    bool dragging = false;
    float dragGrabOffsetY = 0.0f;
    float hoverAmount = 0.0f;
    float hoverTarget = 0.0f;
    int number = 0;

    void timerCallback() override;
};

class ChainView::Content final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit Content(ChainView& view) : owner(view) { addAndMakeVisible(addButton); }
    ~Content() override { stopTimer(); }

    void rebuild(const std::vector<PatchLinkProcessor::LoadedNode>& nodes)
    {
        bool sameSequence = ((int)nodes.size() == slots.size());
        if (sameSequence)
        {
            for (int i = 0; i < slots.size(); ++i)
            {
                if (slots[i]->node.id != nodes[(size_t)i].id)
                {
                    sameSequence = false;
                    break;
                }
            }
        }

        if (sameSequence)
        {
            for (int i = 0; i < slots.size(); ++i)
                slots[i]->setNodeAndIndex(nodes[(size_t)i], i);
            addButton.setVisible(!nodes.empty());
            layout();
            return;
        }

        std::map<uint32_t, float> prevY;
        for (auto* slot : slots)
            if (slot->hasVisualY)
                prevY[slot->node.id.uid] = slot->visualY;

        dragged = nullptr;
        slots.clear();
        int index = 0;
        for (const auto& node : nodes)
        {
            auto* slot = new Slot(*this, node, index++);
            if (const auto it = prevY.find(node.id.uid); it != prevY.end())
            {
                slot->visualY = it->second;
                slot->hasVisualY = true;
            }
            addAndMakeVisible(slot);
            slots.add(slot);
        }
        addButton.setVisible(!nodes.empty());
        layout();
    }

    int displayIndexFor(Slot* targetSlot) const
    {
        if (dragged == nullptr)
            return slots.indexOf(targetSlot);

        int pos = 0;
        for (auto* s : slots)
        {
            if (s == dragged) continue;
            if (s == targetSlot)
                return pos >= dropIndex ? pos + 1 : pos;
            ++pos;
        }
        return slots.indexOf(targetSlot);
    }

    void layout()
    {
        const int cardW = juce::jmax(220, getWidth() - cardX - 18);
        bool needsAnim = false;
        for (auto* slot : slots)
        {
            if (slot == dragged) continue;
            const float targetY = (float)(topSpace + displayIndexFor(slot) * (rowHeight + gap));
            if (!slot->hasVisualY)
            {
                slot->visualY = targetY;
                slot->hasVisualY = true;
            }
            else if (std::abs(slot->visualY - targetY) > 0.6f)
            {
                needsAnim = true;
            }
            else
            {
                slot->visualY = targetY;
            }
            slot->setBounds(cardX, juce::roundToInt(slot->visualY), cardW, rowHeight);
        }
        const int endY = topSpace + slots.size() * (rowHeight + gap);
        addButton.setBounds(cardX, endY + 2, cardW, 42);
        setSize(getWidth(), juce::jmax(owner.viewport.getHeight(), endY + bottomSpace));
        if (needsAnim) startTimerHz(60);
        repaint();
    }

    void resized() override { layout(); }

    void timerCallback() override
    {
        const int cardW = juce::jmax(220, getWidth() - cardX - 18);
        bool anyMoving = false;
        for (auto* slot : slots)
        {
            if (slot == dragged) continue;
            const float targetY = (float)(topSpace + displayIndexFor(slot) * (rowHeight + gap));
            const float delta = targetY - slot->visualY;
            if (std::abs(delta) <= 0.6f)
            {
                slot->visualY = targetY;
            }
            else
            {
                slot->visualY += delta * 0.32f;
                anyMoving = true;
            }
            slot->hasVisualY = true;
            slot->setBounds(cardX, juce::roundToInt(slot->visualY), cardW, rowHeight);
        }
        if (!anyMoving)
            stopTimer();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (slots.isEmpty()) return;
        g.setColour(theme::faint);
        g.setFont(theme::mono(9.0f));
        g.drawText("INPUT", 0, 8, cardX, 22, juce::Justification::centred);
        g.setColour(theme::accent.withAlpha(0.30f));
        const auto railX = (float)cardX * 0.5f;
        g.drawLine(railX, 34.0f, railX, (float)addButton.getY() + 8, 1.0f);
        for (auto* slot : slots)
        {
            if (slot == dragged) continue;
            const auto cy = (float)slot->getBounds().getCentreY();
            g.setColour(theme::background);
            g.fillEllipse(railX - 5, cy - 5, 10, 10);
            g.setColour(slot->node.bypass ? theme::faint : theme::accent);
            g.fillEllipse(railX - 3, cy - 3, 6, 6);
        }
        if (dragged != nullptr)
        {
            const float cardW = (float)juce::jmax(220, getWidth() - cardX - 18);
            const float slotY = (float)(topSpace + dropIndex * (rowHeight + gap));
            const juce::Rectangle<float> placeholder((float)cardX, slotY, cardW, (float)rowHeight);
            g.setColour(theme::accent.withAlpha(0.08f));
            g.fillRoundedRectangle(placeholder, theme::radius);
            g.setColour(theme::accent.withAlpha(0.55f));
            g.drawRoundedRectangle(placeholder.reduced(0.5f), theme::radius, 1.5f);
        }
        g.setColour(theme::faint);
        g.setFont(theme::mono(9.0f));
        g.drawText("OUTPUT", 0, addButton.getBottom() + 8, cardX, 22, juce::Justification::centred);
    }

    void dragStarted(Slot* slot, float grabOffsetY)
    {
        dragged = slot;
        dragGrabOffsetY = grabOffsetY;
        dragged->toFront(false);
        dropIndex = slots.indexOf(slot);
        dragged->repaint();
        repaint();
    }

    void dragMoved(const juce::MouseEvent& e)
    {
        if (dragged == nullptr) return;
        const auto p = e.getEventRelativeTo(this).position;
        const int maxY = juce::jmax(topSpace, topSpace + (slots.size() - 1) * (rowHeight + gap) + 16);
        const int newY = juce::jlimit(topSpace - 16, maxY, juce::roundToInt(p.y - dragGrabOffsetY));
        dragged->setTopLeftPosition(cardX, newY);
        const float centerY = (float)dragged->getBounds().getCentreY();
        const int nextDrop = juce::jlimit(0, slots.size() - 1,
                                          juce::roundToInt((centerY - (float)topSpace - (float)rowHeight * 0.5f)
                                                           / (float)(rowHeight + gap)));
        if (nextDrop != dropIndex)
        {
            dropIndex = nextDrop;
            startTimerHz(60);
        }
        repaint();
    }

    void dragFinished()
    {
        if (dragged == nullptr) return;
        auto* slot = dragged;
        const auto target = dropIndex;
        slot->visualY = (float)slot->getY();
        slot->hasVisualY = true;
        dragged = nullptr;
        slot->repaint();
        if (owner.onReorder && slots.indexOf(slot) != target)
            owner.onReorder(slot->node.id, target);
        else
            layout();
    }

    ChainView& owner;
    juce::OwnedArray<Slot> slots;
    PlButton addButton { "Add next plugin", Icon::plus, PlButton::Style::secondary };
    Slot* dragged = nullptr;
    float dragGrabOffsetY = 0.0f;
    int dropIndex = 0;

    static constexpr int cardX = 48;
    static constexpr int rowHeight = 92;
    static constexpr int gap = 20;
    static constexpr int topSpace = 42;
    static constexpr int bottomSpace = 72;
};

ChainView::Slot::Slot(Content& contentOwner, const PatchLinkProcessor::LoadedNode& n, int index)
    : owner(contentOwner), node(n), enabled(!n.bypass), number(index + 1)
{
    power.setTooltip(n.bypass ? "Enable plugin" : "Bypass plugin");
    power.setSelected(enabled);
    power.onClick = [this] {
        enabled = !enabled;
        node.bypass = !enabled;
        refreshEnabled();
        owner.repaint();
        if (owner.owner.onBypass) owner.owner.onBypass(node.id, !enabled);
    };
    open.setTooltip("Open the plugin interface (double-click the card)");
    open.onClick = [this] { if (owner.owner.onOpen) owner.owner.onOpen(node.id); };
    more.setTooltip("Plugin actions");
    more.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Open interface");
        menu.addItem(2, enabled ? "Bypass" : "Enable");
        const auto index = owner.slots.indexOf(this);
        menu.addSeparator();
        menu.addItem(3, "Move up", index > 0);
        menu.addItem(4, "Move down", index < owner.slots.size() - 1);
        menu.addSeparator();
        menu.addItem(5, "Remove from chain");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more), [this](int result) {
            switch (result)
            {
                case 1: if (owner.owner.onOpen) owner.owner.onOpen(node.id); break;
                case 2: power.triggerClick(); break;
                case 3: if (owner.owner.onReorder) owner.owner.onReorder(node.id, owner.slots.indexOf(this) - 1); break;
                case 4: if (owner.owner.onReorder) owner.owner.onReorder(node.id, owner.slots.indexOf(this) + 1); break;
                case 5: if (owner.owner.onRemove) owner.owner.onRemove(node.id); break;
                default: break;
            }
        });
    };
    addAndMakeVisible(power);
    addAndMakeVisible(open);
    addAndMakeVisible(more);
}

void ChainView::Slot::setNodeAndIndex(const PatchLinkProcessor::LoadedNode& n, int index)
{
    node = n;
    enabled = !n.bypass;
    number = index + 1;
    refreshEnabled();
}

void ChainView::Slot::refreshEnabled()
{
    power.setSelected(enabled);
    power.setTooltip(enabled ? "Bypass plugin" : "Enable plugin");
    repaint();
}

void ChainView::Slot::mouseEnter(const juce::MouseEvent&)
{
    hoverTarget = 1.0f;
    startTimerHz(60);
}

void ChainView::Slot::mouseExit(const juce::MouseEvent&)
{
    if (!isMouseOver(true))
    {
        hoverTarget = 0.0f;
        startTimerHz(60);
    }
}

void ChainView::Slot::timerCallback()
{
    if (hoverTarget > 0.5f && !isMouseOver(true) && !dragging)
        hoverTarget = 0.0f;
    const float delta = hoverTarget - hoverAmount;
    hoverAmount += delta * 0.28f;
    if (std::abs(delta) < 0.015f)
    {
        hoverAmount = hoverTarget;
        stopTimer();
    }
    repaint();
}

void ChainView::Slot::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float amount = dragging ? 1.0f : hoverAmount;
    const auto bg = theme::card.interpolatedWith(theme::cardHover, amount);
    const auto border = dragging ? theme::accent.withAlpha(0.85f)
                                 : theme::lineSoft.interpolatedWith(theme::line, amount);
    g.setColour(bg);
    g.fillRoundedRectangle(r, theme::radius);
    g.setColour(border);
    g.drawRoundedRectangle(r, theme::radius, dragging ? 1.5f : 1.0f);

    g.setColour(enabled ? theme::accent.withAlpha(0.75f + 0.25f * amount)
                        : theme::faint.withAlpha(0.40f));
    g.fillRoundedRectangle(r.getX() + 1.5f, r.getY() + 12.0f, 3.0f, r.getHeight() - 24.0f, 1.5f);

    if (!enabled)
    {
        g.setColour(theme::background.withAlpha(0.28f));
        g.fillRoundedRectangle(r, theme::radius);
    }

    g.setColour(theme::faint.interpolatedWith(theme::muted, amount * 0.6f));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse(14.0f + col * 5, 36.0f + row * 7, 2.0f, 2.0f);

    g.setColour(enabled ? theme::accent : theme::faint);
    g.setFont(theme::mono(10.5f));
    g.drawText(juce::String::formatted("%02d", number), 32, 0, 30, getHeight(), juce::Justification::centred);

    auto text = getLocalBounds().reduced(72, 0);
    text.removeFromRight(172);
    g.setColour(enabled ? theme::text : theme::muted);
    g.setFont(theme::font(15.0f, true));
    const auto name = node.desc.name.isNotEmpty() ? node.desc.name : "Plugin " + juce::String(node.id.uid);
    g.drawText(name, text.removeFromTop(52), juce::Justification::centredLeft, true);
    g.setColour(theme::muted);
    g.setFont(theme::font(11.5f));
    auto detail = node.desc.manufacturerName.isNotEmpty() ? node.desc.manufacturerName : "Unknown developer";
    detail += "   /   " + node.desc.pluginFormatName;
    if (node.desc.version.isNotEmpty()) detail += "   /   " + node.desc.version;
    if (!enabled) detail += "   /   BYPASSED";
    g.drawText(detail, text.removeFromTop(25), juce::Justification::centredLeft, true);
}

void ChainView::Slot::resized()
{
    auto r = getLocalBounds().reduced(12, 0);
    more.setBounds(r.removeFromRight(34).withSizeKeepingCentre(34, 34));
    r.removeFromRight(4);
    power.setBounds(r.removeFromRight(34).withSizeKeepingCentre(34, 34));
    r.removeFromRight(12);
    open.setBounds(r.removeFromRight(82).withSizeKeepingCentre(82, 34));
}

void ChainView::Slot::mouseDown(const juce::MouseEvent& e)
{
    dragCandidate = e.mods.isLeftButtonDown();
    dragging = false;
    dragGrabOffsetY = e.getEventRelativeTo(&owner).position.y - (float)getBounds().getY();
}

void ChainView::Slot::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragCandidate) return;
    if (!dragging)
    {
        if (e.getDistanceFromDragStart() < 5)
            return;
        dragging = true;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        owner.dragStarted(this, dragGrabOffsetY);
    }
    owner.dragMoved(e);
}

void ChainView::Slot::mouseUp(const juce::MouseEvent&)
{
    if (dragging)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        owner.dragFinished();
    }
    dragCandidate = false;
    dragging = false;
}

void ChainView::Slot::mouseDoubleClick(const juce::MouseEvent&)
{
    if (!dragging && owner.owner.onOpen)
        owner.owner.onOpen(node.id);
}

ChainView::ChainView()
{
    contentOwner = std::make_unique<Content>(*this);
    content = contentOwner.get();
    viewport.setViewedComponent(content, false);
    viewport.setScrollBarsShown(true, false, true, false);
    viewport.setScrollBarThickness(10);
    addAndMakeVisible(viewport);
    content->addButton.onClick = [this] { if (onAdd) onAdd(); };

    emptyTitle.setText("Your chain starts here", juce::dontSendNotification);
    emptyTitle.setFont(theme::font(24.0f, true));
    emptyTitle.setJustificationType(juce::Justification::centred);
    emptySubtitle.setText("Add an effect, open its interface, and shape your sound.\nYour plugins and settings stay together in one portable chain.",
                          juce::dontSendNotification);
    emptySubtitle.setFont(theme::font(13.0f));
    emptySubtitle.setColour(juce::Label::textColourId, theme::muted);
    emptySubtitle.setJustificationType(juce::Justification::centred);
    emptyAdd.onClick = [this] { if (onAdd) onAdd(); };
    addAndMakeVisible(emptyTitle);
    addAndMakeVisible(emptySubtitle);
    addAndMakeVisible(emptyAdd);

    hint.setText("Drag anywhere on a card to reorder  /  Double-click to edit", juce::dontSendNotification);
    hint.setFont(theme::font(11.0f));
    hint.setColour(juce::Label::textColourId, theme::faint);
    addAndMakeVisible(hint);
    clearButton.onClick = [this] { if (onClear) onClear(); };
    addAndMakeVisible(clearButton);
}

ChainView::~ChainView() = default;

void ChainView::applyTheme()
{
    emptyTitle.setColour(juce::Label::textColourId, theme::text);
    emptySubtitle.setColour(juce::Label::textColourId, theme::muted);
    hint.setColour(juce::Label::textColourId, theme::faint);
    repaint();
    content->repaint();
}

void ChainView::refresh(const std::vector<PatchLinkProcessor::LoadedNode>& nodes)
{
    empty = nodes.empty();
    content->rebuild(nodes);
    emptyTitle.setVisible(empty);
    emptySubtitle.setVisible(empty);
    emptyAdd.setVisible(empty);
    hint.setVisible(!empty);
    clearButton.setVisible(!empty);
    resized();
}

void ChainView::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom(44);
    hint.setBounds(bottom.reduced(18, 0));
    clearButton.setBounds(bottom.removeFromRight(128).reduced(6, 5));
    viewport.setBounds(r);
    content->setSize(juce::jmax(300, viewport.getWidth() - 12), viewport.getHeight());
    content->layout();

    auto centre = r.getCentre();
    emptyTitle.setBounds(r.getX() + 20, centre.y - 102, r.getWidth() - 40, 36);
    emptySubtitle.setBounds(r.getX() + 20, centre.y - 56, r.getWidth() - 40, 54);
    emptyAdd.setBounds(centre.x - 102, centre.y + 20, 204, 40);
}
