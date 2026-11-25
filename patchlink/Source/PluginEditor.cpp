#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Chain/ShareClient.h"
#include "Core/ChainFormat.h"
#include "UI/PluginBrowser.h"
#include "UI/ShareDialog.h"
#include "UI/ScanDialog.h"
#include <thread>

class HostedPluginWindow final : public juce::DocumentWindow
{
public:
    HostedPluginWindow(const juce::String& name, juce::AudioProcessorGraph::NodeID id,
                       juce::AudioProcessor* processor)
        : juce::DocumentWindow(name, theme::background,
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          nodeId(id)
    {
        setUsingNativeTitleBar(true);
        std::unique_ptr<juce::AudioProcessorEditor> content(processor->createEditor());
        if (content == nullptr) content = std::make_unique<juce::GenericAudioProcessorEditor>(*processor);
        setResizable(content->isResizable(), false);
        setContentOwned(content.release(), true);
        setWantsKeyboardFocus(false);
    }

    void closeButtonPressed() override
    {
        if (onClosed) onClosed(this);
        else setVisible(false);
    }

    juce::AudioProcessorGraph::NodeID nodeId;
    std::function<void(HostedPluginWindow*)> onClosed;
};

class ScanManagerWindow final : public juce::DialogWindow
{
public:
    ScanManagerWindow() : juce::DialogWindow(i18n::tr("Plugin Library", "Bibliothèque de plugins"), theme::background, true, false) {}
    void closeButtonPressed() override { setVisible(false); }
};

PatchLinkEditor::PatchLinkEditor(PatchLinkProcessor& processor)
    : AudioProcessorEditor(processor), proc(processor)
{
    i18n::loadSavedLanguage();
    theme::loadSavedTheme();
    lookAndFeel.applyPalette();
    setLookAndFeel(&lookAndFeel);
    setSize(1120, 720);
    setResizeLimits(840, 540, 3200, 2200);
    setResizable(true, true);
    setWantsKeyboardFocus(true);

    presetDb.load();

    simpleButton.onClick = [this] { setMode(Mode::simple); };
    patcherButton.onClick = [this] { setMode(Mode::patcher); };
    presetsButton.onClick = [this] { setMode(Mode::presets); };
    themeButton.onClick = [this] { openThemeMenu(); };
    langButton.onClick = [this] { openLanguageMenu(); };
    addButton.onClick = [this] {
        const auto index = (int)proc.getLoadedNodes().size();
        openPluginBrowser(282 + (index % 5) * 224, 208 + (index / 5) * 132);
    };
    libraryButton.onClick = [this] { openScanDialog(); };
    shareButton.onClick = [this] { openShareDialog(true); };
    codeButton.onClick = [this] { openShareDialog(false); };
    importButton.onClick = [this] { doImport(); };
    exportButton.onClick = [this] { doExport(); };
    savePresetButton.onClick = [this] {
        if (presetHubView != nullptr)
            presetHubView->openSaveCurrentDialog();
    };
    dragDawButton.onDragStart = [this] { startExternalDawDrag({}, -1); };
    dragDawButton.onClick = [this] { showFormatPickerMenu({}, -1, false); };
    for (auto* button : { &simpleButton, &patcherButton, &presetsButton, &themeButton, &langButton, &libraryButton, &codeButton,
                          &shareButton, &importButton, &exportButton, &savePresetButton, &dragDawButton, &addButton })
        addAndMakeVisible(button);

    patchName.setText(proc.getPatchName(), juce::dontSendNotification);
    patchName.setFont(theme::font(17.0f, true));
    patchName.setEditable(true, true, false);
    patchName.onTextChange = [this] {
        const auto txt = patchName.getText().trim();
        if (txt.isNotEmpty())
        {
            proc.setPatchName(txt);
            if (stagedPatchVar.isObject())
            {
                stagedPatchTitle = txt;
                if (auto* o = stagedPatchVar.getDynamicObject())
                    o->setProperty("name", txt);
            }
        }
    };
    addAndMakeVisible(patchName);

    statusLabel.setFont(theme::font(11.0f));
    statusLabel.setColour(juce::Label::textColourId, theme::muted);
    chainInfo.setFont(theme::mono(10.0f));
    chainInfo.setColour(juce::Label::textColourId, theme::faint);
    chainInfo.setJustificationType(juce::Justification::centred);
    systemInfo.setFont(theme::mono(10.0f));
    systemInfo.setColour(juce::Label::textColourId, theme::faint);
    systemInfo.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(chainInfo);
    addAndMakeVisible(systemInfo);

    chainView.onBypass = [this](auto id, bool bypass) { proc.setNodeBypass(id, bypass); };
    chainView.onRemove = [this](auto id) { closeWindowsForNode(id); proc.removeNode(id); };
    chainView.onOpen = [this](auto id) { openPluginWindow(id); };
    chainView.onReorder = [this](auto id, int index) { proc.moveNode(id, index); };
    chainView.onDragSlot = [this](int slotIdx) { startExternalDawDrag({}, slotIdx); };
    chainView.onExportSlot = [this](int slotIdx) { showFormatPickerMenu({}, slotIdx, false); };
    chainView.onAdd = [this] { addButton.triggerClick(); };
    chainView.onOpenLibrary = [this] { openScanDialog(); };
    chainView.onOpenCode = [this] { openShareDialog(false); };
    chainView.onClear = [this] { confirmClearChain(); };
    addAndMakeVisible(chainView);

    patcherView.setEndpointIds(proc.getInputNodeId(), proc.getOutputNodeId());
    patcherView.onAddAt = [this](int x, int y) { openPluginBrowser(x, y); };
    patcherView.onOpen = [this](auto id) { openPluginWindow(id); };
    patcherView.onBypass = [this](auto id, bool bypass) { proc.setNodeBypass(id, bypass); };
    patcherView.onRemove = [this](auto id) { closeWindowsForNode(id); proc.removeNode(id); };
    patcherView.onMove = [this](auto id, int x, int y) { proc.setNodePosition(id, x, y); };
    patcherView.onMoveFinished = [this] { proc.sendChangeMessage(); };
    patcherView.onDisconnectAll = [this](auto id) { proc.disconnectAll(id); };
    patcherView.onDisconnect = [this](auto src, auto dst) { proc.disconnect(src, dst); };
    patcherView.onConnect = [this](auto src, auto dst) {
        const auto left = proc.connectNodes(src, 0, dst, 0);
        const auto right = proc.connectNodes(src, 1, dst, 1);
        if (!left || !right) showToast("That connection could not be made. Check for a feedback loop.", true);
    };
    addChildComponent(patcherView);

    presetHubView = std::make_unique<PresetHubView>(presetDb, proc.getKnownList(), apiBase);
    presetHubView->onLoadPatch = [this](const juce::var& patch, const juce::String& title) {
        setMode(Mode::simple);
        loadPatchIntoProcessor(patch, title);
    };
    presetHubView->onStagePatch = [this](const juce::var& patch, const juce::String& title) {
        stagePatch(patch, title,
                   i18n::tr("Staged \"", "Chaîne \"") + title
                       + i18n::tr("\" (zero-load) - drag button to your DAW",
                                  "\" prête (sans chargement) - glissez vers votre DAW"));
    };
    presetHubView->onDragPatchToDaw = [this](const juce::var& patch, const juce::String& title, bool startDrag) {
        stagePatch(patch, title);
        if (startDrag)
            startExternalDawDrag(patch, -1);
        else
            showFormatPickerMenu(patch, -1, false);
    };
    presetHubView->onSharePatch = [this](const juce::var& patch, const juce::String&) {
        openShareDialog(true, patch);
    };
    presetHubView->onImportFilesRequested = [this](const juce::String& targetFolder) {
        doImportIntoLibrary(targetFolder);
    };
    presetHubView->onRequestCurrentPatch = [this]() { return getActiveOrStagedPatchVar(); };
    presetHubView->onRequestCurrentTitle = [this]() {
        const auto t = patchName.getText().trim();
        return t.isNotEmpty() ? t : proc.getPatchName();
    };
    presetHubView->onToast = [this](const juce::String& msg, bool err) { showToast(msg, err); };
    presetHubView->onStatus = [this](const juce::String& txt) { setStatus(txt); };
    presetHubView->onShowModal = [this](std::unique_ptr<juce::Component> modal) {
        dismissOverlay();
        overlay = std::move(modal);
        if (overlay != nullptr)
        {
            overlay->setBounds(getLocalBounds());
            addAndMakeVisible(overlay.get());
            overlay->grabKeyboardFocus();
        }
    };
    presetHubView->onDismissModal = [this] { dismissOverlay(); };
    addChildComponent(presetHubView.get());

    addChildComponent(toast);

    hostDetection = dawexport::detectHostDaw(proc.wrapperType);
    selectedExportFormat = hostDetection.isStandalone ? dawexport::TargetFormat::autoDetect
                                                      : hostDetection.detectedFormat;
    applyLanguage(i18n::currentLang, false);

    proc.addChangeListener(this);
    proc.getScanManager().addChangeListener(this);
    setMode(proc.preferredMode == 2 ? Mode::patcher : (proc.preferredMode == 3 ? Mode::presets : Mode::simple));
    setStatus(i18n::tr("Ready  ·  ", "Prêt  ·  ")
              + (hostDetection.isStandalone ? i18n::tr("Standalone", "Autonome")
                                            : (i18n::tr("Host: ", "Hôte : ") + hostDetection.hostName)));

    if (!proc.getScanManager().hasCompletedSetup()
        && proc.getKnownList().getNumTypes() == 0
        && !proc.getScanManager().isScanning())
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
            if (safe && safe->overlay == nullptr
                && !safe->proc.getScanManager().hasCompletedSetup()
                && safe->proc.getKnownList().getNumTypes() == 0)
            {
                safe->showFirstRunSetupModal();
            }
        });
    }
}

PatchLinkEditor::~PatchLinkEditor()
{
    proc.getScanManager().removeChangeListener(this);
    proc.removeChangeListener(this);
    pluginWindows.clear();
    scanWindow.reset();
    overlay.reset();
    setLookAndFeel(nullptr);
}

void PatchLinkEditor::setMode(Mode newMode)
{
    mode = newMode;
    simpleButton.setSelected(mode == Mode::simple);
    patcherButton.setSelected(mode == Mode::patcher);
    presetsButton.setSelected(mode == Mode::presets);
    proc.preferredMode = static_cast<int>(mode);
    if (mode == Mode::simple)
    {
        proc.autoConnectSerial = true;
        proc.rebuildSerialIfNeeded();
        chainView.setVisible(true);
        patcherView.setVisible(false);
        if (presetHubView != nullptr) presetHubView->setVisible(false);
    }
    else if (mode == Mode::patcher)
    {
        proc.autoConnectSerial = false;
        chainView.setVisible(false);
        patcherView.setVisible(true);
        if (presetHubView != nullptr) presetHubView->setVisible(false);
    }
    else
    {
        chainView.setVisible(false);
        patcherView.setVisible(false);
        if (presetHubView != nullptr)
        {
            presetHubView->setVisible(true);
            presetHubView->refreshPluginAvailability();
            presetHubView->refreshLocal(true);
        }
    }
    refreshViews();
    resized();
}

