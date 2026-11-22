#pragma once
#include <JuceHeader.h>
#include "../Scan/PluginScanManager.h"
#include "../Core/DesignTokens.h"

// ScanDialog: the Scan Manager. Dedicated window, never a hidden checkbox.
// - Folder sources grouped by origin (system / DAW-found / yours) with on/off + recursive.
// - DAW proposal row: "Found FL Studio — its folders are pre-checked. [Scan]".
// - Progress bar + current file + Cancel; results summary; blacklist tab.
class ScanDialog : public juce::Component,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    explicit ScanDialog(PluginScanManager& m);
    ~ScanDialog() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginScanManager& mgr;

    juce::Label titleLabel, subLabel, statusLabel, progLabel;
    juce::ProgressBar progressBar;
    double progressValue = 0.0;
    juce::TextButton scanBtn { "Scan selected" }, rescanBtn { "Full rescan" },
                     cancelBtn { "Cancel" }, addFolderBtn { "+ Add folder…" },
                     closeBtn { "Done" }, rescanFailedBtn { "Rescan failed only" };

    struct SourceRow : public juce::Component
    {
        ScanDialog& owner;
        size_t index;
        juce::ToggleButton on { "" };
        juce::Label pathLabel, originLabel;
        juce::ToggleButton rec { "subfolders" };
        juce::TextButton del { "x" };
        SourceRow(ScanDialog& o, size_t i);
        void refresh(const PluginScanManager::Source& s);
        void resized() override;
    };
    juce::OwnedArray<SourceRow> rows;
    juce::Component listHolder;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Component resultsPage;
    juce::Label resultsLabel;
    juce::TextEditor resultsBox;
    juce::Component blacklistPage;
    juce::Label blLabel;
    juce::TextEditor blBox;
    juce::TextButton blClearBtn { "Remove selected from blacklist" };

    void rebuildRows();
    void refreshResults();
    void refreshBlacklist();
    void timerCallback() override; // progress pump
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // scan finished

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanDialog)
};
