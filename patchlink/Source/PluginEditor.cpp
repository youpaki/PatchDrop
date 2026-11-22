#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Chain/ShareClient.h"
#include "Core/DesignTokens.h"
#include "UI/ScanDialog.h"

// ---------- Vue Simple : liste verticale ----------
struct PatchLinkEditor::SimpleList : juce::Component
{
    PatchLinkEditor& ed;
    PatchLinkProcessor& proc;
    struct Row : juce::Component
    {
        juce::Label name;
        juce::ToggleButton bypass { "BYPASS" };
        juce::TextButton del { "X" };
        juce::AudioProcessorGraph::NodeID id;
        juce::String indexText;
        PatchLinkProcessor& proc;
        Row(PatchLinkProcessor& p, PatchLinkProcessor::LoadedNode n, int idx) : proc(p), id(n.id)
        {
            indexText = juce::String::formatted("%02d", idx + 1);
            name.setText(n.desc.name.isNotEmpty() ? n.desc.name : ("Node " + juce::String(n.id.uid)), juce::dontSendNotification);
            bypass.setToggleState(n.bypass, juce::dontSendNotification);
            addAndMakeVisible(name); addAndMakeVisible(bypass); addAndMakeVisible(del);
            bypass.onClick = [this] { proc.setNodeBypass(id, bypass.getToggleState()); };
            del.onClick = [this] { proc.removeNode(id); };
        }
        void paint(juce::Graphics& g) override
        {
            g.setColour(design::card);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), design::corner);
            g.setColour(design::yellow);
            g.fillRect(0, 6, 3, getHeight() - 12);
            g.setColour(design::yellow);
            g.setFont(PatchFonts::mono(11.0f));
            g.drawText(indexText, 10, 0, 30, getHeight(), juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced(4);
            del.setBounds(r.removeFromRight(36));
            bypass.setBounds(r.removeFromRight(70));
            r.removeFromLeft(38);
            name.setBounds(r);
        }
    };
    juce::OwnedArray<Row> rows;
    SimpleList(PatchLinkEditor& e, PatchLinkProcessor& p) : ed(e), proc(p) {}
    void refresh()
    {
        rows.clear();
        int y = 0, idx = 0;
        for (auto& n : proc.getLoadedNodes())
        {
            auto* r = new Row(proc, n, idx++);
            r->name.setFont(PatchFonts::ui(13.0f));
            r->bypass.setColour(juce::ToggleButton::textColourId, design::secondary);
            addAndMakeVisible(r);
            r->setBounds(0, y, getWidth(), design::simpleRowH);
            rows.add(r);
            y += design::simpleRowH + 2;
        }
        setSize(getWidth(), juce::jmax(y, 100));
        resized();
    }
    void resized() override
    {
        int y = 0;
        for (auto* r : rows) { r->setBounds(0, y, getWidth(), design::simpleRowH); y += design::simpleRowH + 2; }
    }
};

// ---------- Vue Full : canvas nodal minimal ----------
struct PatchLinkEditor::FullCanvas : juce::Component
{
    PatchLinkEditor& ed;
    PatchLinkProcessor& proc;
    juce::Array<uint32_t> selected; // uid
    juce::ComponentDragger dragger;
    juce::AudioProcessorGraph::NodeID dragId{ 0 };
    juce::Point<int> dragStart;
    static constexpr int W = 150, H = 62;

    FullCanvas(PatchLinkEditor& e, PatchLinkProcessor& p) : ed(e), proc(p) {}

