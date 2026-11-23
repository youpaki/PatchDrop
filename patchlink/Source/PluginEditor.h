#pragma once

#include <JuceHeader.h>
#include <map>
#include "Core/Theme.h"
#include "UI/Controls.h"
#include "UI/ChainView.h"
#include "UI/PatcherView.h"

class PatchLinkProcessor;
class HostedPluginWindow;

class PatchLinkEditor final : public juce::AudioProcessorEditor,
                              private juce::ChangeListener,
                              public juce::FileDragAndDropTarget
{
public:
    explicit PatchLinkEditor(PatchLinkProcessor&);
    ~PatchLinkEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    enum class Mode { simple = 1, patcher = 2 };

    PatchLinkProcessor& proc;
    theme::LookAndFeel lookAndFeel;
    Mode mode = Mode::simple;
    const juce::String apiBase { "https://gg.trenas.fr/patchdrop" };

    PlButton simpleButton { "Chain", Icon::list, PlButton::Style::ghost };
    PlButton patcherButton { "Patcher", Icon::grid, PlButton::Style::ghost };
    PlButton libraryButton { "Library", Icon::library, PlButton::Style::secondary };
    PlButton codeButton { "Open code", Icon::code, PlButton::Style::secondary };
    PlButton shareButton { "Share", Icon::share, PlButton::Style::secondary };
    PlButton importButton { "Import", Icon::importFile, PlButton::Style::ghost };
    PlButton exportButton { "Export", Icon::exportFile, PlButton::Style::ghost };
    PlButton addButton { "Add plugin", Icon::plus, PlButton::Style::primary };
    juce::Label patchName, statusLabel, chainInfo, systemInfo;
    ChainView chainView;
    PatcherView patcherView;
    Toast toast;

    std::unique_ptr<juce::Component> overlay;
    std::unique_ptr<juce::DialogWindow> scanWindow;
    juce::OwnedArray<HostedPluginWindow> pluginWindows;
    juce::String lastShareCode;
    bool isDraggingFile = false;
    juce::String dragDropHint;
    std::map<juce::String, juce::MemoryBlock> droppedFileCache;

    void cacheDraggedFiles(const juce::StringArray& files);

    void setMode(Mode);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshViews();
    void setStatus(const juce::String&);
    void showToast(const juce::String&, bool isError = false);

    void openPluginBrowser(int x, int y);
    void openShareDialog(bool publish);
    void dismissOverlay();
    void doImport();
    void importFiles(const juce::StringArray& files, int dropX, int dropY);
    void doExport();
    void confirmClearChain();
    void openScanDialog();
    void openPluginWindow(juce::AudioProcessorGraph::NodeID);
    void closePluginWindow(HostedPluginWindow*);
    void closeWindowsForNode(juce::AudioProcessorGraph::NodeID);
    void showMissingPlugins(const juce::StringArray&, const juce::String& title = "Chain loaded");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchLinkEditor)
};
