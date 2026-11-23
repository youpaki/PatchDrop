#include "ShareDialog.h"
#include "../Core/ChainFormat.h"

ShareDialog::ShareDialog(Mode dialogMode, const juce::var& patchRoot) : mode(dialogMode)
{
    const auto publishing = mode == Mode::publish;
    title.setText(publishing ? "Share your chain" : "Open a shared chain", juce::dontSendNotification);
    title.setFont(theme::font(22.0f, true));
    subtitle.setText(publishing ? "Publish a six-character code with everything needed to recall your sound."
                                : "Paste a PatchDrop code to load the complete chain and its settings.",
                     juce::dontSendNotification);
    subtitle.setFont(theme::font(12.5f));
    subtitle.setColour(juce::Label::textColourId, theme::muted);
    addAndMakeVisible(title);
    addAndMakeVisible(subtitle);

    closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
    closeButton.setTooltip("Close (Esc)");
    addAndMakeVisible(closeButton);

    auto setupCaption = [](juce::Label& label, const juce::String& text, juce::Component* parent) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(theme::font(11.0f, true));
        label.setColour(juce::Label::textColourId, theme::muted);
        parent->addAndMakeVisible(label);
    };
    setupCaption(titleCaption, "CHAIN TITLE", this);
    setupCaption(authorCaption, "YOUR NAME", this);
    setupCaption(descriptionCaption, "DESCRIPTION", this);
    setupCaption(manifestCaption, "DEPENDENCY MANIFEST", this);

    auto setupEditor = [](juce::TextEditor& editor) {
        editor.setFont(theme::font(13.0f));
        editor.setIndents(10, 0);
    };
    setupEditor(titleEditor);
    setupEditor(authorEditor);
    setupEditor(descriptionEditor);
    setupEditor(codeEditor);
    titleEditor.setTextToShowWhenEmpty("My vocal chain", theme::faint);
    authorEditor.setTextToShowWhenEmpty("Optional", theme::faint);
    descriptionEditor.setTextToShowWhenEmpty("What is this chain good for?", theme::faint);
    descriptionEditor.setMultiLine(true);
    descriptionEditor.setReturnKeyStartsNewLine(true);
    descriptionEditor.setIndents(10, 8);
    if (auto* root = patchRoot.getDynamicObject())
        titleEditor.setText(root->getProperty("name").toString(), false);
    addAndMakeVisible(titleEditor);
    addAndMakeVisible(authorEditor);
    addAndMakeVisible(descriptionEditor);

    publicToggle.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(publicToggle);

    auto lines = chainformat::manifestLines(patchRoot);
    int portable = 0;
    for (const auto& line : lines) if (line.startsWith("[ok]")) ++portable;
    manifestSummary.setText(juce::String(lines.size()) + (lines.size() == 1 ? " dependency" : " dependencies")
                            + "  /  " + juce::String(portable) + " portable", juce::dontSendNotification);
    manifestSummary.setFont(theme::mono(10.0f));
    manifestSummary.setColour(juce::Label::textColourId, theme::faint);
    addAndMakeVisible(manifestSummary);
    manifest.setMultiLine(true);
    manifest.setReadOnly(true);
    manifest.setFont(theme::mono(11.0f));
    manifest.setText(lines.isEmpty() ? "No dependencies. This empty chain can be opened anywhere."
                                     : lines.joinIntoString("\n"));
    addAndMakeVisible(manifest);

    codeEditor.setTextToShowWhenEmpty("K7Q2XA", theme::faint);
    codeEditor.setFont(theme::mono(24.0f));
    codeEditor.setJustification(juce::Justification::centred);
    codeEditor.setInputRestrictions(6, "ABCDEFGHJKMNPQRSTUVWXYZ23456789abcdefghijklmnopqrstuvwxyz");
    codeEditor.onTextChange = [this] {
        const auto clean = codeEditor.getText().toUpperCase().retainCharacters("ABCDEFGHJKMNPQRSTUVWXYZ23456789").substring(0, 6);
        if (clean != codeEditor.getText())
        {
            codeEditor.setText(clean, false);
            codeEditor.moveCaretToEnd();
        }
        actionButton.setEnabled(clean.length() == 6 && !busy);
    };
    codeEditor.onReturnKey = [this] { submit(); };

    successCaption.setText("Your chain is ready to travel", juce::dontSendNotification);
    successCaption.setFont(theme::font(14.0f, true));
    successCaption.setColour(juce::Label::textColourId, theme::accent);
    successCaption.setJustificationType(juce::Justification::centred);
    codeLabel.setFont(theme::mono(40.0f));
    codeLabel.setJustificationType(juce::Justification::centred);
    codeLabel.setColour(juce::Label::textColourId, theme::text);
    copyButton.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(sharedCode);
        status.setText("Copied. Send the code to anyone using PatchLink.", juce::dontSendNotification);
        copyButton.setButtonText("Copied");
    };

    status.setFont(theme::font(11.5f));
    status.setColour(juce::Label::textColourId, theme::muted);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
    stageButton.setTooltip("Store this chain without loading plugins so you can export or drag it");
    stageButton.onClick = [this] { if (onStageFetched) onStageFetched(fetchedPatch, fetchedTitle); };
    addChildComponent(stageButton);
    dragDawButton.setTooltip("Drag onto your DAW (or click for format options) without loading plugins inside PatchLink");
    dragDawButton.onDragStart = [this] { if (onDragFetchedToDaw) onDragFetchedToDaw(fetchedPatch, fetchedTitle, true); };
    dragDawButton.onClick = [this] { if (onDragFetchedToDaw) onDragFetchedToDaw(fetchedPatch, fetchedTitle, false); };
    addChildComponent(dragDawButton);
    actionButton.onClick = [this] { submit(); };
    addAndMakeVisible(actionButton);

    if (publishing)
    {
        addChildComponent(codeEditor);
    }
    else
    {
        actionButton.setButtonText("Open chain");
        actionButton.setEnabled(false);
        addAndMakeVisible(codeEditor);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ShareDialog>(this)] {
            if (safe != nullptr) safe->codeEditor.grabKeyboardFocus();
        });
    }
    addChildComponent(successCaption);
    addChildComponent(codeLabel);
    addChildComponent(copyButton);

    if (!publishing) setupCaption(manifestCaption, "PATCHDROP CODE", this);
    updateVisibility();
}

