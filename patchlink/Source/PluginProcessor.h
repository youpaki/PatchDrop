#pragma once
#include <JuceHeader.h>
#include "Chain/PatchFormat.h"
#include "Scan/PluginScanManager.h"

// PatchLink engine: hosts third-party plugins in an AudioProcessorGraph.
// - Formats via juce::addDefaultFormatsToManager (JUCE 8, includes VST3 + AU).
// - Scanning via PluginScanManager (background, pedal-file crash durability).
// - Instantiation always async (createPluginInstanceAsync, AUv3-safe) + enableAllBuses.
// - Patches: .patchlink v1 (migrated) / chain v2, IDs-not-paths, missing-plugin report.
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

    // Engine access
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownList() { return knownList; }
    juce::AudioProcessorGraph& getGraph() { return graph; }
    PluginScanManager& getScanManager() { return scanManager; }
    std::vector<LoadedNode> getLoadedNodes();

    // Async hosting (message-thread callbacks). err empty = ok.
    using DoneFn = std::function<void(const juce::String& err)>;
    void addPluginAsync(const juce::PluginDescription& desc, int x, int y, DoneFn done);
    void removeNode(juce::AudioProcessorGraph::NodeID id);
    void setNodeBypass(juce::AudioProcessorGraph::NodeID id, bool b);
    bool getNodeBypass(juce::AudioProcessorGraph::NodeID id);
    void setNodePosition(juce::AudioProcessorGraph::NodeID id, int x, int y);
    void clearChain();

    // Manual connections (Full mode)
    bool connectNodes(juce::AudioProcessorGraph::NodeID src, int srcCh,
                      juce::AudioProcessorGraph::NodeID dst, int dstCh);
    void disconnectAll(juce::AudioProcessorGraph::NodeID id);
    std::vector<patchlink::Wire> getWires();

    // Sharing: files are sync (small JSON), patch recall is async (plugin load).
    bool exportPatch(const juce::File& file, juce::String& err);
    void importPatchAsync(const juce::File& file,
                          std::function<void(juce::StringArray missing, juce::String err)> done);
    juce::var graphToVar();
    void loadFromVarAsync(const juce::var& root,
                          std::function<void(juce::StringArray missing, juce::String err)> done);
    static bool loadFromVarSyncLayout(const juce::var& root, juce::String& err); // validation only

    int preferredMode = 0; // 0 = ask, 1 = simple, 2 = full

private:
    juce::AudioProcessorGraph graph;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
    PluginScanManager scanManager;

    juce::AudioProcessorGraph::NodeID inputId, outputId;
    uint32_t nextUid = 100;

    void rebuildSerialIfNeeded();
    bool autoConnectSerial = true;
    void setAutoConnect(bool b) { autoConnectSerial = b; }
    friend class PatchLinkEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchLinkProcessor)
};
