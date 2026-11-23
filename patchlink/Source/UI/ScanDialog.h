#pragma once

#include <JuceHeader.h>
#include "../Core/Theme.h"
#include "../Scan/PluginScanManager.h"
#include "Controls.h"

class ScanDialog : public juce::Component,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    explicit ScanDialog(PluginScanManager& manager);
    ~ScanDialog() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginScanManager& mgr;
    theme::LookAndFeel lookAndFeel;

    juce::Label titleLabel, subtitleLabel;
    juce::Label sourcesLabel, sourceCountLabel, emptySourcesLabel;

    struct SourceRow final : public juce::Component
    {
        SourceRow(ScanDialog&, std::size_t);

        void refresh(const PluginScanManager::Source&);
        void paint(juce::Graphics&) override;
        void resized() override;

        ScanDialog& owner;
        std::size_t index;
        juce::String sourcePath;
        juce::ToggleButton enabledButton;
        juce::Label originLabel, pathLabel;
        juce::ToggleButton recursiveButton { "Subfolders" };
        PlButton deleteButton { {}, Icon::trash, PlButton::Style::ghost };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceRow)
    };

    static constexpr int sourceRowHeight = 40;
    juce::Viewport sourcesViewport;
    juce::Component sourceListContent;
    juce::OwnedArray<SourceRow> rows;

    juce::Label statusLabel, scanFoundLabel, currentFileLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar;

    juce::Label resultsHeading, resultsSummary, failedFilesLabel;
    juce::TextEditor resultsBox;
    juce::Label blacklistHeading, blacklistCountLabel, blacklistHelpLabel;
    juce::TextEditor blacklistBox;
    PlButton removeBlacklistButton { "Remove selected", Icon::trash, PlButton::Style::ghost };

    PlButton addFolderButton { "Add folder", Icon::plus, PlButton::Style::secondary };
    PlButton scanButton { "Scan selected", Icon::scan, PlButton::Style::primary };
    PlButton rescanButton { "Full rescan", Icon::none, PlButton::Style::secondary };
    PlButton cancelButton { "Cancel", Icon::close, PlButton::Style::secondary };
    PlButton closeButton { "Done", Icon::check, PlButton::Style::secondary };

    juce::Rectangle<int> sourcePanelBounds, resultsPanelBounds, blacklistPanelBounds;
    bool scanning = false;
    bool cancelRequested = false;
    bool fullScanRequested = false;
    bool rebuildPending = false;

    void parentHierarchyChanged() override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void beginScan(bool fullRescan);
    void refreshScanProgress();
    void handleScanFinished();
    void updateActionStates();

    void rebuildRows();
    void requestRebuildRows();
    void layoutSourceRows();
    void setSourceEnabled(std::size_t, const juce::String&, bool);
    void setSourceRecursive(std::size_t, const juce::String&, bool);
    void removeSource(std::size_t, const juce::String&);
    void addFolder();

    void refreshResults();
    void refreshBlacklist();
    void removeSelectedBlacklistEntries();
    void layoutDetailsPanels(juce::Rectangle<int>);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanDialog)
};