juce::Rectangle<int> ShareDialog::getCardBounds() const
{
    const auto w = juce::jmin(720, getWidth() - 48);
    const auto desiredHeight = (mode == Mode::publish) ? 620 : (fetchedReady ? 540 : 360);
    const auto h = juce::jmin(desiredHeight, getHeight() - 48);
    return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
}

void ShareDialog::submit()
{
    if (succeeded)
    {
        if (onDismiss) onDismiss();
        return;
    }
    if (fetchedReady)
    {
        if (onLoadFetched) onLoadFetched(fetchedPatch, fetchedTitle);
        return;
    }
    if (busy) return;
    if (mode == Mode::publish)
    {
        if (titleEditor.getText().trim().isEmpty())
        {
            operationFailed("Give your chain a title before publishing.");
            titleEditor.grabKeyboardFocus();
            return;
        }
        if (onPublish) onPublish(titleEditor.getText().trim(), authorEditor.getText().trim(),
                                 descriptionEditor.getText().trim(), publicToggle.getToggleState());
    }
    else
    {
        const auto code = codeEditor.getText().toUpperCase().trim();
        if (code.length() != 6)
        {
            operationFailed("Enter the complete six-character code.");
            return;
        }
        if (onOpenCode) onOpenCode(code);
    }
}

void ShareDialog::setBusy(bool shouldBeBusy, const juce::String& message)
{
    busy = shouldBeBusy;
    titleEditor.setEnabled(!busy);
    authorEditor.setEnabled(!busy);
    descriptionEditor.setEnabled(!busy);
    codeEditor.setEnabled(!busy);
    publicToggle.setEnabled(!busy);
    stageButton.setEnabled(!busy);
    dragDawButton.setEnabled(!busy);
    actionButton.setEnabled(!busy && (mode == Mode::publish || fetchedReady || codeEditor.getText().length() == 6));
    actionButton.setButtonText(busy ? (mode == Mode::publish ? "Publishing..." : "Fetching...")
                                    : (fetchedReady ? "Load effects"
                                                    : (mode == Mode::publish ? "Publish chain" : "Open chain")));
    status.setColour(juce::Label::textColourId, theme::muted);
    status.setText(message.isNotEmpty() ? message : (busy ? "Contacting PatchDrop..." : ""), juce::dontSendNotification);
}

void ShareDialog::publishSucceeded(const juce::String& code)
{
    busy = false;
    succeeded = true;
    sharedCode = code.toUpperCase().trim();
    codeLabel.setText(sharedCode, juce::dontSendNotification);
    status.setText("Anyone with this code can open the chain in PatchLink.", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, theme::muted);
    actionButton.setButtonText("Done");
    actionButton.setEnabled(true);
    updateVisibility();
    resized();
}

