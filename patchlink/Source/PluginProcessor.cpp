#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/ChainFormat.h"

PatchLinkProcessor::PatchLinkProcessor()
    : scanManager(formatManager, knownList)
{
    formatManager.addDefaultFormats();
    scanManager.loadSettings();
    if (scanManager.getSources().empty())
        scanManager.buildDefaultSources(); // standards + detected DAW folders, no silent scan

    inputId  = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), juce::AudioProcessorGraph::NodeID(10))->nodeID;
    outputId = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), juce::AudioProcessorGraph::NodeID(20))->nodeID;

    graph.addConnection({ { inputId, 0 }, { outputId, 0 } });
    graph.addConnection({ { inputId, 1 }, { outputId, 1 } });
}

PatchLinkProcessor::~PatchLinkProcessor() = default;

juce::AudioProcessorEditor* PatchLinkProcessor::createEditor()
{
    return new PatchLinkEditor(*this);
}

void PatchLinkProcessor::prepareToPlay(double sr, int bs)
{
    graph.setPlayConfigDetails(getMainBusNumInputChannels(), getMainBusNumOutputChannels(), sr, bs);
    graph.prepareToPlay(sr, bs);
}

void PatchLinkProcessor::releaseResources() { graph.releaseResources(); }

void PatchLinkProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals d;
    graph.processBlock(b, m);
}

bool PatchLinkProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// ---------- hosting ----------
std::vector<PatchLinkProcessor::LoadedNode> PatchLinkProcessor::getLoadedNodes()
{
    std::vector<LoadedNode> out;
    for (auto* n : graph.getNodes())
    {
        if (n->nodeID == inputId || n->nodeID == outputId) continue;
        LoadedNode ln;
        ln.id = n->nodeID;
        if (auto* inst = dynamic_cast<juce::AudioPluginInstance*>(n->getProcessor()))
            ln.desc = inst->getPluginDescription();
        ln.x = n->properties.getWithDefault("x", 60);
        ln.y = n->properties.getWithDefault("y", 60);
        ln.bypass = n->isBypassed();
        out.push_back(ln);
    }
    return out;
}

void PatchLinkProcessor::addPluginAsync(const juce::PluginDescription& desc, int x, int y, DoneFn done)
{
    // AUv3-safe: never sync-instantiate on the message thread.
    formatManager.createPluginInstanceAsync(
        desc, getSampleRate() > 0 ? getSampleRate() : 44100.0,
        getBlockSize() > 0 ? getBlockSize() : 512,
        [this, x, y, done](std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
            if (inst == nullptr) { if (done) done(err.isNotEmpty() ? err : "Could not load plugin"); return; }
            inst->enableAllBuses();
            graph.suspendProcessing(true);
            auto node = graph.addNode(std::move(inst), juce::AudioProcessorGraph::NodeID(nextUid++));
            if (node != nullptr)
            {
                node->properties.set("x", x);
                node->properties.set("y", y);
                if (autoConnectSerial) rebuildSerialIfNeeded();
            }
            graph.suspendProcessing(false);
            if (done) done(node == nullptr ? juce::String("addNode failed") : juce::String{});
        });
}

void PatchLinkProcessor::removeNode(juce::AudioProcessorGraph::NodeID id)
{
    graph.suspendProcessing(true);
    graph.removeNode(id);
    if (autoConnectSerial) rebuildSerialIfNeeded();
    graph.suspendProcessing(false);
}

void PatchLinkProcessor::setNodeBypass(juce::AudioProcessorGraph::NodeID id, bool b)
{
    if (auto* n = graph.getNodeForId(id)) n->setBypassed(b);
}

bool PatchLinkProcessor::getNodeBypass(juce::AudioProcessorGraph::NodeID id)
{
    if (auto* n = graph.getNodeForId(id)) return n->isBypassed();
    return false;
}

void PatchLinkProcessor::setNodePosition(juce::AudioProcessorGraph::NodeID id, int x, int y)
{
    if (auto* n = graph.getNodeForId(id)) { n->properties.set("x", x); n->properties.set("y", y); }
}

void PatchLinkProcessor::clearChain()
{
    graph.suspendProcessing(true);
    for (auto* n : graph.getNodes())
        if (n->nodeID != inputId && n->nodeID != outputId)
            graph.removeNode(n->nodeID);
    graph.suspendProcessing(false);
}

bool PatchLinkProcessor::connectNodes(juce::AudioProcessorGraph::NodeID src, int srcCh,
                                       juce::AudioProcessorGraph::NodeID dst, int dstCh)
{
    return graph.addConnection({ { src, srcCh }, { dst, dstCh } });
}

void PatchLinkProcessor::disconnectAll(juce::AudioProcessorGraph::NodeID id)
{
    graph.suspendProcessing(true);
    juce::Array<juce::AudioProcessorGraph::Connection> toRemove;
    for (auto& c : graph.getConnections())
        if (c.source.nodeID == id || c.destination.nodeID == id)
            toRemove.add(c);
    for (auto& c : toRemove) graph.removeConnection(c);
    graph.suspendProcessing(false);
}