void PatchLinkEditor::openThemeMenu()
{
    juce::PopupMenu menu;
    for (int i = 0; i < theme::themeCount; ++i)
        menu.addItem(i + 1, theme::themeName(i), true, i == theme::currentThemeIndex);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&themeButton),
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this)](int result) {
            if (safe && result > 0)
                safe->applyCurrentTheme(result - 1);
        });
}

void PatchLinkEditor::applyCurrentTheme(int idx)
{
    theme::applyTheme(idx, true);
    lookAndFeel.applyPalette();
    statusLabel.setColour(juce::Label::textColourId, theme::muted);
    chainInfo.setColour(juce::Label::textColourId, theme::faint);
    systemInfo.setColour(juce::Label::textColourId, theme::faint);
    patchName.setColour(juce::Label::textColourId, theme::text);
    chainView.applyTheme();
    patcherView.applyTheme();
    if (presetHubView != nullptr)
        presetHubView->applyTheme();
    for (auto* window : pluginWindows)
        window->setBackgroundColour(theme::background);
    if (scanWindow != nullptr && !proc.getScanManager().isScanning() && !scanWindow->isVisible())
        scanWindow.reset();
    sendLookAndFeelChange();
    repaint();
    showToast(i18n::tr("Theme: ", "Thème : ") + theme::themeName(theme::currentThemeIndex));
}

void PatchLinkEditor::openLanguageMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "English", true, !i18n::isFrench());
    menu.addItem(2, juce::String::fromUTF8("Français"), true, i18n::isFrench());

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&langButton),
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this)](int result) {
            if (!safe || result == 0)
                return;
            safe->applyLanguage(result == 2 ? i18n::Lang::fr : i18n::Lang::en, true);
        });
}

void PatchLinkEditor::applyLanguage(i18n::Lang lang, bool notify)
{
    i18n::setLanguage(lang, notify);

    simpleButton.setButtonText(i18n::tr("Chain", "Chaîne"));
    simpleButton.setTooltip(i18n::tr("A serial chain from top to bottom", "Une chaîne en série de haut en bas"));
    patcherButton.setButtonText("Patcher");
    patcherButton.setTooltip(i18n::tr("A free-form stereo routing canvas", "Un espace libre de routage stéréo"));
    presetsButton.setButtonText(i18n::tr("Presets", "Presets"));
    presetsButton.setTooltip(i18n::tr("Browse your local preset folders and download chains from PatchDrop",
                                      "Explorer vos dossiers de presets locaux et télécharger depuis PatchDrop"));
    themeButton.setButtonText(i18n::tr("Theme", "Thème"));
    themeButton.setTooltip(i18n::tr("Switch interface color theme", "Changer le thème de couleurs"));
    langButton.setButtonText(i18n::langCode());
    langButton.setTooltip(i18n::tr("Switch language (EN / FR)", "Changer de langue (EN / FR)"));
    libraryButton.setButtonText(i18n::tr("Library", "Dossiers"));
    libraryButton.setTooltip(i18n::tr("Manage plugin folders, scans, and blacklist",
                                      "Gérer les dossiers VST, scans et liste noire"));
    codeButton.setButtonText(i18n::tr("Open code", "Ouvrir code"));
    shareButton.setButtonText(i18n::tr("Share", "Partager"));
    importButton.setButtonText(i18n::tr("Import", "Importer"));
    exportButton.setButtonText(i18n::tr("Export", "Exporter"));
    savePresetButton.setButtonText(i18n::tr("Save", "Sauver"));
    savePresetButton.setTooltip(i18n::tr("Save current chain to Local Preset Library (Ctrl+S)",
                                         "Enregistrer la chaîne dans la bibliothèque locale (Ctrl+S)"));
    addButton.setButtonText(i18n::tr("Add plugin", "Ajouter"));
    addButton.setTooltip(i18n::tr("Search your plugin library (Ctrl+F)", "Chercher un plugin (Ctrl+F)"));
    patchName.setTooltip(i18n::tr("Double-click to rename this chain", "Double-clic pour renommer la chaîne"));

    updateExportButtonState();
    chainView.applyLanguage();
    patcherView.applyLanguage();
    if (presetHubView != nullptr)
        presetHubView->applyLanguage();
    if (scanWindow != nullptr && !proc.getScanManager().isScanning())
        scanWindow.reset();
    refreshViews();
    resized();
    if (notify)
        showToast(i18n::isFrench() ? juce::String::fromUTF8("Langue : Français") : juce::String("Language: English"));
}

void PatchLinkEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshViews();
}

void PatchLinkEditor::refreshViews()
{
    const auto nodes = proc.getLoadedNodes();
    if (!nodes.empty() && stagedPatchVar.isObject())
    {
        stagedPatchVar = juce::var();
        stagedPatchTitle.clear();
    }

    chainView.setLibraryEmpty(proc.getKnownList().getNumTypes() == 0);
    chainView.refresh(nodes);
    patcherView.refresh(nodes, proc.getWires());
    if (presetHubView != nullptr && presetHubView->isVisible())
        presetHubView->refreshPluginAvailability();

    int stagedCount = 0;
    if (nodes.empty())
        if (auto* stObj = stagedPatchVar.getDynamicObject())
            if (auto* arr = stObj->getProperty("nodes").getArray())
                stagedCount = arr->size();

    if (!patchName.hasKeyboardFocus(true))
    {
        if (nodes.empty() && stagedPatchVar.isObject() && stagedPatchTitle.isNotEmpty())
            patchName.setText(stagedPatchTitle, juce::dontSendNotification);
        else
            patchName.setText(proc.getPatchName(), juce::dontSendNotification);
    }

    if (nodes.empty() && stagedCount > 0)
    {
        chainInfo.setText(juce::String(stagedCount) + i18n::tr(" staged (zero-load)", " prêt(s) (sans chargement)"),
                          juce::dontSendNotification);
    }
    else
    {
        chainInfo.setText(juce::String((int)nodes.size())
                          + (nodes.size() == 1 ? i18n::tr(" plugin", " plugin") : i18n::tr(" plugins", " plugins"))
                          + i18n::tr("  /  STEREO", "  /  STÉRÉO"),
                          juce::dontSendNotification);
    }

    const auto sampleRate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    systemInfo.setText(juce::String(proc.getKnownList().getNumTypes()) + i18n::tr(" indexed  /  ", " indexé(s)  /  ")
                       + juce::String(sampleRate / 1000.0, 1) + " kHz", juce::dontSendNotification);
    if (proc.getScanManager().isScanning()) setStatus(i18n::tr("Indexing plugin library...", "Scan des plugins en cours..."));
    else if (statusLabel.getText().startsWith("Indexing") || statusLabel.getText().startsWith("Scan des plugins")) setStatus(i18n::tr("Ready", "Prêt"));
    repaint();
}

void PatchLinkEditor::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void PatchLinkEditor::showToast(const juce::String& text, bool isError)
{
    toast.setSize(juce::jmin(getWidth() - 40, juce::jmax(280, 44 + juce::roundToInt(theme::font(12.5f).getStringWidthFloat(text)))),
                  42);
    resized();
    toast.toFront(false);
    toast.show(text, isError);
}

void PatchLinkEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::surface);
    g.fillRect(0, 0, getWidth(), theme::headerHeight);
    g.setColour(theme::panel.withAlpha(0.52f));
    g.fillRect(0, theme::headerHeight, getWidth(), theme::toolbarHeight);
    g.setColour(theme::lineSoft);
    g.drawHorizontalLine(theme::headerHeight - 1, 0, (float)getWidth());
    g.drawHorizontalLine(theme::headerHeight + theme::toolbarHeight, 0, (float)getWidth());
    g.drawHorizontalLine(getHeight() - theme::footerHeight, 0, (float)getWidth());

    auto logo = juce::Rectangle<float>(22, 18, 30, 30);
    g.setColour(theme::accent.withAlpha(0.12f));
    g.fillRoundedRectangle(logo, 7.0f);
    drawIcon(g, Icon::link, logo.reduced(6), theme::accent, 1.5f);
    g.setColour(theme::text);
    g.setFont(theme::font(16.0f, true));
    g.drawText("PATCHLINK", 62, 16, 112, 22, juce::Justification::centredLeft);
    g.setColour(theme::faint);
    g.setFont(theme::mono(8.5f));
    g.drawText("BY LEVE", 63, 37, 100, 16, juce::Justification::centredLeft);

    g.setColour(theme::accent);
    g.fillEllipse(23.0f, getHeight() - 20.0f, 5.0f, 5.0f);
}

void PatchLinkEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(theme::headerHeight).reduced(16, 13);
    header.removeFromLeft(154);
    simpleButton.setBounds(header.removeFromLeft(78));
    header.removeFromLeft(4);
    patcherButton.setBounds(header.removeFromLeft(84));
    header.removeFromLeft(4);
    presetsButton.setBounds(header.removeFromLeft(84));
    header.removeFromLeft(6);
    shareButton.setBounds(header.removeFromRight(90));
    header.removeFromRight(4);
    codeButton.setBounds(header.removeFromRight(98));
    header.removeFromRight(4);
    libraryButton.setBounds(header.removeFromRight(88));
    header.removeFromRight(4);
    langButton.setBounds(header.removeFromRight(42));
    header.removeFromRight(4);
    themeButton.setBounds(header.removeFromRight(72));

    auto toolbar = r.removeFromTop(theme::toolbarHeight).reduced(22, 10);
    addButton.setBounds(toolbar.removeFromRight(126));
    toolbar.removeFromRight(6);
    dragDawButton.setBounds(toolbar.removeFromRight(130));
    toolbar.removeFromRight(6);
    savePresetButton.setBounds(toolbar.removeFromRight(78));
    savePresetButton.setVisible(toolbar.getWidth() > 260);
    toolbar.removeFromRight(4);
    exportButton.setBounds(toolbar.removeFromRight(84));
    exportButton.setVisible(toolbar.getWidth() > 210);
    toolbar.removeFromRight(4);
    importButton.setBounds(toolbar.removeFromRight(84));
    importButton.setVisible(toolbar.getWidth() > 160);
    patchName.setBounds(toolbar.withTrimmedRight(16));

    auto footer = r.removeFromBottom(theme::footerHeight).reduced(36, 0);
    systemInfo.setBounds(footer.removeFromRight(225));
    footer.removeFromRight(16);
    chainInfo.setBounds(footer.removeFromRight(160));
    statusLabel.setBounds(footer);

    chainView.setBounds(r);
    patcherView.setBounds(r);
    if (presetHubView != nullptr) presetHubView->setBounds(r);
    if (overlay != nullptr) overlay->setBounds(getLocalBounds());
    toast.setCentrePosition(getWidth() / 2, getHeight() - theme::footerHeight - 32);
}

