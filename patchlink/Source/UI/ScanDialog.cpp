#include "ScanDialog.h"

ScanDialog::SourceRow::SourceRow(ScanDialog& dialog, std::size_t rowIndex)
    : owner(dialog), index(rowIndex)
{
    enabledButton.setTooltip("Include this folder when scanning");
    pathLabel.setFont(theme::font(12.5f));
    pathLabel.setColour(juce::Label::textColourId, theme::text);
    pathLabel.setMinimumHorizontalScale(0.55f);
    pathLabel.setInterceptsMouseClicks(false, false);

    originLabel.setFont(theme::font(10.5f));
    originLabel.setColour(juce::Label::textColourId, theme::muted);
    originLabel.setJustificationType(juce::Justification::centredRight);
    originLabel.setMinimumHorizontalScale(0.65f);
    originLabel.setInterceptsMouseClicks(false, false);

    recursiveButton.setTooltip("Also scan folders inside this folder");
    deleteButton.setTooltip("Remove this folder from scan sources");

    addAndMakeVisible(enabledButton);
    addAndMakeVisible(pathLabel);
    addAndMakeVisible(originLabel);
    addAndMakeVisible(recursiveButton);
    addAndMakeVisible(deleteButton);

    enabledButton.onClick = [this]
    {
        const auto enabled = enabledButton.getToggleState();
        owner.setSourceEnabled(index, sourcePath, enabled);
        pathLabel.setColour(juce::Label::textColourId, enabled ? theme::text : theme::muted);
        repaint();
    };
    recursiveButton.onClick = [this]
    {
        owner.setSourceRecursive(index, sourcePath, recursiveButton.getToggleState());
    };
    deleteButton.onClick = [this]
    {
        owner.removeSource(index, sourcePath);
    };
}

void ScanDialog::SourceRow::refresh(const PluginScanManager::Source& source)
{
    sourcePath = source.path;
    enabledButton.setToggleState(source.enabled, juce::dontSendNotification);
    recursiveButton.setToggleState(source.recursive, juce::dontSendNotification);
    pathLabel.setText(source.path, juce::dontSendNotification);
    pathLabel.setTooltip(source.path);
    pathLabel.setColour(juce::Label::textColourId, source.enabled ? theme::text : theme::muted);
    const auto origin = source.origin.isNotEmpty() ? source.origin : juce::String("Custom folder");
    originLabel.setText(origin, juce::dontSendNotification);
    originLabel.setTooltip(origin);
}

void ScanDialog::SourceRow::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(3.0f, 1.0f);
    g.setColour(index % 2 == 0 ? theme::card.withAlpha(0.72f) : theme::card.withAlpha(0.44f));
    g.fillRoundedRectangle(area, theme::smallRadius);
    g.setColour(enabledButton.getToggleState() ? theme::accent.withAlpha(0.48f)
                                               : theme::faint.withAlpha(0.35f));
    g.fillRoundedRectangle(area.getX(), area.getY() + 8.0f, 2.0f,
                           juce::jmax(0.0f, area.getHeight() - 16.0f), 1.0f);
}

void ScanDialog::SourceRow::resized()
{
    auto area = getLocalBounds().reduced(12, 0);
    enabledButton.setBounds(area.removeFromLeft(26).withSizeKeepingCentre(26, 30));

    deleteButton.setBounds(area.removeFromRight(34).withSizeKeepingCentre(32, 32));
    area.removeFromRight(6);

    const auto showRecursive = getWidth() >= 330;
    recursiveButton.setVisible(showRecursive);
    if (showRecursive)
    {
        recursiveButton.setBounds(area.removeFromRight(102).withSizeKeepingCentre(102, 30));
        area.removeFromRight(8);
    }

    const auto showOrigin = getWidth() >= 540;
    originLabel.setVisible(showOrigin);
    if (showOrigin)
    {
        const auto originWidth = juce::jlimit(130, 210, getWidth() / 4);
        originLabel.setBounds(area.removeFromRight(originWidth));
        area.removeFromRight(12);
    }

    pathLabel.setBounds(area.withTrimmedRight(4));
}

