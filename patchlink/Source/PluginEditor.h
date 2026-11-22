#pragma once
#include <JuceHeader.h>

class PatchLinkProcessor;

// Editeur avec écran d'entrée : Mode Simple vs Mode Patcher Full.
// Commutable à tout moment via la barre du haut. Choix mémorisé.
class PatchLinkEditor : public juce::AudioProcessorEditor,
                        public juce::Timer
{
public:
    explicit PatchLinkEditor(PatchLinkProcessor& p);
    ~PatchLinkEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    PatchLinkProcessor& proc;

    enum class Mode { Entry = 0, Simple = 1, Full = 2 };
    Mode mode = Mode::Entry;
    bool askAtStartup = true;

    // Barre du haut (visible en Simple/Full)
    juce::TextButton btnSimple { "Simple" }, btnFull { "Patcher" };
    juce::TextButton btnAdd { "+ Plugin" }, btnImport { "Import" }, btnExport { "Export" }, btnScan { "Scan" };
    juce::TextButton btnShare { "Share: get code" }, btnOpenCode { "Open via code" };
    juce::String apiBase = "https://gg.trenas.fr/patchdrop";
    juce::Label statusLabel;

    // Ecran d'entrée
    juce::TextButton entrySimple { "Mode Simple\n(série, rapide)" }, entryFull { "Mode Patcher\n(nodal complet)" };
    juce::ToggleButton entryAsk { "Toujours demander au démarrage" };
    juce::Label entryTitle, entrySub;

    // Vue simple : liste verticale
    struct SimpleList;
    std::unique_ptr<juce::Viewport> simpleView;
    std::unique_ptr<SimpleList> simpleList;

    // Vue full : canvas nodal minimal (drag + select + connect)
    struct FullCanvas;
    std::unique_ptr<FullCanvas> fullCanvas;

    void setMode(Mode m);
    void refreshAll();
    void pickAndAddPlugin(int x, int y);
    void doImport(), doExport(), doScan();
    void doShareViaCode(), doOpenViaCode();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchLinkEditor)
};