bool PatchLinkEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        const auto c = key.getTextCharacter();
        if (c == 'f' || c == 'F') { addButton.triggerClick(); return true; }
        if (c == 'i' || c == 'I') { doImport(); return true; }
        if (c == 'e' || c == 'E') { doExport(); return true; }
        if (c == 's' || c == 'S') { if (presetHubView) presetHubView->openSaveCurrentDialog(); return true; }
    }
    return false;
}

void PatchLinkEditor::dismissOverlay()
{
    if (overlay == nullptr) return;
    auto* doomed = overlay.release();
    doomed->setVisible(false);
    juce::MessageManager::callAsync([doomed] { delete doomed; });
}

void PatchLinkEditor::openPluginBrowser(int x, int y)
{
    dismissOverlay();
    auto* browser = new PluginBrowser(proc.getKnownList().getTypes());
    overlay.reset(browser);
    browser->onDismiss = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] { if (safe) safe->dismissOverlay(); };
    browser->onScanRequested = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
        if (safe) { safe->dismissOverlay(); safe->openScanDialog(); }
    };
    browser->onPluginChosen = [safe = juce::Component::SafePointer<PatchLinkEditor>(this), x, y](const auto& description) {
        if (!safe) return;
        safe->dismissOverlay();
        safe->setStatus(i18n::tr("Loading ", "Chargement de ") + description.name + "...");
        safe->proc.addPluginAsync(description, x, y, [safe, name = description.name](const juce::String& error) {
            if (!safe) return;
            if (error.isNotEmpty())
            {
                safe->setStatus(i18n::tr("Plugin could not be loaded", "Impossible de charger le plugin"));
                safe->showToast(error, true);
            }
            else
            {
                safe->setStatus(name + i18n::tr(" is ready", " prêt"));
                safe->showToast(name + i18n::tr(" added to your chain", " ajouté à la chaîne"));
            }
            safe->refreshViews();
        });
    };
    browser->setBounds(getLocalBounds());
    addAndMakeVisible(browser);
    browser->grabKeyboardFocus();
}

void PatchLinkEditor::openShareDialog(bool publish, const juce::var& customPatch)
{
    dismissOverlay();
    auto mode = publish ? ShareDialog::Mode::publish : ShareDialog::Mode::openCode;
    const auto activePatch = publish ? (customPatch.isObject() ? customPatch : getActiveOrStagedPatchVar()) : juce::var();
    auto* dialog = new ShareDialog(mode, activePatch);
    overlay.reset(dialog);
    dialog->onDismiss = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] { if (safe) safe->dismissOverlay(); };

    dialog->onPublish = [safe = juce::Component::SafePointer<PatchLinkEditor>(this),
                         dialogSafe = juce::Component::SafePointer<ShareDialog>(dialog),
                         patch = activePatch, base = apiBase](const juce::String& title,
                            const juce::String& author, const juce::String& description, bool isPublic) {
        if (!safe || !dialogSafe) return;
        dialogSafe->setBusy(true, i18n::tr("Publishing your chain to PatchDrop...",
                                           "Publication de votre chaîne sur PatchDrop..."));
        std::thread([safe, dialogSafe, patch, title, author, description, isPublic, base]() mutable {
            juce::String error;
            auto code = ShareClient::publish(patch, title, author, description, base, error, isPublic);
            juce::MessageManager::callAsync([safe, dialogSafe, code, error]() mutable {
                if (dialogSafe)
                {
                    dialogSafe->setBusy(false);
                    if (code.isNotEmpty())
                    {
                        if (safe) safe->lastShareCode = code;
                        juce::SystemClipboard::copyTextToClipboard(code);
                        dialogSafe->publishSucceeded(code);
                    }
                    else
                    {
                        dialogSafe->operationFailed(error.isNotEmpty()
                            ? error
                            : i18n::tr("PatchDrop could not publish this chain.",
                                       "PatchDrop n'a pas pu publier cette chaîne."));
                    }
                }
                else if (safe)
                {
                    if (code.isNotEmpty())
                    {
                        safe->lastShareCode = code;
                        juce::SystemClipboard::copyTextToClipboard(code);
                        safe->showToast(i18n::tr("Published as ", "Publié sous ") + code + i18n::tr(" - code copied", " - code copié"));
                    }
                    else safe->showToast(error.isNotEmpty() ? error : i18n::tr("Publishing failed", "Échec de publication"), true);
                }
            });
        }).detach();
    };

    dialog->onOpenCode = [safe = juce::Component::SafePointer<PatchLinkEditor>(this),
                          dialogSafe = juce::Component::SafePointer<ShareDialog>(dialog),
                          base = apiBase](const juce::String& code) {
        if (!safe || !dialogSafe) return;
        dialogSafe->setBusy(true, i18n::tr("Fetching ", "Chargement de ") + code + "...");
        std::thread([safe, dialogSafe, code, base]() mutable {
            juce::var patch;
            juce::String title, error;
            const auto ok = ShareClient::fetch(code, base, patch, title, error);
            juce::MessageManager::callAsync([safe, dialogSafe, code, patch, title, error, ok]() mutable {
                if (!safe) return;
                if (!ok)
                {
                    if (dialogSafe) dialogSafe->operationFailed(error.isNotEmpty() ? error : i18n::tr("Code not found.", "Code introuvable."));
                    else safe->showToast(error.isNotEmpty() ? error : i18n::tr("Code not found", "Code introuvable"), true);
                    return;
                }
                if (dialogSafe && safe->overlay.get() == dialogSafe.getComponent())
                {
                    dialogSafe->codeFetched(code, title, patch);
                }
                else
                {
                    safe->loadPatchIntoProcessor(patch, title.isNotEmpty() ? title : code);
                }
            });
        }).detach();
    };

    dialog->onLoadFetched = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)](const juce::var& patch,
                                                                                        const juce::String& title) {
        if (!safe) return;
        safe->dismissOverlay();
        safe->loadPatchIntoProcessor(patch, title);
    };

    dialog->onStageFetched = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)](const juce::var& patch,
                                                                                         const juce::String& title) {
        if (!safe) return;
        safe->dismissOverlay();
        safe->stagePatch(patch, title,
                         i18n::tr("Staged \"", "Chaîne \"") + title
                         + i18n::tr("\" (zero-load) - drag button to your DAW",
                                    "\" prête (sans chargement) - glissez vers votre DAW"));
    };

    dialog->onDragFetchedToDaw = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)](const juce::var& patch,
                                                                                             const juce::String& title,
                                                                                             bool startDrag) {
        if (!safe) return;
        safe->stagePatch(patch, title);
        if (startDrag)
            safe->startExternalDawDrag(patch, -1);
        else
            safe->showFormatPickerMenu(patch, -1, false);
    };

    dialog->setBounds(getLocalBounds());
    addAndMakeVisible(dialog);
    dialog->grabKeyboardFocus();
}

void PatchLinkEditor::loadPatchIntoProcessor(const juce::var& patch, const juce::String& label)
{
    clearStagedPatch();
    pluginWindows.clear();
    setStatus(i18n::tr("Loading ", "Chargement de ") + label + "...");
    proc.loadFromVarAsync(patch, [safe = juce::Component::SafePointer<PatchLinkEditor>(this), label](juce::StringArray missing, juce::String loadError) {
        if (!safe) return;
        if (loadError.isNotEmpty())
        {
            safe->setStatus(i18n::tr("Could not open ", "Impossible d'ouvrir ") + label);
            safe->showToast(loadError, true);
            return;
        }
        safe->setStatus(label + i18n::tr(" is ready", " prêt"));
        safe->refreshViews();
        safe->showMissingPlugins(missing, label);
    });
}

void PatchLinkEditor::doImport()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        i18n::tr("Open a PatchLink chain or DAW preset", "Ouvrir une chaîne PatchLink ou un preset DAW"),
        juce::File(),
        "*.patchlink;*.fst;*.rfxchain;*.rpp;*.adg;*.adv;*.als;*.vstpreset;*.bwpreset;*.fxchainpreset;*.fxchain;*.multipreset");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), chooser](const juce::FileChooser&) mutable {
            if (!safe) return;
            const auto file = chooser->getResult();
            if (!file.existsAsFile()) return;
            safe->importFiles({ file.getFullPathName() }, -1, -1);
        });
}

void PatchLinkEditor::doImportIntoLibrary(const juce::String& targetFolder)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        i18n::tr("Import presets into Local Library", "Importer des presets dans la bibliothèque"),
        juce::File(),
        "*.patchlink;*.fst;*.rfxchain;*.rpp;*.adg;*.adv;*.als;*.vstpreset;*.bwpreset;*.fxchainpreset;*.fxchain;*.multipreset");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), chooser, targetFolder](const juce::FileChooser&) mutable {
            if (!safe) return;
            juce::StringArray paths;
            for (const auto& f : chooser->getResults())
                if (f.existsAsFile())
                    paths.add(f.getFullPathName());
            if (!paths.isEmpty())
                safe->importFilesIntoPresetDb(paths, targetFolder);
        });
}