ScanDialog::ScanDialog(PluginScanManager& manager)
    : mgr(manager), progressBar(progressValue)
{
    titleLabel.setText("Scan Manager", juce::dontSendNotification);
    titleLabel.setFont(theme::font(22.0f, true));
    titleLabel.setColour(juce::Label::textColourId, theme::text);
    subtitleLabel.setText("Choose the folders to include. Only enabled sources are scanned; your DAW configuration is never changed.",
                          juce::dontSendNotification);
    subtitleLabel.setFont(theme::font(12.5f));
    subtitleLabel.setColour(juce::Label::textColourId, theme::muted);
    subtitleLabel.setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);

    sourcesLabel.setText("SCAN SOURCES", juce::dontSendNotification);
    sourcesLabel.setFont(theme::font(11.0f, true));
    sourcesLabel.setColour(juce::Label::textColourId, theme::muted);
    sourceCountLabel.setFont(theme::mono(10.0f));
    sourceCountLabel.setColour(juce::Label::textColourId, theme::faint);
    sourceCountLabel.setJustificationType(juce::Justification::centredRight);
    emptySourcesLabel.setText("No scan sources yet.\nAdd a plugin folder to get started.", juce::dontSendNotification);
    emptySourcesLabel.setFont(theme::font(12.5f));
    emptySourcesLabel.setColour(juce::Label::textColourId, theme::muted);
    emptySourcesLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(sourcesLabel);
    addAndMakeVisible(sourceCountLabel);
    addChildComponent(emptySourcesLabel);

    sourcesViewport.setViewedComponent(&sourceListContent, false);
    sourcesViewport.setScrollBarsShown(true, false, true, false);
    sourcesViewport.setScrollBarThickness(9);
    addAndMakeVisible(sourcesViewport);

    statusLabel.setFont(theme::font(12.5f, true));
    statusLabel.setColour(juce::Label::textColourId, theme::text);
    scanFoundLabel.setFont(theme::mono(10.5f));
    scanFoundLabel.setColour(juce::Label::textColourId, theme::muted);
    scanFoundLabel.setJustificationType(juce::Justification::centredRight);
    currentFileLabel.setFont(theme::mono(10.0f));
    currentFileLabel.setColour(juce::Label::textColourId, theme::faint);
    currentFileLabel.setMinimumHorizontalScale(0.55f);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(scanFoundLabel);
    addAndMakeVisible(progressBar);
    addAndMakeVisible(currentFileLabel);

    resultsHeading.setText("SCAN RESULTS", juce::dontSendNotification);
    resultsHeading.setFont(theme::font(11.0f, true));
    resultsHeading.setColour(juce::Label::textColourId, theme::muted);
    resultsSummary.setFont(theme::font(12.0f, true));
    resultsSummary.setColour(juce::Label::textColourId, theme::text);
    failedFilesLabel.setFont(theme::font(10.5f, true));
    failedFilesLabel.setColour(juce::Label::textColourId, theme::faint);
    addAndMakeVisible(resultsHeading);
    addAndMakeVisible(resultsSummary);
    addAndMakeVisible(failedFilesLabel);

    blacklistHeading.setText("BLACKLIST", juce::dontSendNotification);
    blacklistHeading.setFont(theme::font(11.0f, true));
    blacklistHeading.setColour(juce::Label::textColourId, theme::muted);
    blacklistCountLabel.setFont(theme::mono(10.0f));
    blacklistCountLabel.setColour(juce::Label::textColourId, theme::faint);
    blacklistCountLabel.setJustificationType(juce::Justification::centredRight);
    blacklistHelpLabel.setText("Crashed or failed plugins are skipped.\nSelect complete lines, then remove them to retry.",
                               juce::dontSendNotification);
    blacklistHelpLabel.setFont(theme::font(10.5f));
    blacklistHelpLabel.setColour(juce::Label::textColourId, theme::muted);
    blacklistHelpLabel.setMinimumHorizontalScale(0.75f);
    addAndMakeVisible(blacklistHeading);
    addAndMakeVisible(blacklistCountLabel);
    addAndMakeVisible(blacklistHelpLabel);

    auto setupEditor = [](juce::TextEditor& editor, const juce::String& emptyText)
    {
        editor.setMultiLine(true);
        editor.setReadOnly(true);
        editor.setScrollbarsShown(true);
        editor.setFont(theme::mono(10.5f));
        editor.setIndents(8, 6);
        editor.setColour(juce::TextEditor::backgroundColourId, theme::background);
        editor.setColour(juce::TextEditor::textColourId, theme::muted);
        editor.setTextToShowWhenEmpty(emptyText, theme::faint);
    };
    setupEditor(resultsBox, "No failures reported by the last scan.");
    setupEditor(blacklistBox, "No blacklisted plugins.");
    addAndMakeVisible(resultsBox);
    addAndMakeVisible(blacklistBox);

    scanButton.onClick = [this] { beginScan(false); };
    rescanButton.onClick = [this] { beginScan(true); };
    cancelButton.onClick = [this]
    {
        if (!scanning || cancelRequested)
            return;
        cancelRequested = true;
        mgr.cancelScan();
        refreshScanProgress();
        updateActionStates();
    };
    addFolderButton.onClick = [this] { addFolder(); };
    removeBlacklistButton.onClick = [this] { removeSelectedBlacklistEntries(); };
    closeButton.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        {
            if (window->isCurrentlyModal())
                window->exitModalState(0);
            else
                window->setVisible(false);
        }
        else
        {
            setVisible(false);
        }
    };

    scanButton.setTooltip("Scan new or changed plugins in enabled folders");
    rescanButton.setTooltip("Rescan every plugin in enabled folders");
    cancelButton.setTooltip("Stop after the current file");
    removeBlacklistButton.setTooltip("Remove selected entries so they can be scanned again");
    closeButton.setTooltip("Close Scan Manager");

    addAndMakeVisible(addFolderButton);
    addAndMakeVisible(scanButton);
    addAndMakeVisible(rescanButton);
    addChildComponent(cancelButton);
    addAndMakeVisible(closeButton);
    addAndMakeVisible(removeBlacklistButton);

    mgr.addChangeListener(this);
    scanning = mgr.isScanning();
    rebuildRows();
    refreshResults();
    refreshBlacklist();
    refreshScanProgress();
    updateActionStates();
    if (scanning)
        startTimer(80);
}

