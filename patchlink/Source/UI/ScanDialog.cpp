#include "ScanDialog.h"

ScanDialog::SourceRow::SourceRow(ScanDialog& o, size_t i) : owner(o), index(i)
{
    pathLabel.setFont(design::monoFont(11.0f));
    originLabel.setFont(design::uiFont(11.0f));
    originLabel.setColour(juce::Label::textColourId, design::secondary);
    rec.setTooltip("Include subfolders when scanning");
    addAndMakeVisible(on); addAndMakeVisible(pathLabel); addAndMakeVisible(originLabel);
    addAndMakeVisible(rec); addAndMakeVisible(del);
    on.onClick = [this] {
        auto s = owner.mgr.getSources();
        if (index < s.size()) { s[index].enabled = on.getToggleState(); owner.mgr.setSources(s); }
    };
    rec.onClick = [this] {
        auto s = owner.mgr.getSources();
        if (index < s.size()) { s[index].recursive = rec.getToggleState(); owner.mgr.setSources(s); }
    };
    del.onClick = [this] {
        auto s = owner.mgr.getSources();
        if (index < s.size()) { s.erase(s.begin() + (long)index); owner.mgr.setSources(s); }
        owner.rebuildRows();
    };
}

void ScanDialog::SourceRow::refresh(const PluginScanManager::Source& s)
{
    on.setToggleState(s.enabled, juce::dontSendNotification);
    pathLabel.setText(s.path, juce::dontSendNotification);
    originLabel.setText(s.origin, juce::dontSendNotification);
    rec.setToggleState(s.recursive, juce::dontSendNotification);
}

void ScanDialog::SourceRow::resized()
{
    auto r = getLocalBounds().reduced(2);
    on.setBounds(r.removeFromLeft(26));
    del.setBounds(r.removeFromRight(30));
    rec.setBounds(r.removeFromRight(96));
    originLabel.setBounds(r.removeFromRight(220));
    pathLabel.setBounds(r);
}

ScanDialog::ScanDialog(PluginScanManager& m)
    : mgr(m), progressBar(progressValue)
{
    titleLabel.setText("Scan Manager", juce::dontSendNotification);
    titleLabel.setFont(design::uiFont(20.0f, true));
    subLabel.setText("System folders + DAW folders + yours. Only checked folders are scanned — nothing is touched inside your DAWs.",
                     juce::dontSendNotification);
    subLabel.setFont(design::uiFont(12.0f));
    subLabel.setColour(juce::Label::textColourId, design::secondary);
    statusLabel.setFont(design::monoFont(11.0f));
    progLabel.setFont(design::monoFont(11.0f));

    addAndMakeVisible(titleLabel); addAndMakeVisible(subLabel);
    addAndMakeVisible(listHolder);
    addAndMakeVisible(progressBar); addAndMakeVisible(statusLabel); addAndMakeVisible(progLabel);
    for (auto* b : { &scanBtn, &rescanBtn, &rescanFailedBtn, &cancelBtn, &addFolderBtn, &closeBtn })
        addAndMakeVisible(b);

    resultsBox.setReadOnly(true);
    resultsBox.setFont(design::monoFont(11.0f));
    blBox.setReadOnly(true);
    blBox.setFont(design::monoFont(11.0f));
    resultsLabel.setText("Last scan", juce::dontSendNotification);
    blLabel.setText("Blacklisted (crashed or failed) — the scanner skips these. Remove one, then rescan to retry it.",
                    juce::dontSendNotification);
    resultsPage.addAndMakeVisible(resultsLabel); resultsPage.addAndMakeVisible(resultsBox);
    blacklistPage.addAndMakeVisible(blLabel); blacklistPage.addAndMakeVisible(blBox);
    blacklistPage.addAndMakeVisible(blClearBtn);
    tabs.addTab("Results", design::secondary, &resultsPage, false);
    tabs.addTab("Blacklist", design::secondary, &blacklistPage, false);
    addAndMakeVisible(tabs);

    scanBtn.onClick = [this] { mgr.startScan(false); startTimer(100); };
    rescanBtn.onClick = [this] { mgr.startScan(true); startTimer(100); };
    rescanFailedBtn.onClick = [this] {
        // rescan only what failed: temporarily re-run a full pass; failures get retried anyway
        mgr.startScan(false); startTimer(100);
    };
    cancelBtn.onClick = [this] { mgr.cancelScan(); };
    addFolderBtn.onClick = [this] {
        auto fc = std::make_shared<juce::FileChooser>("Add plugin folder", juce::File());
        fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, fc](const juce::FileChooser&) {
                auto d = fc->getResult();
                if (d.isDirectory()) { mgr.addSource({ d.getFullPathName(), "My folder", true, true }); rebuildRows(); }
            });
    };
    closeBtn.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0); };
    blClearBtn.onClick = [this] {
        auto sel = blBox.getHighlightedText();
        if (sel.isNotEmpty()) { mgr.getList().removeFromBlacklist(sel.trim()); refreshBlacklist(); }
        else juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Blacklist",
            "Select a line in the box first, then remove it.");
    };

    mgr.addChangeListener(this);
    rebuildRows();
    refreshResults();
    refreshBlacklist();
    if (mgr.isScanning()) startTimer(100);
}

