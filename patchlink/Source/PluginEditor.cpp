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
    ScanManagerWindow() : juce::DialogWindow("Plugin Library", theme::background, true, false) {}
    void closeButtonPressed() override { setVisible(false); }
};

PatchLinkEditor::PatchLinkEditor(PatchLinkProcessor& processor)
    : AudioProcessorEditor(processor), proc(processor)
{
    theme::loadSavedTheme();
    lookAndFeel.applyPalette();
    setLookAndFeel(&lookAndFeel);
    setSize(1120, 720);
    setResizeLimits(840, 540, 3200, 2200);
    setResizable(true, true);
    setWantsKeyboardFocus(true);

    simpleButton.setTooltip("A serial chain from top to bottom");
    patcherButton.setTooltip("A free-form stereo routing canvas");
    simpleButton.onClick = [this] { setMode(Mode::simple); };
    patcherButton.onClick = [this] { setMode(Mode::patcher); };
    themeButton.setTooltip("Switch interface color theme");
    themeButton.onClick = [this] { openThemeMenu(); };
    addButton.setTooltip("Search your plugin library (Ctrl+F)");
    addButton.onClick = [this] {
        const auto index = (int)proc.getLoadedNodes().size();
        openPluginBrowser(282 + (index % 5) * 224, 208 + (index / 5) * 132);
    };
    libraryButton.setTooltip("Manage plugin folders, scans, and blacklist");
    libraryButton.onClick = [this] { openScanDialog(); };
    shareButton.onClick = [this] { openShareDialog(true); };
    codeButton.onClick = [this] { openShareDialog(false); };
    importButton.onClick = [this] { doImport(); };
    exportButton.onClick = [this] { doExport(); };
    dragDawButton.onDragStart = [this] { startExternalDawDrag({}, -1); };
    dragDawButton.onClick = [this] { showFormatPickerMenu({}, -1, false); };
    for (auto* button : { &simpleButton, &patcherButton, &themeButton, &libraryButton, &codeButton, &shareButton,
                          &importButton, &exportButton, &dragDawButton, &addButton })
        addAndMakeVisible(button);

    patchName.setText(proc.getPatchName(), juce::dontSendNotification);
    patchName.setFont(theme::font(17.0f, true));
    patchName.setEditable(true, true, false);
    patchName.setTooltip("Double-click to rename this chain");
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
    addChildComponent(toast);

    hostDetection = dawexport::detectHostDaw(proc.wrapperType);
    selectedExportFormat = hostDetection.isStandalone ? dawexport::TargetFormat::autoDetect
                                                      : hostDetection.detectedFormat;
    updateExportButtonState();

    proc.addChangeListener(this);
    proc.getScanManager().addChangeListener(this);
    setMode(proc.preferredMode == 2 ? Mode::patcher : Mode::simple);
    setStatus("Ready  ·  " + (hostDetection.isStandalone ? juce::String("Standalone") : ("Host: " + hostDetection.hostName)));
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
    proc.preferredMode = mode == Mode::simple ? 1 : 2;
    if (mode == Mode::simple)
    {
        proc.autoConnectSerial = true;
        proc.rebuildSerialIfNeeded();
        chainView.setVisible(true);
        patcherView.setVisible(false);
    }
    else
    {
        proc.autoConnectSerial = false;
        chainView.setVisible(false);
        patcherView.setVisible(true);
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
    for (auto* window : pluginWindows)
        window->setBackgroundColour(theme::background);
    if (scanWindow != nullptr && !proc.getScanManager().isScanning() && !scanWindow->isVisible())
        scanWindow.reset();
    sendLookAndFeelChange();
    repaint();
    showToast(juce::String("Theme: ") + theme::themeName(theme::currentThemeIndex));
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

    chainView.refresh(nodes);
    patcherView.refresh(nodes, proc.getWires());

    int stagedCount = 0;
    if (nodes.empty() && stagedPatchVar.isObject())
        if (auto* arr = stagedPatchVar.getDynamicObject()->getProperty("nodes").getArray())
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
        chainInfo.setText(juce::String(stagedCount) + " staged (zero-load)", juce::dontSendNotification);
    }
    else
    {
        chainInfo.setText(juce::String((int)nodes.size()) + (nodes.size() == 1 ? " plugin" : " plugins")
                          + "  /  STEREO", juce::dontSendNotification);
    }

    const auto sampleRate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    systemInfo.setText(juce::String(proc.getKnownList().getNumTypes()) + " indexed  /  "
                       + juce::String(sampleRate / 1000.0, 1) + " kHz", juce::dontSendNotification);
    if (proc.getScanManager().isScanning()) setStatus("Indexing plugin library...");
    else if (statusLabel.getText().startsWith("Indexing")) setStatus("Ready");
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
    auto header = r.removeFromTop(theme::headerHeight).reduced(20, 13);
    header.removeFromLeft(170);
    simpleButton.setBounds(header.removeFromLeft(92));
    header.removeFromLeft(4);
    patcherButton.setBounds(header.removeFromLeft(104));
    header.removeFromLeft(10);
    shareButton.setBounds(header.removeFromRight(94));
    header.removeFromRight(8);
    codeButton.setBounds(header.removeFromRight(108));
    header.removeFromRight(8);
    libraryButton.setBounds(header.removeFromRight(98));
    header.removeFromRight(6);
    themeButton.setBounds(header.removeFromRight(76));

    auto toolbar = r.removeFromTop(theme::toolbarHeight).reduced(22, 10);
    addButton.setBounds(toolbar.removeFromRight(132));
    toolbar.removeFromRight(6);
    dragDawButton.setBounds(toolbar.removeFromRight(124));
    toolbar.removeFromRight(6);
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
        safe->setStatus("Loading " + description.name + "...");
        safe->proc.addPluginAsync(description, x, y, [safe, name = description.name](const juce::String& error) {
            if (!safe) return;
            if (error.isNotEmpty())
            {
                safe->setStatus("Plugin could not be loaded");
                safe->showToast(error, true);
            }
            else
            {
                safe->setStatus(name + " is ready");
                safe->showToast(name + " added to your chain");
            }
            safe->refreshViews();
        });
    };
    browser->setBounds(getLocalBounds());
    addAndMakeVisible(browser);
    browser->grabKeyboardFocus();
}