void PatchLinkEditor::importFilesIntoPresetDb(const juce::StringArray& files, const juce::String& targetFolder)
{
    auto cachedFiles = std::move(droppedFileCache);
    droppedFileCache.clear();

    int imported = 0;
    int skipped = 0;
    juce::String lastId;
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (!file.hasFileExtension(".fst;.patchlink;.rfxchain;.rpp;.adg;.adv;.als;.vstpreset;.bwpreset;.fxchainpreset;.fxchain;.multipreset"))
            continue;

        juce::MemoryBlock rawBlock;
        if (const auto it = cachedFiles.find(file.getFullPathName()); it != cachedFiles.end() && it->second.getSize() > 0)
            rawBlock = it->second;
        else if (file.existsAsFile())
            file.loadFileAsData(rawBlock);

        if (rawBlock.getSize() == 0)
        {
            ++skipped;
            continue;
        }

        juce::var patchVar;
        juce::String title = file.getFileNameWithoutExtension();
        juce::String dawTag;
        if (file.hasFileExtension(".patchlink"))
        {
            patchVar = juce::JSON::parse(rawBlock.toString());
            if (auto* o = patchVar.getDynamicObject())
                if (o->getProperty("name").toString().trim().isNotEmpty())
                    title = o->getProperty("name").toString().trim();
        }
        else
        {
            auto parsed = flstudio::parseAnyDawFile(file, rawBlock.getData(), rawBlock.getSize());
            dawexport::enrichParsedFstFromCache(parsed);
            if (!parsed.valid || parsed.plugins.empty())
            {
                ++skipped;
                continue;
            }
            dawTag = parsed.sourceDaw;
            if (parsed.trackName.trim().isNotEmpty())
                title = parsed.trackName.trim();
            patchVar = flstudio::parsedFstToPatchVar(parsed, &proc.getKnownList());
        }

        if (!patchVar.isObject())
        {
            ++skipped;
            continue;
        }

        patchdb::LocalPreset meta;
        meta.title = title;
        meta.folder = targetFolder;
        if (dawTag.isNotEmpty())
            meta.tags.add("#" + dawTag.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789"));
        const auto savedId = presetDb.savePreset(patchVar, std::move(meta), false);
        if (savedId.isNotEmpty())
        {
            lastId = savedId;
            ++imported;
        }
        else
        {
            ++skipped;
        }
    }

    if (presetHubView != nullptr)
        presetHubView->refreshLocal(imported == 0);

    if (imported > 0)
    {
        const auto msg = i18n::tr("Imported ", "Importé ") + juce::String(imported)
                       + i18n::tr(" preset(s) into Local Library", " preset(s) dans la bibliothèque");
        setStatus(msg);
        showToast(msg);
    }
    else if (skipped > 0)
    {
        showToast(i18n::tr("No supported VST3 presets found in dropped file(s)",
                           "Aucun preset VST3 compatible dans les fichiers"), true);
    }
}

void PatchLinkEditor::doExport()
{
    const auto patch = getActiveOrStagedPatchVar();
    if (!patch.isObject())
    {
        showToast(i18n::tr("No chain to export", "Aucune chaîne à exporter"), true);
        return;
    }
    auto chooser = std::make_shared<juce::FileChooser>(
        i18n::tr("Save a PatchLink chain", "Enregistrer une chaîne PatchLink"),
        juce::File(), "*.patchlink");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), chooser, patch](const juce::FileChooser&) mutable {
            if (!safe) return;
            auto file = chooser->getResult();
            if (file.getFullPathName().isEmpty()) return;
            if (file.getFileExtension().isEmpty()) file = file.withFileExtension("patchlink");
            if (!file.replaceWithText(juce::JSON::toString(patch, true)))
            {
                safe->showToast(i18n::tr("Write failed", "Erreur d'écriture"), true);
                return;
            }
            safe->setStatus(i18n::tr("Exported ", "Exporté : ") + file.getFileName());
            safe->showToast(i18n::tr("Chain saved to ", "Chaîne enregistrée sous ") + file.getFileName());
        });
}

void PatchLinkEditor::confirmClearChain()
{
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        i18n::tr("Clear this chain?", "Vider cette chaîne ?"),
        i18n::tr("This removes every hosted plugin and its connections from the current chain.",
                 "Cela retire tous les plugins chargés et leurs câbles de la chaîne actuelle."),
        i18n::tr("Clear chain", "Vider la chaîne"),
        i18n::tr("Cancel", "Annuler"),
        this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<PatchLinkEditor>(this)](int result) {
            if (safe && result != 0)
            {
                safe->pluginWindows.clear();
                safe->proc.clearChain();
                safe->setStatus(i18n::tr("Chain cleared", "Chaîne vidée"));
            }
        }));
}

void PatchLinkEditor::openScanDialog()
{
    if (scanWindow == nullptr)
    {
        auto* window = new ScanManagerWindow();
        scanWindow.reset(window);
        window->setLookAndFeel(&lookAndFeel);
        window->setContentOwned(new ScanDialog(proc.getScanManager()), true);
        window->setResizable(true, true);
        window->setResizeLimits(690, 560, 1500, 1100);
    }
    scanWindow->centreAroundComponent(this, juce::jmin(900, getWidth() + 40), juce::jmin(720, getHeight() + 40));
    scanWindow->setVisible(true);
    scanWindow->toFront(true);
    scanWindow->setLookAndFeel(&lookAndFeel);
}

void PatchLinkEditor::openPluginWindow(juce::AudioProcessorGraph::NodeID id)
{
    auto* processor = proc.getNodeProcessor(id);
    if (processor == nullptr) { showToast("This plugin is no longer in the chain", true); return; }
    for (auto* window : pluginWindows)
        if (window->nodeId == id) { window->toFront(true); return; }

    juce::String name = "Plugin";
    if (auto* instance = dynamic_cast<juce::AudioPluginInstance*>(processor)) name = instance->getPluginDescription().name;
    auto* window = new HostedPluginWindow(name + " / PatchLink", id, processor);
    window->onClosed = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)](HostedPluginWindow* closing) {
        if (safe) safe->closePluginWindow(closing);
    };
    pluginWindows.add(window);
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->setVisible(true);
}

void PatchLinkEditor::closePluginWindow(HostedPluginWindow* window)
{
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PatchLinkEditor>(this), window] {
        if (safe) safe->pluginWindows.removeObject(window);
    });
}

void PatchLinkEditor::closeWindowsForNode(juce::AudioProcessorGraph::NodeID id)
{
    for (int i = pluginWindows.size(); --i >= 0;)
        if (pluginWindows[i]->nodeId == id)
            closePluginWindow(pluginWindows[i]);
}

void PatchLinkEditor::showMissingPlugins(const juce::StringArray& missing, const juce::String& title)
{
    if (missing.isEmpty())
    {
        showToast(i18n::tr("Everything in this chain was restored", "Toute la chaîne a été restaurée"));
        return;
    }
    juce::String extraHint = proc.getKnownList().getNumTypes() == 0
        ? i18n::tr("\n\nTip: Your VST3 library has not been scanned yet! Click 'Choose VST Folders / Rescan' below to pick your VST folders and scan.",
                   "\n\nAstuce : Votre bibliothèque VST3 n'a pas encore été scannée ! Cliquez sur « Dossiers VST / Scanner » ci-dessous.")
        : i18n::tr("\n\nIf these plugins are installed in a custom folder, click 'Choose VST Folders / Rescan' to add that folder and rescan.",
                   "\n\nSi ces plugins sont dans un dossier personnalisé, cliquez sur « Dossiers VST / Scanner » pour l'ajouter et scanner.");
    juce::String message = i18n::tr("The available plugins and settings were restored.\n\nCould not load or skipped:\n\n- ",
                                    "Les plugins disponibles et leurs réglages ont été restaurés.\n\nNon chargés ou ignorés :\n\n- ")
                         + missing.joinIntoString("\n- ")
                         + extraHint;
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::InfoIcon,
        title,
        message,
        i18n::tr("Choose VST Folders / Rescan", "Dossiers VST / Scanner"),
        i18n::tr("Close", "Fermer"),
        this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<PatchLinkEditor>(this)](int res) {
            if (safe && res != 0)
                safe->openScanDialog();
        }));
}

namespace
{
bool isSupportedDropFile(const juce::File& file)
{
    return file.hasFileExtension(".fst;.patchlink;.rfxchain;.rpp;.adg;.adv;.als;.vstpreset;.bwpreset;.fxchainpreset;.fxchain;.multipreset");
}

juce::String describeDraggedFiles(const juce::StringArray& files)
{
    int fstCount = 0;
    int reaperCount = 0;
    int abletonCount = 0;
    int vst3PresetCount = 0;
    int patchCount = 0;
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (f.hasFileExtension(".fst")) ++fstCount;
        else if (f.hasFileExtension(".rfxchain;.rpp")) ++reaperCount;
        else if (f.hasFileExtension(".adg;.adv;.als")) ++abletonCount;
        else if (f.hasFileExtension(".vstpreset;.bwpreset;.fxchainpreset;.fxchain;.multipreset")) ++vst3PresetCount;
        else if (f.hasFileExtension(".patchlink")) ++patchCount;
    }
    const int total = fstCount + reaperCount + abletonCount + vst3PresetCount + patchCount;
    if (total == fstCount && fstCount > 0)
        return fstCount > 1
            ? i18n::tr("Drop FL Studio .fst presets to add to PatchLink", "Déposer les presets FL Studio .fst dans PatchLink")
            : i18n::tr("Drop FL Studio .fst preset (single effect or mixer track)", "Déposer le preset FL Studio .fst (effet ou piste mixer)");
    if (total == reaperCount && reaperCount > 0)
        return i18n::tr("Drop REAPER .RfxChain / .rpp FX chain into PatchLink", "Déposer la chaîne REAPER .RfxChain / .rpp dans PatchLink");
    if (total == abletonCount && abletonCount > 0)
        return i18n::tr("Drop Ableton Live .adg / .adv rack into PatchLink", "Déposer le rack Ableton Live .adg / .adv dans PatchLink");
    if (total == vst3PresetCount && vst3PresetCount > 0)
        return vst3PresetCount > 1
            ? i18n::tr("Drop VST3 preset / DAW FX chain files into PatchLink", "Déposer les presets VST3 / chaînes DAW dans PatchLink")
            : i18n::tr("Drop VST3 preset / DAW FX chain into PatchLink", "Déposer le preset VST3 / chaîne DAW dans PatchLink");
    if (total == patchCount && patchCount > 0)
        return i18n::tr("Drop .patchlink file to open chain", "Déposer le fichier .patchlink pour ouvrir la chaîne");
    return i18n::tr("Drop DAW FX chains (.fst, .RfxChain, .adg, .vstpreset, .patchlink)",
                    "Déposer une chaîne DAW (.fst, .RfxChain, .adg, .vstpreset, .patchlink)");
}

juce::String extractPatchCodeFromDraggedText(const juce::String& rawText)
{
    const auto trimmed = rawText.trim();
    if (trimmed.isEmpty())
        return {};

    const int apiIdx = trimmed.indexOfIgnoreCase("/api/p/");
    if (apiIdx >= 0)
    {
        const auto rest = trimmed.substring(apiIdx + 7);
        juce::String code;
        for (int i = 0; i < rest.length() && code.length() < 8; ++i)
        {
            const auto c = rest[i];
            if (juce::CharacterFunctions::isLetterOrDigit(c))
                code << juce::CharacterFunctions::toUpperCase(c);
            else
                break;
        }
        if (code.length() >= 4 && code.length() <= 8)
            return code;
    }

    if (trimmed.startsWithIgnoreCase("patchdrop:"))
    {
        const auto code = trimmed.substring(10).trim().toUpperCase().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZ23456789");
        if (code.length() >= 4 && code.length() <= 8)
            return code;
    }

    const auto clean = trimmed.toUpperCase();
    if (clean.length() == 6 && clean.containsOnly("ABCDEFGHJKMNPQRSTUVWXYZ23456789"))
        return clean;

    return {};
}
} // namespace

