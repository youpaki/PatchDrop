#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Chain/ShareClient.h"
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
    setLookAndFeel(&lookAndFeel);
    setSize(1120, 720);
    setResizeLimits(840, 540, 3200, 2200);
    setResizable(true, true);
    setWantsKeyboardFocus(true);

    simpleButton.setTooltip("A serial chain from top to bottom");
    patcherButton.setTooltip("A free-form stereo routing canvas");
    simpleButton.onClick = [this] { setMode(Mode::simple); };
    patcherButton.onClick = [this] { setMode(Mode::patcher); };
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
    for (auto* button : { &simpleButton, &patcherButton, &libraryButton, &codeButton, &shareButton,
                          &importButton, &exportButton, &addButton })
        addAndMakeVisible(button);

    patchName.setText(proc.getPatchName(), juce::dontSendNotification);
    patchName.setFont(theme::font(17.0f, true));
    patchName.setEditable(true, true, false);
    patchName.setTooltip("Double-click to rename this chain");
    patchName.onTextChange = [this] {
        if (patchName.getText().trim().isNotEmpty()) proc.setPatchName(patchName.getText());
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

    proc.addChangeListener(this);
    proc.getScanManager().addChangeListener(this);
    setMode(proc.preferredMode == 2 ? Mode::patcher : Mode::simple);
    setStatus("Ready");
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

void PatchLinkEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshViews();
}

void PatchLinkEditor::refreshViews()
{
    const auto nodes = proc.getLoadedNodes();
    chainView.refresh(nodes);
    patcherView.refresh(nodes, proc.getWires());
    if (!patchName.hasKeyboardFocus(true)) patchName.setText(proc.getPatchName(), juce::dontSendNotification);
    chainInfo.setText(juce::String((int)nodes.size()) + (nodes.size() == 1 ? " plugin" : " plugins")
                      + "  /  STEREO", juce::dontSendNotification);
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

    auto toolbar = r.removeFromTop(theme::toolbarHeight).reduced(22, 10);
    addButton.setBounds(toolbar.removeFromRight(132));
    toolbar.removeFromRight(6);
    exportButton.setBounds(toolbar.removeFromRight(88));
    exportButton.setVisible(toolbar.getWidth() > 230);
    toolbar.removeFromRight(4);
    importButton.setBounds(toolbar.removeFromRight(88));
    importButton.setVisible(toolbar.getWidth() > 180);
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
    auto* dialog = new ShareDialog(mode, proc.graphToVar());
    overlay.reset(dialog);
    dialog->onDismiss = [safe = juce::Component::SafePointer<PatchLinkEditor>(this)] { if (safe) safe->dismissOverlay(); };

    dialog->onPublish = [safe = juce::Component::SafePointer<PatchLinkEditor>(this),
                         dialogSafe = juce::Component::SafePointer<ShareDialog>(dialog),
                         patch = proc.graphToVar(), base = apiBase](const juce::String& title,
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
                if (dialogSafe && safe->overlay.get() == dialogSafe.getComponent()) safe->dismissOverlay();
                safe->pluginWindows.clear();
                safe->setStatus("Loading " + (title.isNotEmpty() ? title : code) + "...");
                safe->proc.loadFromVarAsync(patch, [safe, code, title](juce::StringArray missing, juce::String loadError) {
                    if (!safe) return;
                    if (loadError.isNotEmpty())
                    {
                        safe->setStatus("Could not open " + code);
                        safe->showToast(loadError, true);
                        return;
                    }
                    safe->setStatus((title.isNotEmpty() ? title : code) + " is ready");
                    safe->refreshViews();
                    safe->showMissingPlugins(missing, title.isNotEmpty() ? title : code);
                });
            });
        }).detach();
    };

    dialog->setBounds(getLocalBounds());
    addAndMakeVisible(dialog);
    dialog->grabKeyboardFocus();
}