ScanDialog::~ScanDialog()
{
    stopTimer();
    mgr.removeChangeListener(this);
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        if (&window->getLookAndFeel() == &lookAndFeel)
            window->setLookAndFeel(nullptr);
}

void ScanDialog::parentHierarchyChanged()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->setLookAndFeel(&lookAndFeel);
}

void ScanDialog::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::accent);
    g.fillRect(0, 0, getWidth(), 2);

    auto drawPanel = [&g](juce::Rectangle<int> area)
    {
        if (area.isEmpty())
            return;
        g.setColour(theme::panel);
        g.fillRoundedRectangle(area.toFloat(), theme::radius);
        g.setColour(theme::lineSoft);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), theme::radius, 1.0f);
    };
    drawPanel(sourcePanelBounds);
    drawPanel(resultsPanelBounds);
    drawPanel(blacklistPanelBounds);
}

void ScanDialog::resized()
{
    auto area = getLocalBounds().reduced(20, 18);
    titleLabel.setBounds(area.removeFromTop(31));
    subtitleLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(12);

    auto footer = area.removeFromBottom(38);
    area.removeFromBottom(12);
    closeButton.setBounds(footer.removeFromRight(92));
    footer.removeFromRight(8);
    if (scanning)
    {
        cancelButton.setBounds(footer.removeFromRight(128));
    }
    else
    {
        scanButton.setBounds(footer.removeFromRight(138));
        footer.removeFromRight(8);
        rescanButton.setBounds(footer.removeFromRight(120));
        footer.removeFromRight(8);
    }
    addFolderButton.setBounds(footer.removeFromLeft(122));

    auto progressArea = area.removeFromBottom(64);
    area.removeFromBottom(12);
    auto progressHeading = progressArea.removeFromTop(22);
    scanFoundLabel.setBounds(progressHeading.removeFromRight(170));
    statusLabel.setBounds(progressHeading.withTrimmedRight(12));
    progressBar.setBounds(progressArea.removeFromTop(18));
    progressArea.removeFromTop(4);
    currentFileLabel.setBounds(progressArea.removeFromTop(20));

    const auto stackedDetails = area.getWidth() < 540;
    const auto desiredDetailsHeight = stackedDetails
        ? juce::jlimit(210, 350, area.proportionOfHeight(0.62f))
        : juce::jlimit(136, 236, area.proportionOfHeight(0.45f));
    const auto detailsHeight = juce::jmin(desiredDetailsHeight, juce::jmax(0, area.getHeight() - 110));
    auto detailsArea = area.removeFromBottom(detailsHeight);
    area.removeFromBottom(12);
    layoutDetailsPanels(detailsArea);

    sourcePanelBounds = area;
    auto sourceContent = area.reduced(12, 10);
    auto sourceHeading = sourceContent.removeFromTop(24);
    sourceCountLabel.setBounds(sourceHeading.removeFromRight(190));
    sourcesLabel.setBounds(sourceHeading.withTrimmedRight(10));
    sourceContent.removeFromTop(4);
    sourcesViewport.setBounds(sourceContent);
    emptySourcesLabel.setBounds(sourceContent.reduced(18));
    layoutSourceRows();
}

