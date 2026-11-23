#pragma once

#include <JuceHeader.h>
#include <map>
#include "Chain/DawExport.h"
#include "Core/Theme.h"
#include "UI/Controls.h"
#include "UI/ChainView.h"
#include "UI/PatcherView.h"

class PatchLinkProcessor;
class HostedPluginWindow;

class PatchLinkEditor final : public juce::AudioProcessorEditor,
                              private juce::ChangeListener,
                              public juce::FileDragAndDropTarget,
                              public juce::TextDragAndDropTarget,
                              public juce::DragAndDropContainer
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

    bool isInterestedInTextDrag(const juce::String& text) override;
    void textDragEnter(const juce::String& text, int x, int y) override;
    void textDragMove(const juce::String& text, int x, int y) override;
    void textDragExit(const juce::String& text) override;
    void textDropped(const juce::String& text, int x, int y) override;

private:
    enum class Mode { simple = 1, patcher = 2 };

    PatchLinkProcessor& proc;
    theme::LookAndFeel lookAndFeel;
    Mode mode = Mode::simple;
    const juce::String apiBase { "https://gg.trenas.fr/patchdrop" };

    PlButton simpleButton { "Chain", Icon::list, PlButton::Style::ghost };
    PlButton patcherButton { "Patcher", Icon::grid, PlButton::Style::ghost };
    PlButton themeButton { "Theme", Icon::none, PlButton::Style::ghost };
    PlButton libraryButton { "Library", Icon::library, PlButton::Style::secondary };
    PlButton codeButton { "Open code", Icon::code, PlButton::Style::secondary };
    PlButton shareButton { "Share", Icon::share, PlButton::Style::secondary };
    PlButton importButton { "Import", Icon::importFile, PlButton::Style::ghost };
    PlButton exportButton { "Export", Icon::exportFile, PlButton::Style::ghost };
    PlButton dragDawButton { "Drag to DAW", Icon::exportFile, PlButton::Style::secondary };
    PlButton addButton { "Add plugin", Icon::plus, PlButton::Style::primary };
    juce::Label patchName, statusLabel, chainInfo, systemInfo;
    ChainView chainView;
    PatcherView patcherView;
    Toast toast;

    std::unique_ptr<juce::Component> overlay;
    std::unique_ptr<juce::DialogWindow> scanWindow;
    juce::OwnedArray<HostedPluginWindow> pluginWindows;
    juce::String lastShareCode;
    dawexport::HostDetection hostDetection;
    dawexport::TargetFormat selectedExportFormat = dawexport::TargetFormat::autoDetect;
    juce::var stagedPatchVar;
    juce::String stagedPatchTitle;
    bool isDraggingFile = false;
    juce::String dragDropHint;
    std::map<juce::String, juce::MemoryBlock> droppedFileCache;

    void cacheDraggedFiles(const juce::StringArray& files);

    void setMode(Mode);
    void openThemeMenu();
    void applyCurrentTheme(int index);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshViews();
    void setStatus(const juce::String&);
    void showToast(const juce::String&, bool isError = false);

    dawexport::TargetFormat effectiveExportFormat() const;
    void updateExportButtonState();
    juce::var getActiveOrStagedPatchVar();
    void stagePatch(const juce::var& patchVar, const juce::String& title, const juce::String& toastMsg = {});
    void clearStagedPatch();
    void showFormatPickerMenu(const juce::var& customPatch = {}, int singleSlotIndex = -1, bool fromDragAttempt = false);
    void startExternalDawDrag(const juce::var& customPatch = {}, int singleSlotIndex = -1);
    void saveDawPresetToDisk(const juce::var& customPatch, dawexport::TargetFormat fmt, int singleSlotIndex = -1);
    void loadPatchIntoProcessor(const juce::var& patch, const juce::String& label);
    void showEmptyDropModal(const juce::var& stagedVar, const juce::String& label, const juce::StringArray& notes,
                            std::function<void()> loadIntoPatchLinkFn);

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