void PatchLinkEditor::paintOverChildren(juce::Graphics& g)
{
    if (!isDraggingFile) return;

    g.setColour(theme::background.withAlpha(0.68f));
    g.fillRect(getLocalBounds());

    const auto area = juce::Rectangle<int>(
        16, theme::headerHeight + theme::toolbarHeight + 6,
        juce::jmax(120, getWidth() - 32),
        juce::jmax(100, getHeight() - theme::headerHeight - theme::toolbarHeight - theme::footerHeight - 12));
    const auto rf = area.toFloat();

    g.setColour(theme::panel.withAlpha(0.94f));
    g.fillRoundedRectangle(rf, theme::radius);

    juce::Path borderPath;
    borderPath.addRoundedRectangle(rf.reduced(2.0f), theme::radius);
    juce::Path dashed;
    const float dashes[] = { 8.0f, 6.0f };
    juce::PathStrokeType(1.8f).createDashedStroke(dashed, borderPath, dashes, 2);
    g.setColour(theme::accent);
    g.fillPath(dashed);

    const auto cx = area.getCentreX();
    const auto cy = area.getCentreY();
    drawIcon(g, Icon::importFile, juce::Rectangle<float>((float)cx - 18.0f, (float)cy - 44.0f, 36.0f, 36.0f),
             theme::accent, 1.8f);

    g.setColour(theme::text);
    g.setFont(theme::font(17.0f, true));
    g.drawText(i18n::tr("Drop into PatchLink", "Déposer dans PatchLink"),
               area.getX() + 20, cy + 2, area.getWidth() - 40, 26,
               juce::Justification::centred);

    g.setColour(theme::muted);
    g.setFont(theme::font(12.5f));
    g.drawText(dragDropHint, area.getX() + 20, cy + 30, area.getWidth() - 40, 22,
               juce::Justification::centred);
}

bool PatchLinkEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedDropFile(juce::File(path)))
            return true;
    return false;
}

void PatchLinkEditor::cacheDraggedFiles(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (!isSupportedDropFile(file) || !file.existsAsFile())
            continue;

        const auto fileSize = file.getSize();
        if (fileSize <= 0)
            continue;

        const auto key = file.getFullPathName();
        auto& cached = droppedFileCache[key];
        if (static_cast<juce::int64>(cached.getSize()) != fileSize)
        {
            juce::MemoryBlock mb;
            if (file.loadFileAsData(mb) && mb.getSize() > 0)
                cached = std::move(mb);
        }
    }
}

void PatchLinkEditor::fileDragEnter(const juce::StringArray& files, int, int)
{
    isDraggingFile = true;
    dragDropHint = describeDraggedFiles(files);
    cacheDraggedFiles(files);
    repaint();
}

void PatchLinkEditor::fileDragMove(const juce::StringArray& files, int, int)
{
    cacheDraggedFiles(files);
    const auto nextHint = describeDraggedFiles(files);
    if (!isDraggingFile || dragDropHint != nextHint)
    {
        isDraggingFile = true;
        dragDropHint = nextHint;
        repaint();
    }
}

void PatchLinkEditor::fileDragExit(const juce::StringArray&)
{
    droppedFileCache.clear();
    if (!isDraggingFile) return;
    isDraggingFile = false;
    repaint();
}

void PatchLinkEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    isDraggingFile = false;
    repaint();
    if (mode == Mode::presets && presetHubView != nullptr && presetHubView->isLocalScope())
    {
        importFilesIntoPresetDb(files, presetHubView->getActiveLocalFolderForDrop());
        return;
    }
    importFiles(files, x, y);
}

bool PatchLinkEditor::isInterestedInTextDrag(const juce::String& text)
{
    return extractPatchCodeFromDraggedText(text).isNotEmpty();
}

void PatchLinkEditor::textDragEnter(const juce::String& text, int, int)
{
    const auto code = extractPatchCodeFromDraggedText(text);
    if (code.isEmpty())
        return;
    isDraggingFile = true;
    dragDropHint = i18n::tr("Drop preset ", "Déposer le preset ") + code + i18n::tr(" from PatchDrop browser", " depuis PatchDrop");
    repaint();
}

void PatchLinkEditor::textDragMove(const juce::String& text, int, int)
{
    const auto code = extractPatchCodeFromDraggedText(text);
    if (code.isEmpty())
        return;
    const auto nextHint = i18n::tr("Drop preset ", "Déposer le preset ") + code + i18n::tr(" from PatchDrop browser", " depuis PatchDrop");
    if (!isDraggingFile || dragDropHint != nextHint)
    {
        isDraggingFile = true;
        dragDropHint = nextHint;
        repaint();
    }
}

void PatchLinkEditor::textDragExit(const juce::String&)
{
    if (!isDraggingFile)
        return;
    isDraggingFile = false;
    repaint();
}

void PatchLinkEditor::textDropped(const juce::String& text, int, int)
{
    isDraggingFile = false;
    repaint();

    const auto code = extractPatchCodeFromDraggedText(text);
    if (code.isEmpty())
        return;

    setStatus(i18n::tr("Fetching ", "Récupération de ") + code + i18n::tr(" from PatchDrop...", " depuis PatchDrop..."));
    auto safe = juce::Component::SafePointer<PatchLinkEditor>(this);
    const auto base = apiBase;
    std::thread([safe, code, base]() mutable {
        juce::var patch;
        juce::String title, error;
        const auto ok = ShareClient::fetch(code, base, patch, title, error);
        juce::MessageManager::callAsync([safe, code, patch, title, error, ok]() mutable {
            if (!safe)
                return;
            if (!ok)
            {
                safe->setStatus(i18n::tr("Could not fetch ", "Code introuvable : ") + code);
                safe->showToast(error.isNotEmpty() ? error : (i18n::tr("Code ", "Code ") + code + i18n::tr(" not found", " introuvable")), true);
                return;
            }

            const auto label = title.isNotEmpty() ? title : code;
            if (safe->proc.getLoadedNodes().empty())
            {
                auto stagedPatch = patch;
                if (auto* o = stagedPatch.getDynamicObject())
                    if (title.isNotEmpty())
                        o->setProperty("name", title);

                safe->showEmptyDropModal(stagedPatch, label, {}, [safe, patch, label] {
                    if (safe) safe->loadPatchIntoProcessor(patch, label);
                });
            }
            else
            {
                safe->loadPatchIntoProcessor(patch, label);
            }
        });
    }).detach();
}

dawexport::TargetFormat PatchLinkEditor::effectiveExportFormat() const
{
    if (selectedExportFormat != dawexport::TargetFormat::autoDetect)
        return selectedExportFormat;
    if (!hostDetection.isStandalone && hostDetection.detectedFormat != dawexport::TargetFormat::autoDetect)
        return hostDetection.detectedFormat;
    return dawexport::TargetFormat::flStudioFst;
}

void PatchLinkEditor::updateExportButtonState()
{
    if (hostDetection.isStandalone && selectedExportFormat == dawexport::TargetFormat::autoDetect)
    {
        dragDawButton.setButtonText(i18n::tr("Drag to DAW", "Glisser DAW"));
        dragDawButton.setTooltip(i18n::tr("Standalone mode: click to pick DAW format (FL / REAPER / Ableton / VST3), then drag out to your DAW",
                                          "Mode autonome : cliquez pour choisir le format DAW (FL / REAPER / Ableton / VST3), puis glissez"));
    }
    else
    {
        const auto fmt = effectiveExportFormat();
        dragDawButton.setButtonText(i18n::tr("Drag .", "Glisser .") + dawexport::formatExtension(fmt));
        const auto src = hostDetection.isStandalone
                             ? i18n::tr("Format: ", "Format : ")
                             : (i18n::tr("Auto-detected ", "Détecté : ") + hostDetection.hostName + ": ");
        dragDawButton.setTooltip(src + dawexport::formatLabel(fmt)
                                 + i18n::tr(" - drag onto your DAW, or click to change format / save",
                                            " - glissez vers votre DAW, ou cliquez pour changer de format / enregistrer"));
    }
}

juce::var PatchLinkEditor::getActiveOrStagedPatchVar()
{
    if (!proc.getLoadedNodes().empty())
        return proc.graphToVar();
    if (stagedPatchVar.isObject())
        return stagedPatchVar;
    return proc.graphToVar();
}

void PatchLinkEditor::stagePatch(const juce::var& patchVar, const juce::String& title, const juce::String& toastMsg)
{
    stagedPatchVar = patchVar;
    stagedPatchTitle = title.trim();
    if (stagedPatchTitle.isEmpty())
        if (auto* o = patchVar.getDynamicObject())
            stagedPatchTitle = o->getProperty("name").toString();
    if (stagedPatchTitle.isNotEmpty())
        proc.setPatchName(stagedPatchTitle);
    refreshViews();
    if (toastMsg.isNotEmpty())
    {
        setStatus(toastMsg);
        showToast(toastMsg);
    }
}

void PatchLinkEditor::clearStagedPatch()
{
    stagedPatchVar = juce::var();
    stagedPatchTitle.clear();
}