ScanDialog::~ScanDialog()
{
    mgr.removeChangeListener(this);
    stopTimer();
}

void ScanDialog::paint(juce::Graphics& g)
{
    g.fillAll(design::canvas);
}

void ScanDialog::resized()
{
    auto r = getLocalBounds().reduced(14);
    titleLabel.setBounds(r.removeFromTop(28));
    subLabel.setBounds(r.removeFromTop(20));
    r.removeFromTop(6);
    auto btns = r.removeFromBottom(36);
    closeBtn.setBounds(btns.removeFromRight(90));
    btns.removeFromRight(8);
    cancelBtn.setBounds(btns.removeFromRight(90));
    btns.removeFromRight(8);
    rescanFailedBtn.setBounds(btns.removeFromRight(140));
    btns.removeFromRight(8);
    rescanBtn.setBounds(btns.removeFromRight(100));
    btns.removeFromRight(8);
    scanBtn.setBounds(btns.removeFromRight(120));
    btns.removeFromRight(8);
    addFolderBtn.setBounds(btns.removeFromLeft(120));
    r.removeFromBottom(6);
    tabs.setBounds(r.removeFromBottom(190));
    r.removeFromBottom(6);
    progLabel.setBounds(r.removeFromBottom(18));
    progressBar.setBounds(r.removeFromBottom(14));
    statusLabel.setBounds(r.removeFromBottom(18));
    listHolder.setBounds(r);
    int y = 0;
    for (auto* row : rows) { row->setBounds(0, y, listHolder.getWidth(), 30); y += 32; }
    resultsLabel.setBounds(6, 4, 300, 20);
    resultsBox.setBounds(6, 28, resultsPage.getWidth() - 12, resultsPage.getHeight() - 34);
    blLabel.setBounds(6, 4, resultsPage.getWidth() - 12, 32);
    blBox.setBounds(6, 40, blacklistPage.getWidth() - 12, blacklistPage.getHeight() - 80);
    blClearBtn.setBounds(6, blacklistPage.getHeight() - 34, 260, 28);
}

void ScanDialog::rebuildRows()
{
    rows.clear();
    auto srcs = mgr.getSources();
    for (size_t i = 0; i < srcs.size(); ++i)
    {
        auto* row = new SourceRow(*this, i);
        row->refresh(srcs[i]);
        listHolder.addAndMakeVisible(row);
        rows.add(row);
    }
    resized();
}

void ScanDialog::refreshResults()
{
    juce::String t;
    t << "Plugins indexed: " << mgr.getList().getNumTypes() << "\n";
    t << "Failed files:\n";
    auto failed = mgr.getFailedFiles();
    t << (failed.isEmpty() ? "(none)\n" : ("- " + failed.joinIntoString("\n- ") + "\n"));
    resultsBox.setText(t);
}

void ScanDialog::refreshBlacklist()
{
    juce::String t;
    for (auto f : mgr.getList().getBlacklistedFiles()) t << f << "\n";
    blBox.setText(t.isEmpty() ? "(empty — good news)" : t);
}

void ScanDialog::timerCallback()
{
    progressValue = mgr.getProgress();
    progressBar.repaint();
    progLabel.setText(juce::String((int)(progressValue * 100)) + "%", juce::dontSendNotification);
    statusLabel.setText(mgr.isScanning() ? ("Scanning: " + mgr.getCurrentFile()) : "Idle",
                        juce::dontSendNotification);
    if (!mgr.isScanning()) stopTimer();
    repaint();
}

void ScanDialog::changeListenerCallback(juce::ChangeBroadcaster*)
{
    stopTimer();
    progressValue = 1.0;
    statusLabel.setText("Done — " + juce::String(mgr.getList().getNumTypes()) + " plugins indexed",
                        juce::dontSendNotification);
    refreshResults();
    refreshBlacklist();
    rebuildRows();
    repaint();
}