void ScanDialog::layoutDetailsPanels(juce::Rectangle<int> area)
{
    if (area.getWidth() >= 540)
    {
        const auto secondWidth = (area.getWidth() - 12) / 2;
        blacklistPanelBounds = area.removeFromRight(secondWidth);
        area.removeFromRight(12);
        resultsPanelBounds = area;
    }
    else
    {
        const auto secondHeight = (area.getHeight() - 12) / 2;
        blacklistPanelBounds = area.removeFromBottom(juce::jmax(0, secondHeight));
        area.removeFromBottom(12);
        resultsPanelBounds = area;
    }

    auto results = resultsPanelBounds.reduced(12, 10);
    resultsHeading.setBounds(results.removeFromTop(20));
    resultsSummary.setBounds(results.removeFromTop(20));
    failedFilesLabel.setBounds(results.removeFromTop(20));
    results.removeFromTop(2);
    resultsBox.setBounds(results);

    auto blacklist = blacklistPanelBounds.reduced(12, 10);
    auto blacklistHeader = blacklist.removeFromTop(20);
    blacklistCountLabel.setBounds(blacklistHeader.removeFromRight(82));
    blacklistHeading.setBounds(blacklistHeader.withTrimmedRight(8));
    blacklistHelpLabel.setBounds(blacklist.removeFromTop(32));
    auto removeArea = blacklist.removeFromBottom(30);
    blacklist.removeFromBottom(8);
    blacklistBox.setBounds(blacklist);
    removeBlacklistButton.setBounds(removeArea.removeFromLeft(142));
}

void ScanDialog::rebuildRows()
{
    const auto previousScrollPosition = sourcesViewport.getViewPositionY();
    rows.clear();

    const auto sources = mgr.getSources();
    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        auto* row = new SourceRow(*this, i);
        row->refresh(sources[i]);
        sourceListContent.addAndMakeVisible(row);
        rows.add(row);
    }

    emptySourcesLabel.setVisible(sources.empty());
    sourcesViewport.setVisible(!sources.empty());
    layoutSourceRows();
    sourcesViewport.setViewPosition(0, juce::jmax(0, previousScrollPosition));
    updateActionStates();
    repaint();
}

void ScanDialog::requestRebuildRows()
{
    if (rebuildPending)
        return;

    rebuildPending = true;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ScanDialog>(this)]
    {
        if (safe == nullptr)
            return;
        safe->rebuildPending = false;
        safe->rebuildRows();
    });
}

void ScanDialog::layoutSourceRows()
{
    const auto width = juce::jmax(1, sourcesViewport.getMaximumVisibleWidth());
    const auto requiredHeight = rows.size() * sourceRowHeight;
    const auto height = juce::jmax(sourcesViewport.getMaximumVisibleHeight(), requiredHeight);
    sourceListContent.setSize(width, height);

    for (int i = 0; i < rows.size(); ++i)
        rows.getUnchecked(i)->setBounds(0, i * sourceRowHeight, width, sourceRowHeight);
    sourceListContent.repaint();
}

void ScanDialog::setSourceEnabled(std::size_t index, const juce::String& path, bool enabled)
{
    if (scanning)
        return;

    auto sources = mgr.getSources();
    auto source = std::find_if(sources.begin(), sources.end(), [&path](const auto& candidate)
    {
        return candidate.path == path;
    });
    if (source == sources.end())
        return;
    if (index < sources.size() && sources[index].path == path)
        source = sources.begin() + static_cast<std::ptrdiff_t>(index);

    source->enabled = enabled;
    mgr.setSources(sources);
    updateActionStates();
}

void ScanDialog::setSourceRecursive(std::size_t index, const juce::String& path, bool recursive)
{
    if (scanning)
        return;

    auto sources = mgr.getSources();
    auto source = std::find_if(sources.begin(), sources.end(), [&path](const auto& candidate)
    {
        return candidate.path == path;
    });
    if (source == sources.end())
        return;
    if (index < sources.size() && sources[index].path == path)
        source = sources.begin() + static_cast<std::ptrdiff_t>(index);

    source->recursive = recursive;
    mgr.setSources(sources);
}