    juce::Rectangle<int> boxFor(uint32_t uid)
    {
        for (auto& n : proc.getLoadedNodes())
            if (n.id.uid == uid) return { n.x, n.y, W, H };
        return {};
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(design::bg);
        Halftone::dots(g, getLocalBounds(), juce::Colour(0x0dffe600), 22, 1.4f);

        // wires: signal yellow
        g.setColour(design::audio);
        for (auto& wline : proc.getWires())
        {
            auto a = boxFor(wline.src), b = boxFor(wline.dst);
            if (a.isEmpty() || b.isEmpty()) continue;
            auto p1 = a.getCentre().toFloat();
            auto p2 = b.getCentre().toFloat();
            g.drawLine(p1.x, p1.y, p2.x, p2.y, 2.0f);
        }
        // IN / OUT
        g.setColour(design::secondary);
        g.setFont(PatchFonts::mono(11.0f));
        g.drawRoundedRectangle(10, 10, 90, 40, design::corner, 1.5f);
        g.drawText("IN", 10, 10, 90, 40, juce::Justification::centred);
        g.drawRoundedRectangle((float)getWidth() - 100, 10, 90, 40, design::corner, 1.5f);
        g.drawText("OUT", getWidth() - 100, 10, 90, 40, juce::Justification::centred);

        // nodes
        for (auto& n : proc.getLoadedNodes())
        {
            bool sel = selected.contains(n.id.uid);
            auto r = juce::Rectangle<int>(n.x, n.y, W, H);
            g.setColour(design::card);
            g.fillRoundedRectangle(r.toFloat(), design::corner);
            g.setColour(sel ? design::yellow : design::line);
            g.drawRoundedRectangle(r.toFloat(), design::corner, sel ? 2.5f : 1.0f);
            if (sel) { g.setColour(design::yellow); g.fillRect(r.getX(), r.getY(), 5, r.getHeight()); }
            g.setColour(design::text);
            g.setFont(PatchFonts::ui(13.0f));
            juce::String nm = n.desc.name.isNotEmpty() ? n.desc.name : ("Node " + juce::String(n.id.uid));
            if (n.bypass) nm += " [BYPASSED]";
            g.drawText(nm, r.reduced(10, 4), juce::Justification::topLeft);
            g.setFont(PatchFonts::mono(10.0f));
            g.setColour(design::secondary);
            g.drawText(n.desc.pluginFormatName + "  // SHIFT+CLICK 2 NODES = WIRE",
                       r.reduced(10, 4), juce::Justification::bottomLeft);
        }
        if (proc.getLoadedNodes().empty())
        {
            g.setColour(design::secondary);
            g.setFont(PatchFonts::ui(14.0f));
            g.drawText("DOUBLE-CLICK TO ADD A PLUGIN — SHIFT+CLICK 2 NODES TO WIRE",
                       getLocalBounds(), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        uint32_t hit = 0;
        for (auto& n : proc.getLoadedNodes())
            if (juce::Rectangle<int>(n.x, n.y, W, H).contains(e.position.toInt()))
                hit = n.id.uid;
        if (hit != 0)
        {
            if (e.mods.isShiftDown() && selected.size() == 1 && !selected.contains(hit))
            {
                // relie selected[0] -> hit
                proc.connectNodes(juce::AudioProcessorGraph::NodeID(selected.getFirst()),
                                  0, juce::AudioProcessorGraph::NodeID(hit), 0);
                proc.connectNodes(juce::AudioProcessorGraph::NodeID(selected.getFirst()),
                                  1, juce::AudioProcessorGraph::NodeID(hit), 1);
                selected.clear();
                repaint();
                return;
            }
            if (!e.mods.isCommandDown() && !e.mods.isCtrlDown()) selected.clear();
            if (!selected.contains(hit)) selected.add(hit);
            dragId = juce::AudioProcessorGraph::NodeID(hit);
            dragStart = e.position.toInt();
            dragger.startDraggingComponent(this, e);
        }
        else selected.clear();
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragId.uid == 0) return;
        // déplace le noeud : delta depuis le départ
        auto delta = e.position.toInt() - dragStart;
        for (auto& n : proc.getLoadedNodes())
            if (n.id.uid == dragId.uid)
                proc.setNodePosition(n.id, n.x + delta.x, n.y + delta.y);
        dragStart = e.position.toInt();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        uint32_t hit = 0;
        for (auto& n : proc.getLoadedNodes())
            if (juce::Rectangle<int>(n.x, n.y, W, H).contains(e.position.toInt()))
                hit = n.id.uid;
        if (hit == 0) ed.pickAndAddPlugin(e.position.x, e.position.y);
    }
};

// ---------- Editeur ----------
PatchLinkEditor::PatchLinkEditor(PatchLinkProcessor& p)
    : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&hazardLaf);
    setSize(860, 600);
    setResizable(true, true);

    entryTitle.setText("PATCHLINK", juce::dontSendNotification);
    entryTitle.setFont(PatchFonts::display(52.0f));
    entryTitle.setColour(juce::Label::textColourId, design::yellow);
    entrySub.setText("LEVE AUDIO CHAINER — SIMPLE CHAIN OR FULL PATCHER. SAME ENGINE.",
                     juce::dontSendNotification);
    entrySub.setFont(PatchFonts::mono(12.0f));
    entrySimple.setButtonText("SIMPLE MODE\nVERTICAL CHAIN. FAST.");
    entryFull.setButtonText("PATCHER MODE\nFULL NODAL LIKE FL PATCHER");
    entryAsk.setButtonText("Always ask on startup");
    entryAsk.setToggleState(true, juce::dontSendNotification);
    for (auto* b : { &btnSimple, &btnFull, &btnAdd, &btnImport, &btnExport, &btnScan, &btnShare, &btnOpenCode,
                     &entrySimple, &entryFull })
    {
        b->setColour(juce::TextButton::buttonColourId, design::raised);
        b->setColour(juce::TextButton::textColourOffId, design::text);
        b->setColour(juce::TextButton::buttonOnColourId, design::audio);
    }
    entryTitle.setColour(juce::Label::textColourId, design::audio);
    statusLabel.setFont(design::monoFont(12.0f));
    addAndMakeVisible(entryTitle); addAndMakeVisible(entrySub);
    addAndMakeVisible(entrySimple); addAndMakeVisible(entryFull); addAndMakeVisible(entryAsk);

    for (auto* b : { &btnSimple, &btnFull, &btnAdd, &btnImport, &btnExport, &btnScan, &btnShare, &btnOpenCode })
        addAndMakeVisible(b);
    statusLabel.setFont(PatchFonts::mono(12.0f));
    statusLabel.setColour(juce::Label::textColourId, design::yellow);
    addAndMakeVisible(statusLabel);

    simpleView = std::make_unique<juce::Viewport>();
    simpleList = std::make_unique<SimpleList>(*this, proc);
    simpleView->setViewedComponent(simpleList.get(), false);
    addAndMakeVisible(simpleView.get());

    fullCanvas = std::make_unique<FullCanvas>(*this, proc);
    addAndMakeVisible(fullCanvas.get());

    entrySimple.onClick = [this] {
        askAtStartup = entryAsk.getToggleState();
        if (!askAtStartup) proc.preferredMode = 1;
        setMode(Mode::Simple);
    };
    entryFull.onClick = [this] {
        askAtStartup = entryAsk.getToggleState();
        if (!askAtStartup) proc.preferredMode = 2;
        setMode(Mode::Full);
    };
    btnSimple.onClick = [this] { setMode(Mode::Simple); };
    btnFull.onClick = [this] { setMode(Mode::Full); };
    btnAdd.onClick = [this] { pickAndAddPlugin(120, 120); };
    btnImport.onClick = [this] { doImport(); };
    btnExport.onClick = [this] { doExport(); };
    btnScan.onClick = [this] { openScanDialog(); };
    btnShare.onClick = [this] { doShareViaCode(); };
    btnOpenCode.onClick = [this] { doOpenViaCode(); };

    // Si l'utilisateur a mémorisé un mode, on saute l'entrée
    if (proc.preferredMode == 1) setMode(Mode::Simple);
    else if (proc.preferredMode == 2) setMode(Mode::Full);
    else setMode(Mode::Entry);

    startTimer(500);
}