std::vector<patchlink::Wire> PatchLinkProcessor::getWires()
{
    std::vector<patchlink::Wire> w;
    for (auto& c : graph.getConnections())
    {
        if (c.source.nodeID == inputId && c.destination.nodeID == outputId) continue;
        w.push_back({ c.source.nodeID.uid, c.destination.nodeID.uid,
                      c.source.channelIndex, c.destination.channelIndex });
    }
    return w;
}

void PatchLinkProcessor::rebuildSerialIfNeeded()
{
    for (auto& c : graph.getConnections())
        graph.removeConnection(c);

    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    for (auto* n : graph.getNodes())
        if (n->nodeID != inputId && n->nodeID != outputId)
            ids.push_back(n->nodeID);
    std::sort(ids.begin(), ids.end(), [](auto a, auto b) { return a.uid < b.uid; });

    if (ids.empty())
    {
        graph.addConnection({ { inputId, 0 }, { outputId, 0 } });
        graph.addConnection({ { inputId, 1 }, { outputId, 1 } });
        return;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        graph.addConnection({ { inputId, ch }, { ids.front(), ch } });
        for (size_t i = 0; i + 1 < ids.size(); ++i)
            graph.addConnection({ { ids[i], ch }, { ids[i + 1], ch } });
        graph.addConnection({ { ids.back(), ch }, { outputId, ch } });
    }
}

// ---------- patches ----------
juce::var PatchLinkProcessor::graphToVar()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("app", "PatchLink");
    root->setProperty("chainFormat", chainformat::kFormatVersion);

    juce::Array<juce::var> jnodes;
    for (auto* n : graph.getNodes())
    {
        if (n->nodeID == inputId || n->nodeID == outputId) continue;
        auto* proc = n->getProcessor();
        juce::PluginDescription desc;
        if (auto* inst = dynamic_cast<juce::AudioPluginInstance*>(proc))
            desc = inst->getPluginDescription();
        jnodes.add(chainformat::makeNodeVar(desc, patchlink::stateToBase64(proc),
                                            (int)n->nodeID.uid,
                                            (int)n->properties.getWithDefault("x", 60),
                                            (int)n->properties.getWithDefault("y", 60),
                                            n->isBypassed()));
    }
    root->setProperty("nodes", jnodes);

    juce::Array<juce::var> jw;
    for (auto& c : graph.getConnections())
    {
        if (c.source.nodeID == inputId && c.destination.nodeID == outputId) continue;
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        auto mapId = [&](juce::AudioProcessorGraph::NodeID id) -> int {
            if (id == inputId) return 1;
            if (id == outputId) return 2;
            return (int)id.uid;
        };
        o->setProperty("src", mapId(c.source.nodeID));
        o->setProperty("srcCh", c.source.channelIndex);
        o->setProperty("dst", mapId(c.destination.nodeID));
        o->setProperty("dstCh", c.destination.channelIndex);
        jw.add(juce::var(o.get()));
    }
    root->setProperty("connections", jw);
    return juce::var(root.get());
}

bool PatchLinkProcessor::loadFromVarSyncLayout(const juce::var& root, juce::String& err)
{
    if (!root.isObject()) { err = "Not a patch"; return false; }
    if (auto* o = root.getDynamicObject())
        if (o->hasProperty("nodes")) return true;
    err = "Patch has no nodes";
    return false;
}

struct PatchLoadState
{
    struct NodeSpec
    {
        juce::PluginDescription desc;
        juce::String stateB64;
        int oldUid = 0, x = 60, y = 60;
        bool bypass = false;
    };
    std::vector<NodeSpec> nodes;
    std::vector<std::tuple<int,int,int,int>> wires; // src, srcCh, dst, dstCh (old uids; 1=in, 2=out)
    size_t index = 0;
    std::map<int, juce::AudioProcessorGraph::NodeID> uidMap;
    juce::StringArray missing;
};

