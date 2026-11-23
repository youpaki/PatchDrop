#pragma once

#include <JuceHeader.h>
#include "Controls.h"

class ShareDialog final : public ModalOverlay
{
public:
    enum class Mode { publish, openCode };

    ShareDialog(Mode, const juce::var& patchRoot);

    std::function<void(const juce::String&, const juce::String&, const juce::String&, bool)> onPublish;
    std::function<void(const juce::String&)> onOpenCode;
    std::function<void(const juce::var& patch, const juce::String& title)> onLoadFetched;
    std::function<void(const juce::var& patch, const juce::String& title)> onStageFetched;
    std::function<void(const juce::var& patch, const juce::String& title, bool startDrag)> onDragFetchedToDaw;

    void setBusy(bool, const juce::String& status = {});
    void publishSucceeded(const juce::String& code);
    void codeFetched(const juce::String& code, const juce::String& title, const juce::var& patch);
    void operationFailed(const juce::String& message);
    juce::Rectangle<int> getCardBounds() const override;

private:
    Mode mode;
    bool busy = false;
    bool succeeded = false;
    bool fetchedReady = false;
    juce::String sharedCode;
    juce::String fetchedCode;
    juce::String fetchedTitle;
    juce::var fetchedPatch;

    juce::Label title, subtitle, titleCaption, authorCaption, descriptionCaption;
    juce::Label manifestCaption, manifestSummary, status, successCaption, codeLabel;
    juce::TextEditor titleEditor, authorEditor, descriptionEditor, codeEditor, manifest;
    juce::ToggleButton publicToggle { "List in the public PatchDrop explorer" };
    PlButton closeButton { "", Icon::close, PlButton::Style::ghost };
    PlButton stageButton { "Stage only", Icon::none, PlButton::Style::ghost };
    PlButton dragDawButton { "Drag to DAW", Icon::exportFile, PlButton::Style::secondary };
    PlButton actionButton { "Publish chain", Icon::share, PlButton::Style::primary };
    PlButton copyButton { "Copy code", Icon::copy, PlButton::Style::secondary };

    void submit();
    void updateVisibility();
    void layoutCard(juce::Rectangle<int>) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShareDialog)
};
