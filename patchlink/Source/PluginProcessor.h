#pragma once
#include <JuceHeader.h>
#include "Chain/PatchFormat.h"

// Moteur audio : héberge des plugins tiers dans un AudioProcessorGraph.
// Partageable via fichier .patchlink (JSON + états base64).
class PatchLinkProcessor : public juce::AudioProcessor
{
public:
    PatchLinkProcessor();
    ~PatchLinkProcessor() override;

    struct LoadedNode
    {
        juce::AudioProcessorGraph::NodeID id;
        juce::PluginDescription desc;
        int x = 0, y = 0;
        bool bypass = false;
    };

    // AudioProcessor
    const juce::String getName() const override { return "PatchLink"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void prepareToPlay(double sr, int bs) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // Hosting
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownList() { return knownList; }
    juce::AudioProcessorGraph& getGraph() { return graph; }
    std::vector<LoadedNode> getLoadedNodes();
    void scanDefaultPathsAsync();
    juce::String addPluginByDescription(const juce::PluginDescription& desc, int x, int y);
    void removeNode(juce::AudioProcessorGraph::NodeID id);
    void setNodeBypass(juce::AudioProcessorGraph::NodeID id, bool b);
    bool getNodeBypass(juce::AudioProcessorGraph::NodeID id);
    void setNodePosition(juce::AudioProcessorGraph::NodeID id, int x, int y);
    void clearChain();

    // Connexions manuelles (mode Full). V1 : série auto + connexions libres.
    bool connectNodes(juce::AudioProcessorGraph::NodeID src, int srcCh,
                      juce::AudioProcessorGraph::NodeID dst, int dstCh);
    void disconnectAll(juce::AudioProcessorGraph::NodeID id);
    std::vector<patchlink::Wire> getWires();

    // Partage .patchlink
    bool exportPatch(const juce::File& file, juce::String& err);
    // Retourne la liste des plugins manquants (noms lisibles)
    bool importPatch(const juce::File& file, juce::StringArray& missing, juce::String& err);
    juce::var graphToVar();
    bool loadFromVar(const juce::var& root, juce::StringArray& missing);

    // Préférence UI : 0 = demander, 1 = simple, 2 = full
    int preferredMode = 0;

private:
    juce::AudioProcessorGraph graph;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;

    juce::AudioProcessorGraph::NodeID inputId, outputId;
    uint32_t nextUid = 100;
    juce::CriticalSection lock;

    void rebuildSerialIfNeeded();
    bool autoConnectSerial = true; // true en mode Simple, false en mode Full
    void setAutoConnect(bool b) { autoConnectSerial = b; }
    friend class PatchLinkEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchLinkProcessor)
};
