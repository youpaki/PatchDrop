#pragma once

#include <JuceHeader.h>
#include "Controls.h"
#include "../PluginProcessor.h"

class ChainView final : public juce::Component
{
public:
    ChainView();
    ~ChainView() override;
    void refresh(const std::vector<PatchLinkProcessor::LoadedNode>&);
    void applyTheme();
    void resized() override;

    std::function<void(juce::AudioProcessorGraph::NodeID, bool)> onBypass;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onRemove;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onOpen;
    std::function<void(juce::AudioProcessorGraph::NodeID, int)> onReorder;
    std::function<void()> onAdd;
    std::function<void()> onClear;

private:
    class Content;
    class Slot;

    juce::Viewport viewport;
    std::unique_ptr<Content> contentOwner;
    Content* content = nullptr;
    juce::Label emptyTitle, emptySubtitle, hint;
    PlButton emptyAdd { "Add your first plugin", Icon::plus, PlButton::Style::primary };
    PlButton clearButton { "Clear chain", Icon::trash, PlButton::Style::ghost };
    bool empty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainView)
};