void PatchLinkEditor::showFormatPickerMenu(const juce::var& customPatch, int singleSlotIndex, bool fromDragAttempt)
{
    const auto patch = customPatch.isObject() ? customPatch : getActiveOrStagedPatchVar();
    const bool hasNodes = [&]() {
        if (auto* o = patch.getDynamicObject())
            if (auto* arr = o->getProperty("nodes").getArray())
                return !arr->isEmpty();
        return false;
    }();

    juce::PopupMenu menu;
    menu.addSectionHeader(hostDetection.isStandalone
                              ? i18n::tr("Standalone export format", "Format d'export (Autonome)")
                              : (i18n::tr("Detected host: ", "DAW détecté : ") + hostDetection.hostName));
    menu.addItem(1, "FL Studio (.fst)", true, selectedExportFormat == dawexport::TargetFormat::flStudioFst);
    menu.addItem(2, "REAPER (.RfxChain)", true, selectedExportFormat == dawexport::TargetFormat::reaperRfxChain);
    menu.addItem(3, "Ableton Live Rack (.adg)", true, selectedExportFormat == dawexport::TargetFormat::abletonAdg);
    menu.addItem(4, "VST3 Preset (.vstpreset)", true, selectedExportFormat == dawexport::TargetFormat::vstPreset);
    menu.addItem(5, "PatchLink (.patchlink)", true, selectedExportFormat == dawexport::TargetFormat::patchlinkFile);
    menu.addSeparator();
    menu.addItem(10,
                 singleSlotIndex >= 0 ? i18n::tr("Save single preset file...", "Enregistrer ce preset...")
                                      : i18n::tr("Save DAW preset file...", "Enregistrer le preset DAW..."),
                 hasNodes);

    auto opts = juce::PopupMenu::Options();
    if (!customPatch.isObject() && singleSlotIndex < 0)
        opts = opts.withTargetComponent(&dragDawButton);

    menu.showMenuAsync(opts,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), patch, singleSlotIndex, fromDragAttempt](int result) {
            if (!safe || result == 0) return;
            if (result >= 1 && result <= 5)
            {
                const dawexport::TargetFormat map[] = {
                    dawexport::TargetFormat::flStudioFst,
                    dawexport::TargetFormat::reaperRfxChain,
                    dawexport::TargetFormat::abletonAdg,
                    dawexport::TargetFormat::vstPreset,
                    dawexport::TargetFormat::patchlinkFile
                };
                safe->selectedExportFormat = map[result - 1];
                safe->updateExportButtonState();
                const auto lbl = dawexport::formatLabel(safe->selectedExportFormat);
                safe->setStatus(i18n::tr("DAW target: ", "Cible DAW : ") + lbl);
                safe->showToast(fromDragAttempt
                                    ? (i18n::tr("Format set to ", "Format choisi : ") + lbl
                                       + i18n::tr(" - now drag button into your DAW!", " - glissez maintenant vers votre DAW !"))
                                    : (i18n::tr("Format set to ", "Format choisi : ") + lbl
                                       + i18n::tr(" (drag button to DAW)", " (glisser vers le DAW)")));
            }
            else if (result == 10)
            {
                safe->saveDawPresetToDisk(patch, safe->effectiveExportFormat(), singleSlotIndex);
            }
        });
}

void PatchLinkEditor::startExternalDawDrag(const juce::var& customPatch, int singleSlotIndex)
{
    const auto patch = customPatch.isObject() ? customPatch : getActiveOrStagedPatchVar();
    const bool hasNodes = [&]() {
        if (auto* o = patch.getDynamicObject())
            if (auto* arr = o->getProperty("nodes").getArray())
                return !arr->isEmpty();
        return false;
    }();
    if (!hasNodes)
    {
        showToast(i18n::tr("Add, drop, or open a chain first before dragging to your DAW",
                           "Ajoutez, déposez ou ouvrez une chaîne avant de glisser vers votre DAW"), true);
        return;
    }

    if (hostDetection.isStandalone && selectedExportFormat == dawexport::TargetFormat::autoDetect)
    {
        showFormatPickerMenu(customPatch, singleSlotIndex, true);
        return;
    }

    const auto fmt = effectiveExportFormat();
    juce::String err;
    const auto files = dawexport::prepareDragFiles(patch, fmt, err, singleSlotIndex);
    if (files.isEmpty())
    {
        showToast(err.isNotEmpty() ? err : i18n::tr("Could not prepare DAW preset", "Impossible de préparer le fichier DAW"), true);
        return;
    }

    setStatus(i18n::tr("Dragging ", "Glissement ") + dawexport::formatLabel(fmt) + i18n::tr(" to DAW...", " vers le DAW..."));
    performExternalDragDropOfFiles(files, false, this,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
            if (safe) safe->setStatus(i18n::tr("Ready", "Prêt"));
        });
}

void PatchLinkEditor::saveDawPresetToDisk(const juce::var& customPatch, dawexport::TargetFormat fmt, int singleSlotIndex)
{
    const auto patch = customPatch.isObject() ? customPatch : getActiveOrStagedPatchVar();
    const auto ext = dawexport::formatExtension(fmt);
    juce::String defaultName = "PatchLink";
    if (auto* root = patch.getDynamicObject())
    {
        const auto n = root->getProperty("name").toString().trim();
        if (n.isNotEmpty()) defaultName = n;
        if (singleSlotIndex >= 0)
            if (auto* arr = root->getProperty("nodes").getArray())
                if (singleSlotIndex < arr->size())
                    defaultName = dawexport::detail::extractNodeInfo(arr->getReference(singleSlotIndex)).name;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        i18n::tr("Save ", "Enregistrer ") + dawexport::formatLabel(fmt),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(
            dawexport::detail::sanitizeFileBase(defaultName, "PatchLink") + "." + ext),
        "*." + ext);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), chooser, patch, fmt, singleSlotIndex, ext](const juce::FileChooser&) mutable {
            if (!safe) return;
            auto target = chooser->getResult();
            if (target.getFullPathName().isEmpty()) return;
            if (target.getFileExtension().isEmpty()) target = target.withFileExtension(ext);

            juce::String err;
            const auto generated = dawexport::prepareDragFiles(patch, fmt, err, singleSlotIndex);
            if (generated.isEmpty())
            {
                safe->showToast(err.isNotEmpty() ? err : i18n::tr("Export failed", "Échec de l'export"), true);
                return;
            }
            if (generated.size() == 1)
            {
                const juce::File src(generated[0]);
                if (!src.copyFileTo(target))
                {
                    safe->showToast(i18n::tr("Could not write ", "Impossible d'écrire ") + target.getFileName(), true);
                    return;
                }
                safe->setStatus(i18n::tr("Saved ", "Enregistré : ") + target.getFileName());
                safe->showToast(i18n::tr("Saved ", "Enregistré : ") + target.getFileName());
                return;
            }
            const auto dir = target.getParentDirectory();
            const auto base = target.getFileNameWithoutExtension();
            int copied = 0;
            for (const auto& path : generated)
            {
                const juce::File src(path);
                if (src.copyFileTo(dir.getChildFile(base + "_" + src.getFileName())))
                    ++copied;
            }
            if (copied == 0)
            {
                safe->showToast(i18n::tr("Could not write preset files to ", "Impossible d'écrire les presets dans ") + dir.getFileName(), true);
                return;
            }
            safe->setStatus(i18n::tr("Saved ", "Enregistré : ") + juce::String(copied) + " ." + ext + i18n::tr(" files", " fichiers"));
            safe->showToast(i18n::tr("Saved ", "Enregistré : ") + juce::String(copied)
                            + i18n::tr(" preset files to ", " fichiers preset dans ") + dir.getFileName());
        });
}

namespace
{
class EmptyDropModal final : public ModalOverlay
{
public:
    EmptyDropModal(const juce::String& titleText,
                   const juce::var& patchVar,
                   const juce::StringArray& extraNotes)
    {
        title.setText(i18n::tr("Zero-Load or Import: ", "Sans-chargement ou Importer : ") + titleText, juce::dontSendNotification);
        title.setFont(theme::font(20.0f, true));
        subtitle.setText(i18n::tr("PatchLink is empty. Share a code or drag to your DAW cleanly without loading plugins, or load effects now.",
                                  "PatchLink est vide. Partagez un code ou glissez vers votre DAW sans charger les plugins, ou chargez-les."),
                         juce::dontSendNotification);
        subtitle.setFont(theme::font(12.5f));
        subtitle.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(title);
        addAndMakeVisible(subtitle);

        closeButton.setTooltip(i18n::tr("Cancel (Esc)", "Annuler (Échap)"));
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(closeButton);

        auto lines = chainformat::manifestLines(patchVar);
        for (const auto& note : extraNotes)
            if (note.isNotEmpty()) lines.add("[note] " + note);

        caption.setText(i18n::tr("DETECTED PLUGINS (", "PLUGINS DÉTECTÉS (") + juce::String(lines.size()) + ")", juce::dontSendNotification);
        caption.setFont(theme::font(11.0f, true));
        caption.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(caption);

        manifest.setMultiLine(true);
        manifest.setReadOnly(true);
        manifest.setFont(theme::mono(11.0f));
        manifest.setText(lines.isEmpty() ? i18n::tr("No plugins detected.", "Aucun plugin détecté.") : lines.joinIntoString("\n"));
        addAndMakeVisible(manifest);

        stageBtn.setButtonText(i18n::tr("Stage only", "Garder prêt"));
        dragBtn.setButtonText(i18n::tr("Drag to DAW", "Glisser vers DAW"));
        shareBtn.setButtonText(i18n::tr("Share code", "Partager code"));
        loadBtn.setButtonText(i18n::tr("Load effects", "Charger effets"));
        stageBtn.setTooltip(i18n::tr("Keep staged in toolbar without instantiating any plugin DLLs",
                                     "Garder prêt dans la barre sans charger les DLLs"));
        dragBtn.setTooltip(i18n::tr("Drag this button directly into your DAW (zero plugins loaded inside PatchLink)",
                                    "Glisser ce bouton dans votre DAW (zéro plugin chargé ici)"));
        shareBtn.setTooltip(i18n::tr("Publish and get a 6-character code immediately without loading plugins",
                                     "Publier un code à 6 caractères sans charger les plugins"));
        loadBtn.setTooltip(i18n::tr("Load all available VST3 plugins into PatchLink now",
                                    "Charger tous les plugins VST3 disponibles ici"));

        stageBtn.onClick = [this] { if (onStageOnly) onStageOnly(); };
        dragBtn.onDragStart = [this] { if (onDragOnly) onDragOnly(true); };
        dragBtn.onClick = [this] { if (onDragOnly) onDragOnly(false); };
        shareBtn.onClick = [this] { if (onShareOnly) onShareOnly(); };
        loadBtn.onClick = [this] { if (onLoadEffects) onLoadEffects(); };

        addAndMakeVisible(stageBtn);
        addAndMakeVisible(dragBtn);
        addAndMakeVisible(shareBtn);
        addAndMakeVisible(loadBtn);
    }

    juce::Rectangle<int> getCardBounds() const override
    {
        const int w = juce::jmin(680, getWidth() - 48);
        const int h = juce::jmin(470, getHeight() - 48);
        return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
    }

    std::function<void()> onStageOnly;
    std::function<void(bool startDrag)> onDragOnly;
    std::function<void()> onShareOnly;
    std::function<void()> onLoadEffects;

private:
    juce::Label title, subtitle, caption;
    juce::TextEditor manifest;
    PlButton closeButton { "", Icon::close, PlButton::Style::ghost };
    PlButton stageBtn { "Stage only", Icon::none, PlButton::Style::ghost };
    PlButton dragBtn { "Drag to DAW", Icon::exportFile, PlButton::Style::secondary };
    PlButton shareBtn { "Share code", Icon::share, PlButton::Style::secondary };
    PlButton loadBtn { "Load effects", Icon::plus, PlButton::Style::primary };

