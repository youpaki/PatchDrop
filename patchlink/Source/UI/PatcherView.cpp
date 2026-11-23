#include "PatcherView.h"

class PatcherView::Content final : public juce::Component
{
public:
    explicit Content(PatcherView& view) : owner(view)
    {
        setSize(workspaceWidth, workspaceHeight);
        setWantsKeyboardFocus(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::background);
        g.setColour(theme::lineSoft.withAlpha(0.42f));
        const auto clip = g.getClipBounds().expanded(gridSize);
        const int startX = juce::jmax(0, (clip.getX() / gridSize) * gridSize);
        const int endX   = juce::jmin(getWidth(), clip.getRight());
        const int startY = juce::jmax(0, (clip.getY() / gridSize) * gridSize);
        const int endY   = juce::jmin(getHeight(), clip.getBottom());
        for (int x = startX; x <= endX; x += gridSize)
            for (int y = startY; y <= endY; y += gridSize)
                g.fillEllipse((float)x - 0.65f, (float)y - 0.65f, 1.3f, 1.3f);

        for (const auto& wire : wires)
        {
            if (wire.srcCh != 0) continue;
            const auto a = boxFor(wire.src);
            const auto b = boxFor(wire.dst);
            if (a.isEmpty() || b.isEmpty()) continue;
            const bool wireHovered = (hoveredWire.src == wire.src && hoveredWire.dst == wire.dst && hoveredWire.src != 0);
            auto path = cablePath(outputPort(a), inputPort(b));
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.strokePath(path, juce::PathStrokeType(wireHovered ? 6.0f : 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(wireHovered ? theme::accentHover : theme::accent.withAlpha(0.75f));
            g.strokePath(path, juce::PathStrokeType(wireHovered ? 2.8f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (cableSource.uid != 0)
        {
            auto path = cablePath(cableStart, cableEnd);
            juce::Path dashed;
            const float dashes[] = { 6.0f, 5.0f };
            juce::PathStrokeType(2.0f).createDashedStroke(dashed, path, dashes, 2);
            g.setColour(theme::accentHover);
            g.fillPath(dashed);
        }

        drawEndpoint(g, true, boxFor(inputId));
        drawEndpoint(g, false, boxFor(outputId));
        for (const auto& node : nodes) drawNode(g, node);

        if (nodes.empty())
        {
            auto r = juce::Rectangle<int>(330, 240, 500, 120);
            g.setColour(theme::muted);
            g.setFont(theme::font(20.0f, true));
            g.drawText("A blank canvas for your signal", r.removeFromTop(38), juce::Justification::centred);
            g.setColour(theme::faint);
            g.setFont(theme::font(13.0f));
            g.drawText("Double-click anywhere to add a plugin.\nDrag from a right port to a left port to connect.",
                       r, juce::Justification::centredTop);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto point = e.position;

        if (e.mods.isMiddleButtonDown())
        {
            panning = true;
            panStartMouseScreen = e.getScreenPosition();
            panStartViewPos = owner.viewport.getViewPosition();
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            return;
        }

        const auto source = portAt(point, true);
        if (source.uid != 0 && e.mods.isLeftButtonDown())
        {
            cableSource = source;
            cableStart = outputPort(boxFor(source));
            cableEnd = point;
            repaint();
            return;
        }

        const auto hit = nodeAt(point);
        if (hit.uid != 0)
        {
            selected = hit;
            if (e.mods.isRightButtonDown()) { showNodeMenu(hit); return; }
            if (hit != inputId && hit != outputId && powerRect(boxFor(hit)).contains(point))
            {
                const auto* node = findNode(hit);
                if (node != nullptr && owner.onBypass) owner.onBypass(hit, !node->bypass);
                return;
            }
            if (hit != inputId && hit != outputId)
            {
                dragId = hit;
                dragOffset = point - boxFor(hit).getPosition().toFloat();
            }
            repaint();
            return;
        }

        if (e.mods.isRightButtonDown())
        {
            const auto wire = wireAt(point);
            if (wire.src != 0)
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Delete connection");
                menu.showMenuAsync(juce::PopupMenu::Options(), [this, wire](int result) {
                    if (result == 1 && owner.onDisconnect)
                        owner.onDisconnect(juce::AudioProcessorGraph::NodeID(wire.src),
                                           juce::AudioProcessorGraph::NodeID(wire.dst));
                });
                return;
            }
        }
        selected = juce::AudioProcessorGraph::NodeID(0);
        if (e.mods.isLeftButtonDown())
        {
            panning = true;
            panStartMouseScreen = e.getScreenPosition();
            panStartViewPos = owner.viewport.getViewPosition();
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (panning)
        {
            const auto delta = e.getScreenPosition() - panStartMouseScreen;
            owner.viewport.setViewPosition(panStartViewPos - delta);
            return;
        }
        if (cableSource.uid != 0)
        {
            cableEnd = e.position;
            updateHover(e.position);
            repaint();
            return;
        }
        if (dragId.uid == 0 || dragId == inputId || dragId == outputId) return;
        if (auto* node = findNode(dragId))
        {
            const auto p = e.position - dragOffset;
            node->x = juce::jlimit(12, getWidth() - nodeWidth - 12, juce::roundToInt(p.x));
            node->y = juce::jlimit(12, getHeight() - nodeHeight - 12, juce::roundToInt(p.y));
            if (owner.onMove) owner.onMove(dragId, node->x, node->y);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (panning)
        {
            panning = false;
            updateHover(e.position);
        }
        if (cableSource.uid != 0)
        {
            const auto destination = portAt(e.position, false);
            if (destination.uid != 0 && destination != cableSource && owner.onConnect)
                owner.onConnect(cableSource, destination);
            cableSource = juce::AudioProcessorGraph::NodeID(0);
            updateHover(e.position);
            repaint();
        }
        if (dragId.uid != 0 && owner.onMoveFinished) owner.onMoveFinished();
        dragId = juce::AudioProcessorGraph::NodeID(0);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto hit = nodeAt(e.position);
        if (hit.uid != 0)
        {
            if (hit != inputId && hit != outputId && owner.onOpen) owner.onOpen(hit);
        }
        else if (owner.onAddAt)
        {
            owner.onAddAt(juce::roundToInt(e.position.x) - nodeWidth / 2,
                          juce::roundToInt(e.position.y) - nodeHeight / 2);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        updateHover(e.position);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoveredNode.uid != 0 || hoveredPortNode.uid != 0 || hoveredWire.src != 0)
        {
            hoveredNode = {};
            hoveredPortNode = {};
            hoveredWire = {};
            repaint();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            && selected.uid != 0 && selected != inputId && selected != outputId)
        {
            if (owner.onRemove) owner.onRemove(selected);
            selected = juce::AudioProcessorGraph::NodeID(0);
            return true;
        }
        return false;
    }

    PatcherView& owner;
    std::vector<PatchLinkProcessor::LoadedNode> nodes;
    std::vector<patchlink::Wire> wires;
    juce::AudioProcessorGraph::NodeID inputId { 10 }, outputId { 20 }, selected { 0 };
    juce::AudioProcessorGraph::NodeID dragId { 0 }, cableSource { 0 };
    juce::AudioProcessorGraph::NodeID hoveredNode { 0 }, hoveredPortNode { 0 };
    bool hoveredPortOutput = false;
    patchlink::Wire hoveredWire {};
    bool panning = false;
    juce::Point<int> panStartMouseScreen, panStartViewPos;
    juce::Point<float> dragOffset, cableStart, cableEnd;

    void updateHover(juce::Point<float> point)
    {
        juce::AudioProcessorGraph::NodeID nextNode { 0 }, nextPort { 0 };
        bool nextOut = false;
        patchlink::Wire nextWire {};

        if (cableSource.uid != 0)
        {
            const auto dest = portAt(point, false);
            if (dest.uid != 0 && dest != cableSource)
            {
                nextPort = dest;
                nextOut = false;
            }
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        }
        else
        {
            const auto outPort = portAt(point, true);
            const auto inPort = outPort.uid != 0 ? juce::AudioProcessorGraph::NodeID(0) : portAt(point, false);
            if (outPort.uid != 0)
            {
                nextPort = outPort;
                nextOut = true;
                setMouseCursor(juce::MouseCursor::CrosshairCursor);
            }
            else if (inPort.uid != 0)
            {
                nextPort = inPort;
                nextOut = false;
                setMouseCursor(juce::MouseCursor::CrosshairCursor);
            }
            else
            {
                nextNode = nodeAt(point);
                if (nextNode.uid != 0)
                {
                    const bool overPower = (nextNode != inputId && nextNode != outputId && powerRect(boxFor(nextNode)).contains(point));
                    setMouseCursor(overPower ? juce::MouseCursor::PointingHandCursor
                                             : (nextNode == inputId || nextNode == outputId
                                                    ? juce::MouseCursor::NormalCursor
                                                    : juce::MouseCursor::DraggingHandCursor));
                }
                else
                {
                    nextWire = wireAt(point);
                    setMouseCursor(nextWire.src != 0 ? juce::MouseCursor::PointingHandCursor
                                                     : juce::MouseCursor::NormalCursor);
                }
            }
        }

        if (hoveredNode != nextNode || hoveredPortNode != nextPort
            || hoveredPortOutput != nextOut || hoveredWire.src != nextWire.src
            || hoveredWire.dst != nextWire.dst)
        {
            hoveredNode = nextNode;
            hoveredPortNode = nextPort;
            hoveredPortOutput = nextOut;
            hoveredWire = nextWire;
            repaint();
        }
    }

    static constexpr int workspaceWidth = 1900;
    static constexpr int workspaceHeight = 1200;
    static constexpr int nodeWidth = 196;
    static constexpr int nodeHeight = 92;
    static constexpr int gridSize = 24;

    juce::Rectangle<int> boxFor(uint32_t uid) const
    {
        if (uid == inputId.uid) return { 42, 260, 154, 84 };
        if (uid == outputId.uid) return { 1660, 260, 154, 84 };
        for (const auto& node : nodes)
            if (node.id.uid == uid) return { node.x, node.y, nodeWidth, nodeHeight };
        return {};
    }

    juce::Rectangle<int> boxFor(juce::AudioProcessorGraph::NodeID id) const { return boxFor(id.uid); }

    PatchLinkProcessor::LoadedNode* findNode(juce::AudioProcessorGraph::NodeID id)
    {
        for (auto& node : nodes) if (node.id == id) return &node;
        return nullptr;
    }

    static juce::Point<float> outputPort(juce::Rectangle<int> r)
    {
        return { (float)r.getRight(), (float)r.getCentreY() };
    }

    static juce::Point<float> inputPort(juce::Rectangle<int> r)
    {
        return { (float)r.getX(), (float)r.getCentreY() };
    }

    static juce::Path cablePath(juce::Point<float> a, juce::Point<float> b)
    {
        const auto distance = juce::jmax(55.0f, std::abs(b.x - a.x) * 0.46f);
        juce::Path path;
        path.startNewSubPath(a);
        path.cubicTo(a + juce::Point<float>(distance, 0), b - juce::Point<float>(distance, 0), b);
        return path;
    }

    juce::AudioProcessorGraph::NodeID nodeAt(juce::Point<float> p) const
    {
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
            if (boxFor(it->id.uid).expanded(4).toFloat().contains(p)) return it->id;
        if (boxFor(inputId.uid).expanded(4).toFloat().contains(p)) return inputId;
        if (boxFor(outputId.uid).expanded(4).toFloat().contains(p)) return outputId;
        return {};
    }

    juce::AudioProcessorGraph::NodeID portAt(juce::Point<float> p, bool output) const
    {
        auto test = [&](juce::AudioProcessorGraph::NodeID id) {
            const auto box = boxFor(id.uid);
            if (box.isEmpty()) return false;
            const auto port = output ? outputPort(box) : inputPort(box);
            return port.getDistanceFrom(p) <= 14.0f;
        };
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
            if (test(it->id)) return it->id;
        if (output && test(inputId)) return inputId;
        if (!output && test(outputId)) return outputId;
        return {};
    }

    static juce::Rectangle<float> powerRect(juce::Rectangle<int> r)
    {
        return juce::Rectangle<float>((float)r.getRight() - 34.0f, (float)r.getY() + 10.0f, 22.0f, 22.0f);
    }

    patchlink::Wire wireAt(juce::Point<float> point) const
    {
        for (const auto& wire : wires)
        {
            if (wire.srcCh != 0) continue;
            auto path = cablePath(outputPort(boxFor(wire.src)), inputPort(boxFor(wire.dst)));
            juce::Path stroked;
            juce::PathStrokeType(9.0f).createStrokedPath(stroked, path);
            if (stroked.contains(point.x, point.y)) return wire;
        }
        return {};
    }

    void showNodeMenu(juce::AudioProcessorGraph::NodeID id)
    {
        if (id == inputId || id == outputId) return;
        const auto* node = findNode(id);
        if (node == nullptr) return;
        juce::PopupMenu menu;
        menu.addItem(1, "Open interface");
        menu.addItem(2, node->bypass ? "Enable" : "Bypass");
        menu.addItem(3, "Disconnect all");
        menu.addSeparator();
        menu.addItem(4, "Remove plugin");
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, id](int result) {
            switch (result)
            {
                case 1: if (owner.onOpen) owner.onOpen(id); break;
                case 2:
                    if (auto* n = findNode(id); n != nullptr && owner.onBypass) owner.onBypass(id, !n->bypass);
                    break;
                case 3: if (owner.onDisconnectAll) owner.onDisconnectAll(id); break;
                case 4: if (owner.onRemove) owner.onRemove(id); break;
                default: break;
            }
        });
    }

    void drawEndpoint(juce::Graphics& g, bool input, juce::Rectangle<int> r)
    {
        g.setColour(theme::panel);
        g.fillRoundedRectangle(r.toFloat(), theme::radius);
        g.setColour(theme::line);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), theme::radius, 1.0f);
        g.setColour(theme::blue);
        g.setFont(theme::mono(11.0f));
        g.drawText(input ? "AUDIO IN" : "AUDIO OUT", r.reduced(12, 0), juce::Justification::centred);
        g.setColour(theme::faint);
        g.setFont(theme::font(10.0f));
        g.drawText("STEREO", r.translated(0, 20).reduced(12, 0), juce::Justification::centred);
        const auto port = input ? outputPort(r) : inputPort(r);
        const auto endpointId = input ? inputId : outputId;
        const bool portHovered = (hoveredPortNode == endpointId && hoveredPortOutput == input);
        if (portHovered)
        {
            g.setColour(theme::blue.withAlpha(0.24f));
            g.fillEllipse(port.x - 11, port.y - 11, 22, 22);
        }
        g.setColour(theme::background);
        g.fillEllipse(port.x - 7, port.y - 7, 14, 14);
        g.setColour(theme::blue);
        g.drawEllipse(port.x - 5, port.y - 5, 10, 10, portHovered ? 2.2f : 1.5f);
    }

    void drawNode(juce::Graphics& g, const PatchLinkProcessor::LoadedNode& node)
    {
        const auto r = boxFor(node.id.uid);
        const auto rf = r.toFloat();
        const auto isSelected = selected == node.id;
        const auto isHovered = hoveredNode == node.id;
        g.setColour(juce::Colours::black.withAlpha(0.20f));
        g.fillRoundedRectangle(rf.translated(0, 3), theme::radius);
        g.setColour((isSelected || isHovered) ? theme::cardHover : theme::card);
        g.fillRoundedRectangle(rf, theme::radius);
        g.setColour(isSelected ? theme::accent.withAlpha(0.80f)
                               : (isHovered ? theme::line : theme::lineSoft));
        g.drawRoundedRectangle(rf.reduced(0.5f), theme::radius, isSelected ? 1.5f : 1.0f);
        if (node.bypass)
        {
            g.setColour(theme::background.withAlpha(0.24f));
            g.fillRoundedRectangle(rf, theme::radius);
        }

        g.setColour(node.bypass ? theme::muted : theme::text);
        g.setFont(theme::font(14.0f, true));
        const auto name = node.desc.name.isNotEmpty() ? node.desc.name : "Plugin " + juce::String(node.id.uid);
        g.drawText(name, r.getX() + 14, r.getY() + 8, r.getWidth() - 58, 28, juce::Justification::centredLeft, true);
        g.setColour(theme::muted);
        g.setFont(theme::font(10.5f));
        auto details = node.desc.manufacturerName.isNotEmpty() ? node.desc.manufacturerName : "Unknown developer";
        details += "  /  " + node.desc.pluginFormatName;
        g.drawText(details, r.getX() + 14, r.getBottom() - 30, r.getWidth() - 28, 20, juce::Justification::centredLeft, true);

        drawIcon(g, Icon::power, powerRect(r).reduced(3), node.bypass ? theme::faint : theme::accent, 1.4f);
        for (bool output : { false, true })
        {
            const auto port = output ? outputPort(r) : inputPort(r);
            const bool portHovered = (hoveredPortNode == node.id && hoveredPortOutput == output);
            if (portHovered)
            {
                g.setColour(theme::accent.withAlpha(0.24f));
                g.fillEllipse(port.x - 11, port.y - 11, 22, 22);
            }
            g.setColour(theme::background);
            g.fillEllipse(port.x - 7, port.y - 7, 14, 14);
            g.setColour(portHovered ? theme::accentHover : theme::accent.withAlpha(node.bypass ? 0.35f : 0.9f));
            g.drawEllipse(port.x - 5, port.y - 5, 10, 10, portHovered ? 2.2f : 1.5f);
        }
    }
};

PatcherView::PatcherView()
{
    contentOwner = std::make_unique<Content>(*this);
    content = contentOwner.get();
    viewport.setViewedComponent(content, false);
    viewport.setScrollBarsShown(true, true);
    viewport.setScrollBarThickness(10);
    addAndMakeVisible(viewport);

    helpLabel.setText("Drag empty canvas to pan  /  Drag ports to connect  /  Double-click to add or edit",
                      juce::dontSendNotification);
    helpLabel.setFont(theme::font(11.0f));
    helpLabel.setColour(juce::Label::textColourId, theme::faint);
    addAndMakeVisible(helpLabel);
    resetView.onClick = [this] { viewport.setViewPosition(0, 160); };
    addAndMakeVisible(resetView);
}

PatcherView::~PatcherView() = default;

void PatcherView::applyTheme()
{
    helpLabel.setColour(juce::Label::textColourId, theme::faint);
    repaint();
    content->repaint();
}

void PatcherView::setEndpointIds(juce::AudioProcessorGraph::NodeID input, juce::AudioProcessorGraph::NodeID output)
{
    content->inputId = input;
    content->outputId = output;
}

void PatcherView::refresh(const std::vector<PatchLinkProcessor::LoadedNode>& nodes,
                          const std::vector<patchlink::Wire>& wires)
{
    if (content->dragId.uid == 0)
    {
        content->nodes = nodes;
        content->wires = wires;
    }
    content->repaint();
}

void PatcherView::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom(42);
    helpLabel.setBounds(bottom.reduced(16, 0));
    resetView.setBounds(bottom.removeFromRight(112).reduced(4, 5));
    viewport.setBounds(r);
    if (!viewport.isShowing()) viewport.setViewPosition(0, 160);
}