void ScanDialog::removeSource(std::size_t index, const juce::String& path)
{
    if (scanning)
        return;

    auto sources = mgr.getSources();
    auto source = std::find_if(sources.begin(), sources.end(), [&path](const auto& candidate)
    {
        return candidate.path == path;
    });
    if (source == sources.end())
        return;
    if (index < sources.size() && sources[index].path == path)
        source = sources.begin() + static_cast<std::ptrdiff_t>(index);

    sources.erase(source);
    mgr.setSources(sources);
    updateActionStates();
    requestRebuildRows();
}

void ScanDialog::addFolder()
{
    if (scanning)
        return;

    auto chooser = std::make_shared<juce::FileChooser>(
        "Add plugin folder", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [safe = juce::Component::SafePointer<ScanDialog>(this), chooser](const juce::FileChooser&)
    {
        if (safe == nullptr)
            return;

        const auto folder = chooser->getResult();
        if (!folder.isDirectory())
            return;

        safe->mgr.addSource({ folder.getFullPathName(), "My folder", true, true });
        safe->rebuildRows();
        safe->sourcesViewport.setViewPosition(0, safe->sourceListContent.getHeight());
        safe->statusLabel.setText("Folder added. Ready to scan.", juce::dontSendNotification);
        safe->currentFileLabel.setText({}, juce::dontSendNotification);
    });
}

void ScanDialog::beginScan(bool fullRescan)
{
    if (scanning || mgr.isScanning())
        return;

    const auto sources = mgr.getSources();
    const auto hasEnabledSource = std::any_of(sources.begin(), sources.end(), [](const auto& source)
    {
        return source.enabled && source.path.isNotEmpty();
    });
    if (!hasEnabledSource)
    {
        updateActionStates();
        return;
    }

    fullScanRequested = fullRescan;
    cancelRequested = false;
    progressValue = 0.0;
    mgr.startScan(fullRescan);
    scanning = mgr.isScanning();
    refreshResults();
    refreshScanProgress();
    updateActionStates();
    resized();
    startTimer(80);
}

void ScanDialog::refreshScanProgress()
{
    progressValue = juce::jlimit(0.0, 1.0, static_cast<double>(mgr.getProgress()));
    const auto found = scanning ? mgr.getNumFound() : mgr.getList().getNumTypes();
    scanFoundLabel.setText(juce::String(found) + (found == 1 ? " plugin indexed" : " plugins indexed"),
                           juce::dontSendNotification);

    if (scanning)
    {
        statusLabel.setText(cancelRequested ? "Cancelling after the current file..."
                                             : (fullScanRequested ? "Full rescan in progress"
                                                                  : "Scanning selected folders"),
                            juce::dontSendNotification);
        const auto currentFile = mgr.getCurrentFile();
        currentFileLabel.setText(currentFile.isNotEmpty() ? currentFile : "Preparing plugin scanner...",
                                 juce::dontSendNotification);
        currentFileLabel.setTooltip(currentFile);
    }
    else
    {
        if (statusLabel.getText().isEmpty())
            statusLabel.setText("Ready to scan", juce::dontSendNotification);
    }

    progressBar.repaint();
}

void ScanDialog::handleScanFinished()
{
    stopTimer();
    const auto wasCancelled = cancelRequested;
    const auto wasFullScan = fullScanRequested;
    scanning = false;
    cancelRequested = false;
    fullScanRequested = false;
    progressValue = juce::jlimit(0.0, 1.0, static_cast<double>(mgr.getProgress()));

    refreshResults();
    refreshBlacklist();
    scanFoundLabel.setText(juce::String(mgr.getList().getNumTypes()) + " plugins indexed",
                           juce::dontSendNotification);
    statusLabel.setText(wasCancelled ? "Scan cancelled"
                                     : (wasFullScan ? "Full rescan complete" : "Scan complete"),
                        juce::dontSendNotification);
    currentFileLabel.setText({}, juce::dontSendNotification);
    progressBar.repaint();
    updateActionStates();
    resized();
}

