#pragma once

#include <JuceHeader.h>
#include "Controls.h"
#include "../PluginProcessor.h"

class PatcherView final : public juce::Component
{
public:
    PatcherView();
    ~PatcherView() override;

    void setEndpointIds(juce::AudioProcessorGraph::NodeID input, juce::AudioProcessorGraph::NodeID output);
    void refresh(const std::vector<PatchLinkProcessor::LoadedNode>&,
                 const std::vector<patchlink::Wire>&);
    void applyTheme();
    void applyLanguage();
    void resized() override;
    juce::Point<int> getScrollOffset() const { return viewport.getViewPosition(); }

    std::function<void(int, int)> onAddAt;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onOpen;
    std::function<void(juce::AudioProcessorGraph::NodeID, bool)> onBypass;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onRemove;
    std::function<void(juce::AudioProcessorGraph::NodeID, juce::AudioProcessorGraph::NodeID)> onConnect;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onDisconnectAll;
    std::function<void(juce::AudioProcessorGraph::NodeID, juce::AudioProcessorGraph::NodeID)> onDisconnect;
    std::function<void(juce::AudioProcessorGraph::NodeID, int, int)> onMove;
    std::function<void()> onMoveFinished;

private:
    class Content;
    std::unique_ptr<Content> contentOwner;
    Content* content = nullptr;
    juce::Viewport viewport;
    juce::Label helpLabel;
    PlButton resetView { "Reset view", Icon::grid, PlButton::Style::ghost };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatcherView)
};