void PatchLinkProcessor::loadFromVarAsync(const juce::var& root,
    std::function<void(juce::StringArray missing, juce::String err)> done)
{
    juce::String err;
    if (!loadFromVarSyncLayout(root, err)) { if (done) done({}, err); return; }

    auto state = std::make_shared<PatchLoadState>();
    if (auto* jnodes = root.getDynamicObject()->getProperty("nodes").getArray())
    {
        for (auto& jn : *jnodes)
        {
            auto* no = jn.getDynamicObject();
            if (no == nullptr) continue;
            PatchLoadState::NodeSpec spec;
            // v2 stores ids.* ; v1 stores flat fields — read both.
            if (auto* ids = no->getProperty("ids").getDynamicObject())
            {
                spec.desc.name = ids->getProperty("name").toString();
                spec.desc.manufacturerName = ids->getProperty("manufacturer").toString();
                spec.desc.version = ids->getProperty("version").toString();
                spec.desc.pluginFormatName = ids->getProperty("format").toString();
                spec.desc.fileOrIdentifier = ids->getProperty("fileOrId").toString();
            }
            else
            {
                spec.desc.name = no->getProperty("name").toString();
                spec.desc.manufacturerName = no->getProperty("manufacturer").toString();
                spec.desc.version = no->getProperty("pversion").toString();
                spec.desc.pluginFormatName = no->getProperty("format").toString();
                spec.desc.fileOrIdentifier = no->getProperty("fileOrId").toString();
            }
            // resolve against the scanned list (tolerates version drift)
            bool resolved = false;
            for (auto& t : knownList.getTypes())
                if (t.fileOrIdentifier == spec.desc.fileOrIdentifier) { spec.desc = t; resolved = true; break; }
            if (!resolved)
                for (auto& t : knownList.getTypes())
                    if (t.name == spec.desc.name && t.pluginFormatName == spec.desc.pluginFormatName) { spec.desc = t; resolved = true; break; }
            juce::ignoreUnused(resolved);
            spec.stateB64 = no->getProperty("stateBase64").toString();
            spec.oldUid = (int)no->getProperty("uid");
            spec.x = (int)no->getProperty("x");
            spec.y = (int)no->getProperty("y");
            spec.bypass = (bool)no->getProperty("bypass");
            state->nodes.push_back(spec);
        }
    }
    if (auto* jw = root.getDynamicObject()->getProperty("connections").getArray())
        for (auto& j : *jw)
            if (auto* co = j.getDynamicObject())
                state->wires.emplace_back((int)co->getProperty("src"), (int)co->getProperty("srcCh"),
                                          (int)co->getProperty("dst"), (int)co->getProperty("dstCh"));

    clearChain();
    state->uidMap[1] = inputId;
    state->uidMap[2] = outputId;
    auto self = this;
    auto next = std::make_shared<std::function<void()>>();
    *next = [self, state, done, next]() {
        if (state->index >= state->nodes.size())
        {
            bool hasWires = false;
            self->graph.suspendProcessing(true);
            for (auto& [s, sc, d, dc] : state->wires)
            {
                if (!state->uidMap.count(s) || !state->uidMap.count(d)) continue;
                self->graph.addConnection({ { state->uidMap[s], sc }, { state->uidMap[d], dc } });
                hasWires = true;
            }
            if (!hasWires) self->rebuildSerialIfNeeded();
            self->graph.suspendProcessing(false);
            if (done) done(state->missing, {});
            return;
        }
        auto spec = state->nodes[state->index++];
        self->formatManager.createPluginInstanceAsync(
            spec.desc, self->getSampleRate() > 0 ? self->getSampleRate() : 44100.0,
            self->getBlockSize() > 0 ? self->getBlockSize() : 512,
            [self, state, done, next, spec](std::unique_ptr<juce::AudioPluginInstance> inst,
                                            const juce::String&) mutable {
                if (inst == nullptr)
                {
                    state->missing.add(spec.desc.name.isNotEmpty() ? spec.desc.name
                                                                   : spec.desc.fileOrIdentifier);
                    (*next)();
                    return;
                }
                inst->enableAllBuses();
                self->graph.suspendProcessing(true);
                auto node = self->graph.addNode(std::move(inst),
                                                juce::AudioProcessorGraph::NodeID(self->nextUid++));
                if (node != nullptr)
                {
                    node->properties.set("x", spec.x);
                    node->properties.set("y", spec.y);
                    node->setBypassed(spec.bypass);
                    patchlink::base64ToState(node->getProcessor(), spec.stateB64);
                    state->uidMap[spec.oldUid] = node->nodeID;
                }
                else state->missing.add(spec.desc.name);
                self->graph.suspendProcessing(false);
                (*next)();
            });
    };
    (*next)();
}

bool PatchLinkProcessor::exportPatch(const juce::File& file, juce::String& err)
{
    auto v = graphToVar();
    if (!file.replaceWithText(juce::JSON::toString(v, true))) { err = "Write failed"; return false; }
    return true;
}

void PatchLinkProcessor::importPatchAsync(const juce::File& file,
    std::function<void(juce::StringArray missing, juce::String err)> done)
{
    auto txt = file.loadFileAsString();
    if (txt.isEmpty()) { if (done) done({}, "Empty file"); return; }
    auto v = juce::JSON::parse(txt);
    if (!v.isObject()) { if (done) done({}, "Invalid JSON"); return; }
    // v1 files carry "version" instead of "chainFormat" — still readable (see loader).
    loadFromVarAsync(v, done);
}

void PatchLinkProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream mos(dest, true);
    mos.writeString(juce::JSON::toString(graphToVar()));
    mos.writeInt(preferredMode);
}

void PatchLinkProcessor::setStateInformation(const void* data, int size)
{
    juce::MemoryInputStream mis(data, (size_t)size, false);
    auto json = mis.readString();
    auto v = juce::JSON::parse(json);
    if (v.isObject())
    {
        // DAW recall must not block: async load, state preserved in placeholders on failure.
        loadFromVarAsync(v, [](juce::StringArray, juce::String) {});
    }
    if (!mis.isExhausted()) preferredMode = mis.readInt();
}

// JUCE entry point: creates a new instance of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PatchLinkProcessor();
}