PatchLinkEditor::~PatchLinkEditor() { setLookAndFeel(nullptr); stopTimer(); }

void PatchLinkEditor::setMode(Mode m)
{
    mode = m;
    if (m == Mode::Simple) { proc.autoConnectSerial = true; proc.rebuildSerialIfNeeded(); }
    if (m == Mode::Full) proc.autoConnectSerial = false;
    refreshAll();
    resized();
}

void PatchLinkEditor::refreshAll()
{
    if (simpleList) simpleList->refresh();
    if (fullCanvas) fullCanvas->repaint();
    auto n = proc.getLoadedNodes().size();
    juce::String base = juce::String((int)n) + " plugin(s)  ·  "
                        + (mode == Mode::Simple ? "Simple (auto-series)" : mode == Mode::Full ? "Patcher (manual)" : "");
    if (proc.getKnownList().getNumTypes() == 0)
        base += "  —  No plugins indexed yet: click Scan (your DAW folders are pre-checked)";
    statusLabel.setText(base, juce::dontSendNotification);
    repaint();
}

void PatchLinkEditor::timerCallback() { refreshAll(); }

void PatchLinkEditor::paint(juce::Graphics& g)
{
    g.fillAll(design::bg);
    Halftone::dots(g, getLocalBounds(), juce::Colour(0x14ffffff), 18, 1.5f);
    // hazard strip up top
    Halftone::stripes(g, juce::Rectangle<int>(0, 0, getWidth(), 5));
    if (mode == Mode::Entry)
    {
        // giant watermark
        g.setColour(juce::Colour(0x0ffffe600));
        g.setFont(PatchFonts::display(120.0f));
        g.drawText("LEVE", getLocalBounds().removeFromBottom(200), juce::Justification::centredBottom, true);
    }
}