    void layoutCard(juce::Rectangle<int> card) override
    {
        auto r = card.reduced(24, 20);
        closeButton.setBounds(r.getRight() - 34, r.getY(), 34, 34);
        title.setBounds(r.removeFromTop(32).withTrimmedRight(44));
        subtitle.setBounds(r.removeFromTop(34));
        r.removeFromTop(10);

        auto footer = r.removeFromBottom(40);
        loadBtn.setBounds(footer.removeFromRight(124).reduced(0, 2));
        footer.removeFromRight(8);
        shareBtn.setBounds(footer.removeFromRight(118).reduced(0, 2));
        footer.removeFromRight(8);
        dragBtn.setBounds(footer.removeFromRight(128).reduced(0, 2));
        footer.removeFromRight(8);
        stageBtn.setBounds(footer.removeFromRight(98).reduced(0, 2));
        r.removeFromBottom(12);

        caption.setBounds(r.removeFromTop(20));
        r.removeFromTop(4);
        manifest.setBounds(r);
    }
};

class FirstRunSetupModal final : public ModalOverlay
{
public:
    explicit FirstRunSetupModal(PluginScanManager& scanMgr) : mgr(scanMgr)
    {
        title.setText(i18n::tr("Welcome to PatchLink — Choose your VST folders",
                               "Bienvenue sur PatchLink — Choisissez vos dossiers VST"),
                      juce::dontSendNotification);
        title.setFont(theme::font(20.0f, true));
        subtitle.setText(i18n::tr("Select where your third-party VST3 plugins are installed so PatchLink can load shared chains and effects. You can add custom folders anytime from the Library button.",
                                  "Indiquez où vos plugins VST3 sont installés pour que PatchLink puisse charger les chaînes partagées. Vous pourrez modifier ces dossiers via le bouton Dossiers."),
                         juce::dontSendNotification);
        subtitle.setFont(theme::font(12.5f));
        subtitle.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(title);
        addAndMakeVisible(subtitle);

        closeButton.setTooltip(i18n::tr("Skip for now (Esc)", "Passer pour l'instant (Échap)"));
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(closeButton);

        caption.setText(i18n::tr("DETECTED & CUSTOM VST FOLDERS", "DOSSIERS VST DÉTECTÉS ET PERSONNALISÉS"),
                        juce::dontSendNotification);
        caption.setFont(theme::font(11.0f, true));
        caption.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(caption);

        folderList.setMultiLine(true);
        folderList.setReadOnly(true);
        folderList.setFont(theme::mono(11.0f));
        addAndMakeVisible(folderList);
        refreshFolders();

        addFolderBtn.setButtonText(i18n::tr("+ Choose VST Folder...", "+ Choisir dossier VST..."));
        manageBtn.setButtonText(i18n::tr("Edit paths...", "Modifier..."));
        skipBtn.setButtonText(i18n::tr("Skip for now", "Plus tard"));
        scanNowBtn.setButtonText(i18n::tr("Scan Plugins Now", "Scanner maintenant"));
        addFolderBtn.setTooltip(i18n::tr("Browse and add a custom VST3 folder (e.g. D:\\VST3 or C:\\Program Files\\Steinberg\\VSTPlugins)",
                                         "Parcourir et ajouter un dossier VST personnalisé (ex. D:\\VST3 ou C:\\Program Files\\Steinberg\\VSTPlugins)"));
        manageBtn.setTooltip(i18n::tr("Open the full Library Manager to toggle or remove individual paths",
                                      "Ouvrir le gestionnaire complet pour activer ou retirer chaque dossier"));
        skipBtn.setTooltip(i18n::tr("Skip scanning for now (you can still convert chains in Zero-Load mode)",
                                    "Passer le scan pour l'instant (le mode Sans-chargement reste disponible)"));
        scanNowBtn.setTooltip(i18n::tr("Scan all enabled folders above and index your VST3 plugins",
                                       "Scanner tous les dossiers activés ci-dessus pour indexer vos plugins VST3"));

        addFolderBtn.onClick = [this] { pickFolder(); };
        manageBtn.onClick = [this] { if (onManageFolders) onManageFolders(); };
        skipBtn.onClick = [this] { if (onDismiss) onDismiss(); };
        scanNowBtn.onClick = [this] { if (onScanNow) onScanNow(); };

        addAndMakeVisible(addFolderBtn);
        addAndMakeVisible(manageBtn);
        addAndMakeVisible(skipBtn);
        addAndMakeVisible(scanNowBtn);
    }

    juce::Rectangle<int> getCardBounds() const override
    {
        const int w = juce::jmin(700, getWidth() - 48);
        const int h = juce::jmin(460, getHeight() - 48);
        return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
    }

    void refreshFolders()
    {
        juce::StringArray lines;
        for (const auto& s : mgr.getSources())
        {
            lines.add(juce::String(s.enabled ? "[ON]  " : "[OFF] ")
                      + s.path
                      + (s.origin.isNotEmpty() ? ("   (" + s.origin + ")") : ""));
        }
        folderList.setText(lines.isEmpty()
                               ? i18n::tr("No folders configured yet. Click '+ Choose VST Folder...' below to add your VST3 directory.",
                                          "Aucun dossier configuré. Cliquez sur « + Choisir dossier VST... » ci-dessous.")
                               : lines.joinIntoString("\n"));
    }

    std::function<void()> onManageFolders;
    std::function<void()> onScanNow;

private:
    PluginScanManager& mgr;
    juce::Label title, subtitle, caption;
    juce::TextEditor folderList;
    PlButton closeButton { "", Icon::close, PlButton::Style::ghost };
    PlButton addFolderBtn { "+ Choose VST Folder...", Icon::plus, PlButton::Style::secondary };
    PlButton manageBtn { "Edit paths...", Icon::library, PlButton::Style::ghost };
    PlButton skipBtn { "Skip for now", Icon::none, PlButton::Style::ghost };
    PlButton scanNowBtn { "Scan Plugins Now", Icon::library, PlButton::Style::primary };

    void pickFolder()
    {
        auto chooser = std::make_shared<juce::FileChooser>(i18n::tr("Select a VST3 plugin folder", "Sélectionner un dossier de plugins VST3"));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<FirstRunSetupModal>(this), chooser](const juce::FileChooser&) {
                if (!safe) return;
                const auto dir = chooser->getResult();
                if (!dir.isDirectory()) return;
                safe->mgr.addSource({ dir.getFullPathName(), i18n::tr("Custom VST folder", "Dossier VST personnalisé"), true, true });
                safe->refreshFolders();
            });
    }

    void layoutCard(juce::Rectangle<int> card) override
    {
        auto r = card.reduced(24, 20);
        closeButton.setBounds(r.getRight() - 34, r.getY(), 34, 34);
        title.setBounds(r.removeFromTop(32).withTrimmedRight(44));
        subtitle.setBounds(r.removeFromTop(38));
        r.removeFromTop(10);

        auto footer = r.removeFromBottom(40);
        scanNowBtn.setBounds(footer.removeFromRight(164).reduced(0, 2));
        footer.removeFromRight(8);
        skipBtn.setBounds(footer.removeFromRight(104).reduced(0, 2));
        addFolderBtn.setBounds(footer.removeFromLeft(182).reduced(0, 2));
        footer.removeFromLeft(8);
        manageBtn.setBounds(footer.removeFromLeft(110).reduced(0, 2));
        r.removeFromBottom(12);

        caption.setBounds(r.removeFromTop(20));
        r.removeFromTop(4);
        folderList.setBounds(r);
    }
};
} // namespace

void PatchLinkEditor::showFirstRunSetupModal()
{
    dismissOverlay();
    auto* modal = new FirstRunSetupModal(proc.getScanManager());
    overlay.reset(modal);

    modal->onDismiss = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
        if (!safe) return;
        safe->proc.getScanManager().setSetupCompleted(true);
        safe->dismissOverlay();
    };

    modal->onManageFolders = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
        if (!safe) return;
        safe->proc.getScanManager().setSetupCompleted(true);
        safe->dismissOverlay();
        safe->openScanDialog();
    };

    modal->onScanNow = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
        if (!safe) return;
        safe->proc.getScanManager().setSetupCompleted(true);
        safe->dismissOverlay();
        safe->scanWindow.reset();
        safe->proc.getScanManager().startScan(false);
        safe->openScanDialog();
    };

    modal->setBounds(getLocalBounds());
    addAndMakeVisible(modal);
    modal->grabKeyboardFocus();
}

void PatchLinkEditor::showEmptyDropModal(const juce::var& stagedVar,
                                         const juce::String& label,
                                         const juce::StringArray& notes,
                                         std::function<void()> loadIntoPatchLinkFn)
{
    dismissOverlay();
    auto* modal = new EmptyDropModal(label, stagedVar, notes);
    overlay.reset(modal);
    modal->onDismiss = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] { if (safe) safe->dismissOverlay(); };

    modal->onStageOnly = [safe = juce::Component::SafePointer<PatchLinkEditor>(this), stagedVar, label] {
        if (!safe) return;
        safe->dismissOverlay();
        safe->stagePatch(stagedVar, label,
                         i18n::tr("Staged \"", "Chaîne \"") + label
                         + i18n::tr("\" (zero-load) - drag button to your DAW",
                                    "\" prête (sans chargement) - glissez vers votre DAW"));
    };

    modal->onDragOnly = [safe = juce::Component::SafePointer<PatchLinkEditor>(this), stagedVar, label](bool startDrag) {
        if (!safe) return;
        safe->stagePatch(stagedVar, label);
        if (startDrag)
        {
            safe->startExternalDawDrag(stagedVar, -1);
        }
        else
        {
            safe->dismissOverlay();
            safe->showFormatPickerMenu(stagedVar, -1, false);
        }
    };

    modal->onShareOnly = [safe = juce::Component::SafePointer<PatchLinkEditor>(this), stagedVar, label] {
        if (!safe) return;
        safe->stagePatch(stagedVar, label);
        safe->openShareDialog(true);
    };

    modal->onLoadEffects = [safe = juce::Component::SafePointer<PatchLinkEditor>(this), loadFn = std::move(loadIntoPatchLinkFn)] {
        if (!safe) return;
        safe->dismissOverlay();
        safe->clearStagedPatch();
        if (loadFn) loadFn();
    };

    modal->setBounds(getLocalBounds());
    addAndMakeVisible(modal);
    modal->grabKeyboardFocus();
}