void ScanDialog::updateActionStates()
{
    const auto sources = mgr.getSources();
    const auto enabledCount = static_cast<int>(std::count_if(sources.begin(), sources.end(), [](const auto& source)
    {
        return source.enabled;
    }));
    const auto hasEnabledSource = std::any_of(sources.begin(), sources.end(), [](const auto& source)
    {
        return source.enabled && source.path.isNotEmpty();
    });

    sourceCountLabel.setText(juce::String(static_cast<int>(sources.size()))
                                 + (sources.size() == 1 ? " source / " : " sources / ")
                                 + juce::String(enabledCount) + " enabled",
                             juce::dontSendNotification);
    addFolderButton.setEnabled(!scanning);
    scanButton.setEnabled(!scanning && hasEnabledSource);
    rescanButton.setEnabled(!scanning && hasEnabledSource);
    scanButton.setVisible(!scanning);
    rescanButton.setVisible(!scanning);
    cancelButton.setVisible(scanning);
    cancelButton.setEnabled(scanning && !cancelRequested);
    cancelButton.setButtonText(cancelRequested ? "Cancelling..." : "Cancel");
    closeButton.setButtonText(scanning ? "Close" : "Done");
    closeButton.setTooltip(scanning ? "Close this window; scanning continues in the background"
                                    : "Close Scan Manager");
    scanButton.setTooltip(hasEnabledSource ? "Scan new or changed plugins in enabled folders"
                                           : "Enable at least one folder before scanning");

    for (auto* row : rows)
    {
        row->enabledButton.setEnabled(!scanning);
        row->recursiveButton.setEnabled(!scanning);
        row->deleteButton.setEnabled(!scanning);
    }
    removeBlacklistButton.setEnabled(!scanning && !mgr.getList().getBlacklistedFiles().isEmpty());
}

void ScanDialog::refreshResults()
{
    const auto pluginCount = mgr.getList().getNumTypes();
    const auto failedFiles = mgr.getFailedFiles();
    resultsSummary.setText(juce::String(pluginCount) + (pluginCount == 1 ? " plugin indexed" : " plugins indexed"),
                           juce::dontSendNotification);
    failedFilesLabel.setText("FAILED FILES (" + juce::String(failedFiles.size()) + ")",
                             juce::dontSendNotification);
    failedFilesLabel.setColour(juce::Label::textColourId, failedFiles.isEmpty() ? theme::faint : theme::danger);
    resultsBox.setText(failedFiles.joinIntoString("\n"), false);
}

void ScanDialog::refreshBlacklist()
{
    const auto blacklisted = mgr.getList().getBlacklistedFiles();
    blacklistCountLabel.setText(juce::String(blacklisted.size())
                                    + (blacklisted.size() == 1 ? " entry" : " entries"),
                                juce::dontSendNotification);
    blacklistBox.setText(blacklisted.joinIntoString("\n"), false);
}

void ScanDialog::removeSelectedBlacklistEntries()
{
    if (scanning)
        return;

    const auto selectedText = blacklistBox.getHighlightedText();
    if (selectedText.trim().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Blacklist",
                                               "Select one or more complete entries in the blacklist first.");
        return;
    }

    const auto selectedLines = juce::StringArray::fromLines(selectedText);
    const auto blacklisted = mgr.getList().getBlacklistedFiles();
    juce::StringArray entriesToRemove;
    for (auto line : selectedLines)
    {
        line = line.trim();
        if (line.isNotEmpty() && blacklisted.contains(line) && !entriesToRemove.contains(line))
            entriesToRemove.add(line);
    }

    if (entriesToRemove.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Blacklist",
                                               "Select complete blacklist entries, including the full path on each line.");
        return;
    }

    for (const auto& entry : entriesToRemove)
        mgr.getList().removeFromBlacklist(entry);
    mgr.saveSettings();
    refreshBlacklist();
    updateActionStates();
    statusLabel.setText(juce::String(entriesToRemove.size())
                            + (entriesToRemove.size() == 1 ? " blacklist entry removed" : " blacklist entries removed")
                            + ". Scan again to retry.",
                        juce::dontSendNotification);
}

void ScanDialog::timerCallback()
{
    if (mgr.isScanning())
    {
        if (!scanning)
        {
            scanning = true;
            updateActionStates();
            resized();
        }
        refreshScanProgress();
        return;
    }

    if (scanning)
        handleScanFinished();
    else
        stopTimer();
}

void ScanDialog::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &mgr && !mgr.isScanning())
        handleScanFinished();
}