void PatchLinkEditor::resized()
{
    bool entry = (mode == Mode::Entry);
    for (auto* c : { (juce::Component*)&entryTitle, (juce::Component*)&entrySub,
                     (juce::Component*)&entrySimple, (juce::Component*)&entryFull, (juce::Component*)&entryAsk })
        c->setVisible(entry);
    for (auto* c : { (juce::Component*)&btnSimple, (juce::Component*)&btnFull, (juce::Component*)&btnAdd,
                     (juce::Component*)&btnImport, (juce::Component*)&btnExport, (juce::Component*)&btnScan,
                     (juce::Component*)&btnShare, (juce::Component*)&btnOpenCode,
                     (juce::Component*)&statusLabel })
        c->setVisible(!entry);
    if (simpleView) simpleView->setVisible(!entry && mode == Mode::Simple);
    if (fullCanvas) fullCanvas->setVisible(!entry && mode == Mode::Full);

    auto r = getLocalBounds();
    if (entry)
    {
        entryTitle.setBounds(r.removeFromTop(90).reduced(20, 10));
        entrySub.setBounds(r.removeFromTop(30).reduced(20, 0));
        auto mid = r.reduced(40, 20);
        int hw = mid.getWidth() / 2 - 10;
        entrySimple.setBounds(mid.removeFromLeft(hw));
        mid.removeFromLeft(20);
        entryFull.setBounds(mid);
        entryAsk.setBounds(getLocalBounds().removeFromBottom(30).reduced(20, 4));
        return;
    }
    auto top = r.removeFromTop(64).reduced(6, 4);
    btnSimple.setBounds(top.removeFromLeft(80)); top.removeFromLeft(4);
    btnFull.setBounds(top.removeFromLeft(80)); top.removeFromLeft(12);
    btnAdd.setBounds(top.removeFromLeft(80)); top.removeFromLeft(4);
    btnScan.setBounds(top.removeFromLeft(60)); top.removeFromLeft(12);
    btnImport.setBounds(top.removeFromLeft(70)); top.removeFromLeft(4);
    btnExport.setBounds(top.removeFromLeft(70)); top.removeFromLeft(4);
    btnShare.setBounds(top.removeFromLeft(120)); top.removeFromLeft(4);
    btnOpenCode.setBounds(top.removeFromLeft(120)); top.removeFromLeft(12);
    statusLabel.setBounds(top);
    if (mode == Mode::Simple && simpleView)
        simpleView->setBounds(r.reduced(10));
    if (mode == Mode::Full && fullCanvas)
        fullCanvas->setBounds(r.reduced(10));
}