void PatchLinkEditor::importFiles(const juce::StringArray& files, int x, int y)
{
    auto cachedFiles = std::move(droppedFileCache);
    droppedFileCache.clear();

    struct PendingItem
    {
        juce::String displayName;
        bool isDawPreset = false;
        flstudio::ParsedFst fst;
        juce::var patchVar;
        juce::String error;
    };

    struct BatchState
    {
        std::vector<PendingItem> items;
        size_t index = 0;
        int effectOffset = 0;
        int loadedEffects = 0;
        int loadedChains = 0;
        int loadedMixerTracks = 0;
        bool anyReplaced = false;
        juce::String lastLabel;
        juce::String lastSourceDaw { "FX Chain" };
        juce::StringArray notes;
        juce::StringArray errors;
    };

    auto state = std::make_shared<BatchState>();
    // Decode cached / dropped DAW preset and .patchlink data into RAM immediately.
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (!file.hasFileExtension(".fst;.patchlink;.rfxchain;.rpp;.adg;.adv;.als;.vstpreset;.bwpreset;.fxchainpreset;.fxchain;.multipreset"))
            continue;

        PendingItem item;
        item.displayName = file.getFileNameWithoutExtension();

        juce::MemoryBlock rawBlock;
        if (const auto it = cachedFiles.find(file.getFullPathName()); it != cachedFiles.end() && it->second.getSize() > 0)
            rawBlock = it->second;
        else if (file.existsAsFile())
            file.loadFileAsData(rawBlock);

        if (rawBlock.getSize() == 0)
        {
            item.error = "File not found: " + file.getFileName();
        }
        else if (file.hasFileExtension(".patchlink"))
        {
            const auto txt = rawBlock.toString();
            if (txt.isEmpty())
            {
                item.error = "Empty file: " + file.getFileName();
            }
            else
            {
                item.patchVar = juce::JSON::parse(txt);
                if (!item.patchVar.isObject())
                    item.error = "Invalid JSON: " + file.getFileName();
            }
        }
        else
        {
            item.isDawPreset = true;
            item.fst = flstudio::parseAnyDawFile(file, rawBlock.getData(), rawBlock.getSize());
            dawexport::enrichParsedFstFromCache(item.fst);
            if (item.fst.valid && item.fst.trackName.isEmpty())
                item.fst.trackName = file.getFileNameWithoutExtension();
            if (item.fst.trackName.isNotEmpty())
                item.displayName = item.fst.trackName;
        }

        state->items.push_back(std::move(item));
    }

    if (state->items.empty())
        return;

    auto safe = juce::Component::SafePointer<PatchLinkEditor>(this);
    auto runBatch = [safe, state, x, y]() mutable {
        if (!safe) return;
        safe->clearStagedPatch();
        auto step = std::make_shared<std::function<void()>>();

        *step = [safe, state, step, x, y]() mutable {
            if (!safe) return;

            if (state->index >= state->items.size())
            {
                if (state->anyReplaced)
                    safe->lastShareCode.clear();
                safe->refreshViews();

                if (!state->errors.isEmpty() && state->loadedEffects == 0
                    && state->loadedChains == 0 && state->loadedMixerTracks == 0)
                {
                    safe->setStatus(i18n::tr("Import failed", "Échec de l'import"));
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           i18n::tr("Cannot import file", "Impossible d'importer le fichier"),
                                                           state->errors.joinIntoString("\n"));
                    return;
                }

                if (!state->notes.isEmpty())
                {
                    const juce::String status = (state->loadedEffects > 0 || state->loadedChains > 0 || state->loadedMixerTracks > 0)
                        ? i18n::tr("Imported with missing or skipped plugins", "Importé avec des plugins manquants ou ignorés")
                        : i18n::tr("Nothing loaded", "Rien n'a été chargé");
                    safe->setStatus(status);
                    safe->showMissingPlugins(state->notes,
                                             state->loadedMixerTracks > 0 ? (state->lastSourceDaw + i18n::tr(" chain imported", " : chaîne importée"))
                                             : state->loadedChains > 0   ? i18n::tr("Chain imported", "Chaîne importée")
                                                                         : (state->lastSourceDaw + i18n::tr(" preset import", " : preset importé")));
                    return;
                }

                if (state->loadedEffects == 1 && state->loadedChains == 0 && state->loadedMixerTracks == 0)
                {
                    safe->setStatus(i18n::tr("Added ", "Ajouté : ") + state->lastLabel);
                    safe->showToast(i18n::tr("Added ", "Ajouté : ") + state->lastLabel + i18n::tr(" preset", " (preset)"));
                }
                else if (state->loadedEffects > 1 && state->loadedChains == 0 && state->loadedMixerTracks == 0)
                {
                    const auto msg = i18n::tr("Added ", "Ajout de ") + juce::String(state->loadedEffects) + i18n::tr(" DAW presets", " presets DAW");
                    safe->setStatus(msg);
                    safe->showToast(msg);
                }
                else if (state->loadedMixerTracks > 0)
                {
                    const auto msg = i18n::tr("Loaded ", "Chargé : ") + state->lastSourceDaw + i18n::tr(" chain \"", " chaîne \"") + state->lastLabel + "\"";
                    safe->setStatus(msg);
                    safe->showToast(msg);
                }
                else
                {
                    safe->setStatus(i18n::tr("Chain imported", "Chaîne importée"));
                    safe->showToast(i18n::tr("Chain imported", "Chaîne importée"));
                }
                return;
            }

            const auto item = state->items[state->index++];
            if (item.error.isNotEmpty())
            {
                state->errors.add(item.error);
                (*step)();
                return;
            }

            if (!item.isDawPreset)
            {
                safe->pluginWindows.clear();
                state->anyReplaced = true;
                state->lastLabel = item.displayName;
                safe->setStatus("Opening " + item.displayName + "...");
                safe->proc.loadFromVarAsync(item.patchVar,
                    [safe, state, step](juce::StringArray missing, juce::String err) {
                        if (!safe) return;
                        if (err.isNotEmpty()) state->errors.add(err);
                        else ++state->loadedChains;
                        for (const auto& m : missing) state->notes.add(m);
                        (*step)();
                    });
                return;
            }

            if (!item.fst.valid)
            {
                state->errors.add(item.fst.error.isNotEmpty() ? item.fst.error : ("Invalid preset: " + item.displayName));
                (*step)();
                return;
            }

            if (item.fst.plugins.empty())
            {
                if (item.fst.skipped.isEmpty())
                    state->errors.add("No plugins found in " + item.displayName);
                else
                    for (const auto& s : item.fst.skipped) state->notes.add(s);
                (*step)();
                return;
            }

            const bool isMixerOrMulti = item.fst.isMixerTrack || item.fst.plugins.size() > 1;
            int dropX = 282;
            int dropY = 208;
            if (!isMixerOrMulti && x >= 0 && y >= 0 && safe->mode == Mode::patcher)
            {
                const auto localPt = safe->patcherView.getLocalPoint(safe.getComponent(), juce::Point<int>(x, y))
                                   + safe->patcherView.getScrollOffset();
                dropX = juce::jlimit(60, 1620, localPt.x - 98 + state->effectOffset * 36);
                dropY = juce::jlimit(40, 1060, localPt.y - 46 + state->effectOffset * 28);
                ++state->effectOffset;
            }
            else
            {
                const int count = static_cast<int>(safe->proc.getLoadedNodes().size());
                dropX = 282 + (count % 5) * 224;
                dropY = 208 + (count / 5) * 132;
            }

            const auto dawTag = item.fst.sourceDaw.isNotEmpty() ? item.fst.sourceDaw : juce::String("DAW");
            state->lastSourceDaw = dawTag;
            if (isMixerOrMulti)
            {
                safe->pluginWindows.clear();
                safe->setStatus(i18n::tr("Loading ", "Chargement ") + dawTag + i18n::tr(" FX chain...", " : chaîne FX..."));
            }
            else
            {
                safe->setStatus(i18n::tr("Loading ", "Chargement ") + dawTag + i18n::tr(" preset ", " : preset ") + item.displayName + "...");
            }

            safe->proc.importParsedFstAsync(item.fst, dropX, dropY, true,
                [safe, state, step, isMixerOrMulti, label = item.displayName](bool isMixerTrack, int loadedCount,
                                                                               juce::StringArray skippedOrMissing, juce::String err) {
                    if (!safe) return;
                    if (err.isNotEmpty())
                    {
                        state->errors.add(err);
                    }
                    else if (loadedCount > 0)
                    {
                        state->lastLabel = label;
                        if (isMixerTrack || isMixerOrMulti)
                        {
                            state->anyReplaced = true;
                            ++state->loadedMixerTracks;
                        }
                        else
                        {
                            ++state->loadedEffects;
                        }
                    }
                    for (const auto& m : skippedOrMissing)
                        state->notes.add(m);
                    safe->refreshViews();
                    (*step)();
                });
        };

        (*step)();
    };

    if (proc.getLoadedNodes().empty())
    {
        juce::var stagedCandidate;
        juce::String candidateLabel;
        juce::StringArray extraNotes;
        juce::Array<juce::var> combinedNodes;

        for (auto& item : state->items)
        {
            if (item.error.isNotEmpty())
                continue;
            if (!item.isDawPreset && item.patchVar.isObject())
            {
                stagedCandidate = item.patchVar;
                candidateLabel = item.displayName;
                if (auto* o = item.patchVar.getDynamicObject())
                {
                    if (o->getProperty("name").toString().isNotEmpty())
                        candidateLabel = o->getProperty("name").toString();
                    if (auto* arr = o->getProperty("nodes").getArray())
                        combinedNodes = *arr;
                }
                break;
            }
            if (item.isDawPreset && item.fst.valid && !item.fst.plugins.empty())
            {
                const auto v = flstudio::parsedFstToPatchVar(item.fst, &proc.getKnownList());
                if (auto* o = v.getDynamicObject())
                {
                    if (candidateLabel.isEmpty())
                        candidateLabel = o->getProperty("name").toString();
                    else
                        candidateLabel = juce::String(state->items.size()) + " DAW Presets";
                    if (auto* arr = o->getProperty("nodes").getArray())
                    {
                        for (auto n : *arr)
                        {
                            if (auto* no = n.getDynamicObject())
                            {
                                no->setProperty("uid", 100 + combinedNodes.size());
                                no->setProperty("order", combinedNodes.size());
                            }
                            combinedNodes.add(n);
                        }
                    }
                }
                for (const auto& s : item.fst.skipped)
                    extraNotes.add(s);
            }
        }

        if (!combinedNodes.isEmpty())
        {
            if (!stagedCandidate.isObject())
            {
                juce::DynamicObject::Ptr root = new juce::DynamicObject();
                root->setProperty("app", "PatchLink");
                root->setProperty("chainFormat", chainformat::kFormatVersion);
                root->setProperty("name", candidateLabel.isNotEmpty() ? candidateLabel : "FX Chain");
                root->setProperty("nodes", combinedNodes);
                root->setProperty("connections", juce::Array<juce::var>());
                stagedCandidate = juce::var(root.get());
            }
            showEmptyDropModal(stagedCandidate,
                               candidateLabel.isNotEmpty() ? candidateLabel : "Dropped chain",
                               extraNotes,
                               std::move(runBatch));
            return;
        }
    }

    runBatch();
}
