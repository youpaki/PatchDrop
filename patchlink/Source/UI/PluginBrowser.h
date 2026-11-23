#pragma once

#include <JuceHeader.h>
#include "Controls.h"

class PluginBrowser final : public ModalOverlay
{
public:
    explicit PluginBrowser(const juce::Array<juce::PluginDescription>&);
    void paintOverChildren(juce::Graphics&) override;

    std::function<void(const juce::PluginDescription&)> onPluginChosen;
    std::function<void()> onScanRequested;

private:
    class ResultsModel;

    juce::Array<juce::PluginDescription> allTypes;
    std::vector<juce::PluginDescription> filtered;
    juce::TextEditor search;
    juce::Label title, subtitle, countLabel, emptyLabel;
    PlButton closeButton { "", Icon::close, PlButton::Style::ghost };
    PlButton scanButton { "Open Scan Manager", Icon::scan, PlButton::Style::secondary };
    juce::ListBox results;
    std::unique_ptr<ResultsModel> model;

    void filter();
    void chooseSelected();
    void chooseRow(int);
    void layoutCard(juce::Rectangle<int>) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowser)
};