void PatchLinkEditor::doImport()
{
    auto chooser = std::make_shared<juce::FileChooser>("Open a PatchLink chain or FL Studio preset",
                                                      juce::File(), "*.patchlink;*.fst");
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
    auto chooser = std::make_shared<juce::FileChooser>("Save a PatchLink chain", juce::File(), "*.patchlink");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<PatchLinkEditor>(this), chooser](const juce::FileChooser&) mutable {
            if (!safe) return;
            auto file = chooser->getResult();
            if (file.getFullPathName().isEmpty()) return;
            if (file.getFileExtension().isEmpty()) file = file.withFileExtension("patchlink");
            juce::String error;
            if (!safe->proc.exportPatch(file, error))
            {
                safe->showToast(error, true);
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
    return file.hasFileExtension(".fst;.patchlink");
}

juce::String describeDraggedFiles(const juce::StringArray& files)
{
    int fstCount = 0;
    int patchCount = 0;
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (f.hasFileExtension(".fst")) ++fstCount;
        else if (f.hasFileExtension(".patchlink")) ++patchCount;
    }
    if (fstCount > 0 && patchCount == 0)
        return fstCount > 1
            ? "Drop FL Studio .fst presets to add to PatchLink"
            : "Drop FL Studio .fst preset (single effect or mixer track)";
    if (patchCount > 0 && fstCount == 0)
        return "Drop .patchlink file to open chain";
    return "Drop .fst presets or .patchlink files";
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

void PatchLinkEditor::fileDragEnter(const juce::StringArray& files, int, int)
{
    isDraggingFile = true;
    dragDropHint = describeDraggedFiles(files);
    repaint();
}

void PatchLinkEditor::fileDragMove(const juce::StringArray& files, int, int)
{
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

void PatchLinkEditor::importFiles(const juce::StringArray& files, int x, int y)
{
    struct PendingItem
    {
        juce::String displayName;
        bool isFst = false;
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
        juce::StringArray notes;
        juce::StringArray errors;
    };

    auto state = std::make_shared<BatchState>();
    // Parse dropped .fst / .patchlink files into RAM immediately before temporary drag files are cleaned up.
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (!isSupportedDropFile(file))
            continue;

        PendingItem item;
        item.displayName = file.getFileNameWithoutExtension();

        if (!file.existsAsFile())
        {
            item.error = "File not found: " + file.getFileName();
        }
        else if (file.hasFileExtension(".fst"))
        {
            item.isFst = true;
            item.fst = flstudio::parseFstFile(file);
            if (item.fst.trackName.isNotEmpty())
                item.displayName = item.fst.trackName;
        }
        else
        {
            const auto txt = file.loadFileAsString();
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

        state->items.push_back(std::move(item));
    }

    if (state->items.empty())
        return;

    auto safe = juce::Component::SafePointer<PatchLinkEditor>(this);
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
                                         state->loadedMixerTracks > 0 ? "FL Studio mixer track imported"
                                         : state->loadedChains > 0   ? "Chain imported"
                                                                     : "FL Studio preset import");
                return;
            }

            if (state->loadedEffects == 1 && state->loadedChains == 0 && state->loadedMixerTracks == 0)
            {
                safe->setStatus("Added " + state->lastLabel);
                safe->showToast("Added " + state->lastLabel + " preset");
            }
            else if (state->loadedEffects > 1 && state->loadedChains == 0 && state->loadedMixerTracks == 0)
            {
                const auto msg = "Added " + juce::String(state->loadedEffects) + " FL Studio presets";
                safe->setStatus(msg);
                safe->showToast(msg);
            }
            else if (state->loadedMixerTracks > 0)
            {
                const auto msg = "Loaded FL track \"" + state->lastLabel + "\"";
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

        if (!item.isFst)
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
            state->errors.add(item.fst.error.isNotEmpty() ? item.fst.error : ("Invalid .fst: " + item.displayName));
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

        if (isMixerOrMulti)
        {
            safe->pluginWindows.clear();
            safe->setStatus("Loading FL Studio mixer track...");
        }
        else
        {
            safe->setStatus("Loading FL Studio preset " + item.displayName + "...");
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
}