void ShareDialog::codeFetched(const juce::String& code, const juce::String& titleText, const juce::var& patch)
{
    busy = false;
    fetchedReady = true;
    fetchedCode = code.toUpperCase().trim();
    fetchedTitle = titleText.isNotEmpty() ? titleText : fetchedCode;
    fetchedPatch = patch;

    title.setText("Code " + fetchedCode + " ready", juce::dontSendNotification);
    subtitle.setText("Load effects inside PatchLink, or use Zero-Load mode to drag directly into your DAW.",
                     juce::dontSendNotification);

    auto lines = chainformat::manifestLines(patch);
    int portable = 0;
    for (const auto& line : lines) if (line.startsWith("[ok]")) ++portable;
    manifestCaption.setText("CHAIN PLUGINS — " + fetchedTitle.toUpperCase(), juce::dontSendNotification);
    manifestSummary.setText(juce::String(lines.size()) + (lines.size() == 1 ? " plugin" : " plugins")
                            + "  /  " + juce::String(portable) + " portable", juce::dontSendNotification);
    manifest.setText(lines.isEmpty() ? "No plugins in this chain." : lines.joinIntoString("\n"));

    actionButton.setButtonText("Load effects");
    actionButton.setEnabled(true);
    status.setColour(juce::Label::textColourId, theme::muted);
    status.setText("Zero-Load: drag 'Drag to DAW' onto your DAW window without loading any plugins.",
                   juce::dontSendNotification);

    updateVisibility();
    resized();
}

void ShareDialog::operationFailed(const juce::String& message)
{
    setBusy(false);
    status.setText(message, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, theme::danger);
}

void ShareDialog::updateVisibility()
{
    const auto publishing = mode == Mode::publish;
    for (auto* component : { (juce::Component*)&titleCaption, (juce::Component*)&authorCaption,
                             (juce::Component*)&descriptionCaption,
                             (juce::Component*)&titleEditor, (juce::Component*)&authorEditor,
                             (juce::Component*)&descriptionEditor, (juce::Component*)&publicToggle })
        component->setVisible(publishing && !succeeded);

    const bool showManifest = (publishing && !succeeded) || fetchedReady;
    manifestCaption.setVisible(showManifest || (!publishing && !succeeded));
    manifestSummary.setVisible(showManifest);
    manifest.setVisible(showManifest);
    codeEditor.setVisible(!publishing && !fetchedReady && !succeeded);
    stageButton.setVisible(!publishing && fetchedReady);
    dragDawButton.setVisible(!publishing && fetchedReady);
    successCaption.setVisible(succeeded);
    codeLabel.setVisible(succeeded);
    copyButton.setVisible(succeeded);
    if (succeeded)
        actionButton.setButtonText("Done");
}

void ShareDialog::layoutCard(juce::Rectangle<int> card)
{
    auto r = card.reduced(26, 22);
    closeButton.setBounds(r.getRight() - 34, r.getY(), 34, 34);
    title.setBounds(r.removeFromTop(36).withTrimmedRight(44));
    subtitle.setBounds(r.removeFromTop(26));
    r.removeFromTop(18);

    if (fetchedReady)
    {
        auto footer = r.removeFromBottom(42);
        actionButton.setBounds(footer.removeFromRight(142).reduced(0, 2));
        footer.removeFromRight(8);
        dragDawButton.setBounds(footer.removeFromRight(144).reduced(0, 2));
        footer.removeFromRight(8);
        stageButton.setBounds(footer.removeFromRight(106).reduced(0, 2));
        r.removeFromBottom(6);
        status.setBounds(r.removeFromBottom(22));
        r.removeFromBottom(8);

        auto manifestHeader = r.removeFromTop(20);
        manifestSummary.setBounds(manifestHeader.removeFromRight(190));
        manifestCaption.setBounds(manifestHeader);
        r.removeFromTop(6);
        manifest.setBounds(r);
        return;
    }

    auto footer = r.removeFromBottom(42);
    actionButton.setBounds(footer.removeFromRight(156).reduced(0, 2));
    status.setBounds(footer.reduced(0, 2).withTrimmedRight(12));

    if (succeeded)
    {
        auto centre = r.removeFromTop(juce::jmin(230, r.getHeight()));
        successCaption.setBounds(centre.removeFromTop(36));
        centre.removeFromTop(12);
        codeLabel.setBounds(centre.removeFromTop(72));
        centre.removeFromTop(12);
        copyButton.setBounds(centre.removeFromTop(38).withSizeKeepingCentre(142, 38));
        return;
    }

    if (mode == Mode::openCode)
    {
        manifestCaption.setBounds(r.removeFromTop(19));
        codeEditor.setBounds(r.removeFromTop(64));
        r.removeFromTop(12);
        return;
    }

    titleCaption.setBounds(r.removeFromTop(19));
    titleEditor.setBounds(r.removeFromTop(38));
    r.removeFromTop(10);
    authorCaption.setBounds(r.removeFromTop(19));
    authorEditor.setBounds(r.removeFromTop(36));
    r.removeFromTop(10);
    descriptionCaption.setBounds(r.removeFromTop(19));
    descriptionEditor.setBounds(r.removeFromTop(58));
    r.removeFromTop(8);
    publicToggle.setBounds(r.removeFromTop(26));
    r.removeFromTop(8);
    auto manifestHeader = r.removeFromTop(20);
    manifestSummary.setBounds(manifestHeader.removeFromRight(190));
    manifestCaption.setBounds(manifestHeader);
    manifest.setBounds(r);
}
