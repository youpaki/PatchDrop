#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "Controls.h"
#include "../Core/PresetDatabase.h"
#include "../Chain/ShareClient.h"

class PresetHubView final : public juce::Component,
                            private juce::Timer
{
public:
    enum class Scope { local = 0, online = 1 };

    PresetHubView(patchdb::PresetDatabase& database,
                  const juce::KnownPluginList& knownPlugins,
                  const juce::String& apiBaseUrl);
    ~PresetHubView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void applyTheme();
    void applyLanguage();
    void refreshLocal(bool preserveSelection = true);
    void refreshOnlineCatalog();
    void refreshPluginAvailability();

    void openSaveCurrentDialog();
    void setScope(Scope newScope);
    bool isLocalScope() const noexcept { return scope == Scope::local; }
    juce::String getActiveLocalFolderForDrop() const;

    // Callbacks wired to PatchLinkEditor
    std::function<void(const juce::var& patch, const juce::String& title)> onLoadPatch;
    std::function<void(const juce::var& patch, const juce::String& title)> onStagePatch;
    std::function<void(const juce::var& patch, const juce::String& title, bool startDrag)> onDragPatchToDaw;
    std::function<void(const juce::var& patch, const juce::String& title)> onSharePatch;
    std::function<void(const juce::String& targetFolder)> onImportFilesRequested;
    std::function<juce::var()> onRequestCurrentPatch;
    std::function<juce::String()> onRequestCurrentTitle;
    std::function<void(const juce::String& message, bool isError)> onToast;
    std::function<void(const juce::String& statusText)> onStatus;
    std::function<void(std::unique_ptr<juce::Component> modal)> onShowModal;
    std::function<void()> onDismissModal;

private:
    class SidebarContent;
    class GridContent;
    class InspectorPanel;

    patchdb::PresetDatabase& db;
    const juce::KnownPluginList& knownList;
    juce::String apiBase;

    Scope scope = Scope::local;
    juce::String selectedLocalFolder { "__ALL__" };
    juce::String selectedLocalTag;
    juce::String selectedLocalId;

    juce::String selectedOnlineCategory { "all" };
    juce::String selectedOnlineTag;
    juce::String selectedOnlineCode;

    std::vector<patchdb::LocalPreset> filteredLocal;
    ShareClient::CatalogResult onlineCatalog;
    bool onlineLoading = false;
    bool onlineLoadedOnce = false;
    bool onlineDownloadBusy = false;
    uint64_t onlineRequestGen = 0;

    // Top bar widgets
    PlButton localScopeBtn { "Local Library", Icon::folder, PlButton::Style::ghost };
    PlButton onlineScopeBtn { "Online (PatchDrop)", Icon::globe, PlButton::Style::ghost };
    juce::TextEditor searchBox;
    PlButton clearSearchBtn { "", Icon::close, PlButton::Style::ghost };
    juce::ComboBox sortBox;
    PlButton saveCurrentBtn { "Save current", Icon::save, PlButton::Style::primary };
    PlButton importBtn { "Import", Icon::importFile, PlButton::Style::secondary };
    PlButton refreshOnlineBtn { "Refresh", Icon::refresh, PlButton::Style::secondary };

    // Panes
    std::unique_ptr<SidebarContent> sidebarContent;
    juce::Viewport sidebarViewport;

    std::unique_ptr<GridContent> gridContent;
    juce::Viewport gridViewport;

    std::unique_ptr<InspectorPanel> inspector;

    // Cache of normalized local plugin names for fast Installed/Missing checks
    juce::StringArray normalizedInstalledNames;

    void timerCallback() override;
    void rebuildSortOptions();
    void updateTopBarCountsAndVisibility();
    void runLocalQuery(bool preserveSelection = true);
    void scheduleSearchRefresh();
    bool isPluginInstalledLocally(const juce::String& pluginName) const;

    void selectLocalPreset(const juce::String& id);
    void selectOnlinePreset(const juce::String& code);

    void promptNewFolder();
    void promptRenameFolder(const juce::String& oldName);
    void confirmDeleteFolder(const juce::String& name);

    void openEditMetaDialog(const patchdb::LocalPreset& preset);
    void openSaveModalInternal(bool isEdit,
                               const patchdb::LocalPreset& initialMeta,
                               const juce::var& patchVar);
    void showMoveFolderMenu(const juce::String& presetId, juce::Component* target);
    void showLocalCardContextMenu(const patchdb::LocalPreset& preset);
    void confirmDeletePreset(const patchdb::LocalPreset& preset);

    void loadSelectedLocal();
    void stageSelectedLocal();
    void dragSelectedLocal(bool startDrag);
    void shareSelectedLocal();

    void downloadAndSaveOnline(const ShareClient::OnlinePresetSummary& item, bool alsoLoad, bool startDragDaw = false, bool formatMenu = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetHubView)
};