void PatchLinkEditor::openShareDialog(bool publish)
{
    dismissOverlay();
    auto mode = publish ? ShareDialog::Mode::publish : ShareDialog::Mode::openCode;
    const auto activePatch = publish ? getActiveOrStagedPatchVar() : juce::var();
    auto* dialog = new ShareDialog(mode, activePatch);
    overlay.reset(dialog);
    dialog->onDismiss = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] { if (safe) safe->dismissOverlay(); };

    dialog->onPublish = [safe = juce::Component::SafePointer<PatchLinkEditor>(this),
                         dialogSafe = juce::Component::SafePointer<ShareDialog>(dialog),
                         patch = activePatch, base = apiBase](const juce::String& title,
                            const juce::String& author, const juce::String& description, bool isPublic) {
        if (!safe || !dialogSafe) return;
        dialogSafe->setBusy(true, "Publishing your chain to PatchDrop...");
        std::thread([safe, dialogSafe, patch, title, author, description, isPublic, base]() mutable {
            juce::String error;
            auto code = ShareClient::publish(patch, title, author, description, base, error, isPublic);
            juce::MessageManager::callAsync([safe, dialogSafe, code, error]() mutable {
                if (dialogSafe)
                {
                    dialogSafe->setBusy(false);
                    if (code.isNotEmpty()) dialogSafe->publishSucceeded(code);
                    else dialogSafe->operationFailed(error.isNotEmpty() ? error : "PatchDrop could not publish this chain.");
                }
                else if (safe)
                {
                    if (code.isNotEmpty())
                    {
                        safe->lastShareCode = code;
                        juce::SystemClipboard::copyTextToClipboard(code);
                        safe->showToast("Published as " + code + " - code copied");
                    }
                    else safe->showToast(error.isNotEmpty() ? error : "Publishing failed", true);
                }
            });
        }).detach();
    };

    dialog->onOpenCode = [safe = juce::Component::SafePointer<PatchLinkEditor>(this),
                          dialogSafe = juce::Component::SafePointer<ShareDialog>(dialog),
                          base = apiBase](const juce::String& code) {
        if (!safe || !dialogSafe) return;
        dialogSafe->setBusy(true, "Fetching " + code + "...");
        std::thread([safe, dialogSafe, code, base]() mutable {
            juce::var patch;
            juce::String title, error;
            const auto ok = ShareClient::fetch(code, base, patch, title, error);
            juce::MessageManager::callAsync([safe, dialogSafe, code, patch, title, error, ok]() mutable {
                if (!safe) return;
                if (!ok)
                {
                    if (dialogSafe) dialogSafe->operationFailed(error.isNotEmpty() ? error : "Code not found.");
                    else safe->showToast(error.isNotEmpty() ? error : "Code not found", true);
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
        safe->stagePatch(patch, title, "Staged \"" + title + "\" (zero-load) - drag button to your DAW");
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
    setStatus("Loading " + label + "...");
    proc.loadFromVarAsync(patch, [safe = juce::Component::SafePointer<PatchLinkEditor>(this), label](juce::StringArray missing, juce::String loadError) {
        if (!safe) return;
        if (loadError.isNotEmpty())
        {
            safe->setStatus("Could not open " + label);
            safe->showToast(loadError, true);
            return;
        }
        safe->setStatus(label + " is ready");
        safe->refreshViews();
        safe->showMissingPlugins(missing, label);
    });
}

void PatchLinkEditor::doImport()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Open a PatchLink chain or DAW preset",
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

void PatchLinkEditor::doExport()
{
    const auto patch = getActiveOrStagedPatchVar();
    if (!patch.isObject())
    {
        showToast("No chain to export", true);
        return;
    }
    auto chooser = std::make_shared<juce::FileChooser>("Save a PatchLink chain", juce::File(), "*.patchlink");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), chooser, patch](const juce::FileChooser&) mutable {
            if (!safe) return;
            auto file = chooser->getResult();
            if (file.getFullPathName().isEmpty()) return;
            if (file.getFileExtension().isEmpty()) file = file.withFileExtension("patchlink");
            if (!file.replaceWithText(juce::JSON::toString(patch, true)))
            {
                safe->showToast("Write failed", true);
                return;
            }
            safe->setStatus("Exported " + file.getFileName());
            safe->showToast("Chain saved to " + file.getFileName());
        });
}

void PatchLinkEditor::confirmClearChain()
{
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, "Clear this chain?",
        "This removes every hosted plugin and its connections from the current chain.",
        "Clear chain", "Cancel", this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<PatchLinkEditor>(this)](int result) {
            if (safe && result != 0)
            {
                safe->pluginWindows.clear();
                safe->proc.clearChain();
                safe->setStatus("Chain cleared");
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
        showToast("Everything in this chain was restored");
        return;
    }
    juce::String message = "The available plugins and settings were restored.\n\nInstall or scan these missing plugins, then reopen the chain:\n\n- "
                         + missing.joinIntoString("\n- ")
                         + "\n\nYour patch file keeps their original settings.";
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, title, message);
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
            ? "Drop FL Studio .fst presets to add to PatchLink"
            : "Drop FL Studio .fst preset (single effect or mixer track)";
    if (total == reaperCount && reaperCount > 0)
        return "Drop REAPER .RfxChain / .rpp FX chain into PatchLink";
    if (total == abletonCount && abletonCount > 0)
        return "Drop Ableton Live .adg / .adv rack into PatchLink";
    if (total == vst3PresetCount && vst3PresetCount > 0)
        return vst3PresetCount > 1
            ? "Drop VST3 preset / DAW FX chain files into PatchLink"
            : "Drop VST3 preset / DAW FX chain into PatchLink";
    if (total == patchCount && patchCount > 0)
        return "Drop .patchlink file to open chain";
    return "Drop DAW FX chains (.fst, .RfxChain, .adg, .vstpreset, .patchlink)";
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
    g.drawText("Drop into PatchLink", area.getX() + 20, cy + 2, area.getWidth() - 40, 26,
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
    dragDropHint = "Drop preset " + code + " from PatchDrop browser";
    repaint();
}

void PatchLinkEditor::textDragMove(const juce::String& text, int, int)
{
    const auto code = extractPatchCodeFromDraggedText(text);
    if (code.isEmpty())
        return;
    const auto nextHint = "Drop preset " + code + " from PatchDrop browser";
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

    setStatus("Fetching " + code + " from PatchDrop...");
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
                safe->setStatus("Could not fetch " + code);
                safe->showToast(error.isNotEmpty() ? error : ("Code " + code + " not found"), true);
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
        dragDawButton.setButtonText("Drag to DAW");
        dragDawButton.setTooltip("Standalone mode: click to pick DAW format (FL / REAPER / Ableton / VST3), then drag out to your DAW");
    }
    else
    {
        const auto fmt = effectiveExportFormat();
        dragDawButton.setButtonText("Drag ." + dawexport::formatExtension(fmt));
        const auto src = hostDetection.isStandalone ? juce::String("Format: ")
                                                    : ("Auto-detected " + hostDetection.hostName + ": ");
        dragDawButton.setTooltip(src + dawexport::formatLabel(fmt) + " - drag onto your DAW, or click to change format / save");
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
                              ? "Standalone export format"
                              : ("Detected host: " + hostDetection.hostName));
    menu.addItem(1, "FL Studio (.fst)", true, selectedExportFormat == dawexport::TargetFormat::flStudioFst);
    menu.addItem(2, "REAPER (.RfxChain)", true, selectedExportFormat == dawexport::TargetFormat::reaperRfxChain);
    menu.addItem(3, "Ableton Live Rack (.adg)", true, selectedExportFormat == dawexport::TargetFormat::abletonAdg);
    menu.addItem(4, "VST3 Preset (.vstpreset)", true, selectedExportFormat == dawexport::TargetFormat::vstPreset);
    menu.addItem(5, "PatchLink (.patchlink)", true, selectedExportFormat == dawexport::TargetFormat::patchlinkFile);
    menu.addSeparator();
    menu.addItem(10, singleSlotIndex >= 0 ? "Save single preset file..." : "Save DAW preset file...", hasNodes);

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
                safe->setStatus("DAW target: " + lbl);
                safe->showToast(fromDragAttempt ? ("Format set to " + lbl + " - now drag button into your DAW!")
                                                : ("Format set to " + lbl + " (drag button to DAW)"));
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
        showToast("Add, drop, or open a chain first before dragging to your DAW", true);
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
        showToast(err.isNotEmpty() ? err : "Could not prepare DAW preset", true);
        return;
    }

    setStatus("Dragging " + dawexport::formatLabel(fmt) + " to DAW...");
    performExternalDragDropOfFiles(files, false, this,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] {
            if (safe) safe->setStatus("Ready");
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
        "Save " + dawexport::formatLabel(fmt),
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
                safe->showToast(err.isNotEmpty() ? err : "Export failed", true);
                return;
            }
            if (generated.size() == 1)
            {
                const juce::File src(generated[0]);
                if (!src.copyFileTo(target))
                {
                    safe->showToast("Could not write " + target.getFileName(), true);
                    return;
                }
                safe->setStatus("Saved " + target.getFileName());
                safe->showToast("Saved " + target.getFileName());
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
                safe->showToast("Could not write preset files to " + dir.getFileName(), true);
                return;
            }
            safe->setStatus("Saved " + juce::String(copied) + " ." + ext + " files");
            safe->showToast("Saved " + juce::String(copied) + " preset files to " + dir.getFileName());
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
        title.setText("Zero-Load or Import: " + titleText, juce::dontSendNotification);
        title.setFont(theme::font(20.0f, true));
        subtitle.setText("PatchLink is empty. Share a code or drag to your DAW cleanly without loading plugins, or load effects now.",
                         juce::dontSendNotification);
        subtitle.setFont(theme::font(12.5f));
        subtitle.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(title);
        addAndMakeVisible(subtitle);

        closeButton.setTooltip("Cancel (Esc)");
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(closeButton);

        auto lines = chainformat::manifestLines(patchVar);
        for (const auto& note : extraNotes)
            if (note.isNotEmpty()) lines.add("[note] " + note);

        caption.setText("DETECTED PLUGINS (" + juce::String(lines.size()) + ")", juce::dontSendNotification);
        caption.setFont(theme::font(11.0f, true));
        caption.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(caption);

        manifest.setMultiLine(true);
        manifest.setReadOnly(true);
        manifest.setFont(theme::mono(11.0f));
        manifest.setText(lines.isEmpty() ? "No plugins detected." : lines.joinIntoString("\n"));
        addAndMakeVisible(manifest);

        stageBtn.setTooltip("Keep staged in toolbar without instantiating any plugin DLLs");
        dragBtn.setTooltip("Drag this button directly into your DAW (zero plugins loaded inside PatchLink)");
        shareBtn.setTooltip("Publish and get a 6-character code immediately without loading plugins");
        loadBtn.setTooltip("Load all available VST3 plugins into PatchLink now");

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
} // namespace

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
        safe->stagePatch(stagedVar, label, "Staged \"" + label + "\" (zero-load) - drag button to your DAW");
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
        if (!isSupportedDropFile(file))
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
                    safe->setStatus("Import failed");
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Cannot import file",
                                                           state->errors.joinIntoString("\n"));
                    return;
                }

                if (!state->notes.isEmpty())
                {
                    const juce::String status = (state->loadedEffects > 0 || state->loadedChains > 0 || state->loadedMixerTracks > 0)
                        ? "Imported with missing or skipped plugins"
                        : "Nothing loaded";
                    safe->setStatus(status);
                    safe->showMissingPlugins(state->notes,
                                             state->loadedMixerTracks > 0 ? (state->lastSourceDaw + " chain imported")
                                             : state->loadedChains > 0   ? "Chain imported"
                                                                         : (state->lastSourceDaw + " preset import"));
                    return;
                }

                if (state->loadedEffects == 1 && state->loadedChains == 0 && state->loadedMixerTracks == 0)
                {
                    safe->setStatus("Added " + state->lastLabel);
                    safe->showToast("Added " + state->lastLabel + " preset");
                }
                else if (state->loadedEffects > 1 && state->loadedChains == 0 && state->loadedMixerTracks == 0)
                {
                    const auto msg = "Added " + juce::String(state->loadedEffects) + " DAW presets";
                    safe->setStatus(msg);
                    safe->showToast(msg);
                }
                else if (state->loadedMixerTracks > 0)
                {
                    const auto msg = "Loaded " + state->lastSourceDaw + " chain \"" + state->lastLabel + "\"";
                    safe->setStatus(msg);
                    safe->showToast(msg);
                }
                else
                {
                    safe->setStatus("Chain imported");
                    safe->showToast("Chain imported");
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
                safe->setStatus("Loading " + dawTag + " FX chain...");
            }
            else
            {
                safe->setStatus("Loading " + dawTag + " preset " + item.displayName + "...");
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
