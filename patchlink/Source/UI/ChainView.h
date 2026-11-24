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
    void setLibraryEmpty(bool isEmpty);
    void applyTheme();
    void resized() override;

    std::function<void(juce::AudioProcessorGraph::NodeID, bool)> onBypass;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onRemove;
    std::function<void(juce::AudioProcessorGraph::NodeID)> onOpen;
    std::function<void(juce::AudioProcessorGraph::NodeID, int)> onReorder;
    std::function<void(int)> onDragSlot;
    std::function<void(int)> onExportSlot;
    std::function<void()> onAdd;
    std::function<void()> onOpenLibrary;
    std::function<void()> onOpenCode;
    std::function<void()> onClear;

private:
    class Content;
    class Slot;

    juce::Viewport viewport;
    std::unique_ptr<Content> contentOwner;
    Content* content = nullptr;
    juce::Label emptyTitle, emptySubtitle, hint;
    PlButton emptyAdd { "Add your first plugin", Icon::plus, PlButton::Style::primary };
    PlButton emptySetup { "Choose VST Folders & Scan", Icon::library, PlButton::Style::primary };
    PlButton emptyOpenCode { "Open code", Icon::code, PlButton::Style::secondary };
    PlButton clearButton { "Clear chain", Icon::trash, PlButton::Style::ghost };
    bool empty = true;
    bool libraryEmpty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainView)
};