void PatchLinkEditor::pickAndAddPlugin(int x, int y)
{
    auto types = proc.getKnownList().getTypes();
    if (types.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No plugins",
            "Nothing indexed yet — click Scan first (system + DAW folders are pre-checked).");
        return;
    }
    juce::PopupMenu m;
    std::sort(types.begin(), types.end(),
              [](auto& a, auto& b) { return a.name < b.name; });
    int id = 1;
    std::map<int, juce::PluginDescription> map;
    for (auto& t : types)
    {
        if (id > 500) break; // limite du menu
        m.addItem(id, t.name + "  (" + t.pluginFormatName + ")");
        map[id] = t;
        ++id;
    }
    m.showMenuAsync(juce::PopupMenu::Options(), [this, map, x, y](int r) {
        if (r > 0 && map.count(r))
        {
            statusLabel.setText("Loading plugin…", juce::dontSendNotification);
            proc.addPluginAsync(map.at(r), x, y, [this](const juce::String& err) {
                if (err.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load", err);
                refreshAll();
            });
        }
    });
}

void PatchLinkEditor::doImport()
{
    auto fc = std::make_shared<juce::FileChooser>("Importer un patch", juce::File(), "*.patchlink");
    fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc](const juce::FileChooser&) {
            auto f = fc->getResult();
            if (f.existsAsFile())
            {
                statusLabel.setText("Loading patch…", juce::dontSendNotification);
                proc.importPatchAsync(f, [this](juce::StringArray missing, juce::String err) {
                    if (err.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Import", err);
                    else
                        refreshAll();
                    if (missing.size() > 0)
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Missing plugins",
                            "Not found on your machine:\n- " + missing.joinIntoString("\n- "));
                });
            }
        });
}

void PatchLinkEditor::doExport()
{
    auto fc = std::make_shared<juce::FileChooser>("Exporter le patch", juce::File(), "*.patchlink");
    fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc](const juce::FileChooser&) {
            auto f = fc->getResult();
            if (f.getFullPathName().isNotEmpty())
            {
                if (f.getFileExtension().isEmpty()) f = f.withFileExtension("patchlink");
                juce::String err;
                if (!proc.exportPatch(f, err))
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export", err);
            }
        });
}

void PatchLinkEditor::openScanDialog()
{
    scanWindow = std::make_unique<juce::DialogWindow>("Scan Manager", design::canvas, true, true);
    scanWindow->setContentOwned(new ScanDialog(proc.getScanManager()), true);
    scanWindow->setResizable(true, true);
    scanWindow->centreAroundComponent(this, 720, 620);
    scanWindow->setVisible(true);
}

void PatchLinkEditor::doShareViaCode()
{
    statusLabel.setText("Publishing code...", juce::dontSendNotification);
    juce::String err;
    juce::String code = ShareClient::publish(proc.graphToVar(), "PatchLink chain", "me", "", apiBase, err);
    if (code.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "PatchDrop", err);
        refreshAll();
        return;
    }
    juce::SystemClipboard::copyTextToClipboard(code);
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "PatchDrop",
        "Your code: " + code + "\n(copied — send it to your friend)\nHe clicks Open via code in his instance.");
    statusLabel.setText("Shared: " + code, juce::dontSendNotification);
}

void PatchLinkEditor::doOpenViaCode()
{
    auto* aw = new juce::AlertWindow("PatchDrop", "Paste your friend's code:", juce::AlertWindow::QuestionIcon);
    aw->addTextEditor("code", "", "K7Q2XA");
    aw->addButton("Open", 1);
    aw->addButton("Cancel", 0);
    aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int r) {
        juce::String code;
        if (r != 0) code = aw->getTextEditor("code")->getText();
        delete aw;
        if (r == 0 || code.trim().isEmpty()) return;
        statusLabel.setText("Fetching code…", juce::dontSendNotification);
        juce::var patch; juce::String title, err;
        if (!ShareClient::fetch(code, apiBase, patch, title, err))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "PatchDrop", err);
            return;
        }
        statusLabel.setText("Loading patch…", juce::dontSendNotification);
        proc.loadFromVarAsync(patch, [this, code, title](juce::StringArray missing, juce::String loadErr) {
            if (loadErr.isNotEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "PatchDrop", "Patch unreadable.");
                return;
            }
            refreshAll();
            juce::String msg = "Loaded \"" + title + "\" (" + code.toUpperCase().trim() + ").";
            if (missing.size() > 0) msg += "\nMissing plugins:\n- " + missing.joinIntoString("\n- ");
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "PatchDrop", msg);
        });
    }));
}
