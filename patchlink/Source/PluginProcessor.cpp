#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/ChainFormat.h"
#include "Chain/FlStudioFstParser.h"

PatchLinkProcessor::PatchLinkProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      scanManager(formatManager, knownList)
{
    formatManager.addDefaultFormats();
    scanManager.loadSettings();
    if (scanManager.getSources().empty())
        scanManager.buildDefaultSources(); // standards + detected DAW folders, no silent scan

    setPlayConfigDetails(2, 2, 44100.0, 512);
    graph.setPlayConfigDetails(2, 2, 44100.0, 512);

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
    const int ins  = juce::jmax(2, getMainBusNumInputChannels());
    const int outs = juce::jmax(2, getMainBusNumOutputChannels());
    graph.setPlayConfigDetails(ins, outs, sr, bs);
    graph.prepareToPlay(sr, bs);
    if (graph.getConnections().empty())
        rebuildSerialIfNeeded();
}

void PatchLinkProcessor::releaseResources() { graph.releaseResources(); }

void PatchLinkProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals d;
    graph.processBlock(b, m);
}

bool PatchLinkProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto inSet  = l.getMainInputChannelSet();
    const auto outSet = l.getMainOutputChannelSet();
    return (inSet == juce::AudioChannelSet::stereo() || inSet == juce::AudioChannelSet::mono())
        && (outSet == juce::AudioChannelSet::stereo() || outSet == juce::AudioChannelSet::mono());
}

// ---------- hosting ----------
std::vector<juce::AudioProcessorGraph::NodeID> PatchLinkProcessor::getOrderedNodeIds() const
{
    std::vector<juce::AudioProcessorGraph::Node*> nodes;
    for (auto* n : graph.getNodes())
        if (n->nodeID != inputId && n->nodeID != outputId)
            nodes.push_back(n);

    std::sort(nodes.begin(), nodes.end(), [](const auto* a, const auto* b) {
        const auto ao = (int)a->properties.getWithDefault("order", (int)a->nodeID.uid);
        const auto bo = (int)b->properties.getWithDefault("order", (int)b->nodeID.uid);
        return ao == bo ? a->nodeID.uid < b->nodeID.uid : ao < bo;
    });

    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    for (auto* n : nodes) ids.push_back(n->nodeID);
    return ids;
}

std::vector<PatchLinkProcessor::LoadedNode> PatchLinkProcessor::getLoadedNodes()
{
    std::vector<LoadedNode> out;
    for (auto id : getOrderedNodeIds())
    {
        auto* n = graph.getNodeForId(id);
        if (n == nullptr) continue;
        LoadedNode ln;
        ln.id = n->nodeID;
        if (auto* inst = dynamic_cast<juce::AudioPluginInstance*>(n->getProcessor()))
            ln.desc = inst->getPluginDescription();
        ln.x = n->properties.getWithDefault("x", 60);
        ln.y = n->properties.getWithDefault("y", 60);
        ln.order = n->properties.getWithDefault("order", 0);
        ln.bypass = n->isBypassed();
        out.push_back(ln);
    }
    return out;
}

static void configureMainBusesOnly(juce::AudioPluginInstance& inst)
{
    inst.enableAllBuses();
    if (auto* inBus = inst.getBus(true, 0))
        inBus->enable(true);
    if (auto* outBus = inst.getBus(false, 0))
        outBus->enable(true);
}

void PatchLinkProcessor::addPluginAsync(const juce::PluginDescription& desc, int x, int y, DoneFn done)
{
    addPluginAsync(desc, x, y, {}, false, std::move(done));
}

void PatchLinkProcessor::addPluginAsync(const juce::PluginDescription& desc, int x, int y,
                                        const juce::String& stateBase64, bool bypass, DoneFn done)
{
    // AUv3-safe: never sync-instantiate on the message thread.
    formatManager.createPluginInstanceAsync(
        desc, getSampleRate() > 0 ? getSampleRate() : 44100.0,
        getBlockSize() > 0 ? getBlockSize() : 512,
        [this, x, y, stateBase64, bypass, done](std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
            if (inst == nullptr) { if (done) done(err.isNotEmpty() ? err : "Could not load plugin"); return; }
            configureMainBusesOnly(*inst);
            if (stateBase64.isNotEmpty())
                patchlink::base64ToState(inst.get(), stateBase64);
            graph.suspendProcessing(true);
            auto node = graph.addNode(std::move(inst), juce::AudioProcessorGraph::NodeID(nextUid++));
            if (node != nullptr)
            {
                node->properties.set("x", x);
                node->properties.set("y", y);
                node->properties.set("order", nextOrder++);
                node->setBypassed(bypass);
                if (stateBase64.isNotEmpty())
                    patchlink::base64ToState(node->getProcessor(), stateBase64);
                if (autoConnectSerial) rebuildSerialIfNeeded();
            }
            graph.suspendProcessing(false);
            sendChangeMessage();
            if (done) done(node == nullptr ? juce::String("addNode failed") : juce::String{});
        });
}

void PatchLinkProcessor::removeNode(juce::AudioProcessorGraph::NodeID id)
{
    graph.suspendProcessing(true);
    graph.removeNode(id);
    if (autoConnectSerial) rebuildSerialIfNeeded();
    graph.suspendProcessing(false);
    sendChangeMessage();
}

void PatchLinkProcessor::moveNode(juce::AudioProcessorGraph::NodeID id, int newIndex)
{
    auto ids = getOrderedNodeIds();
    const auto old = std::find(ids.begin(), ids.end(), id);
    if (old == ids.end()) return;
    ids.erase(old);
    ids.insert(ids.begin() + juce::jlimit(0, (int)ids.size(), newIndex), id);
    for (size_t i = 0; i < ids.size(); ++i)
        if (auto* n = graph.getNodeForId(ids[i])) n->properties.set("order", (int)i);
    nextOrder = (int)ids.size();
    graph.suspendProcessing(true);
    if (autoConnectSerial) rebuildSerialIfNeeded();
    graph.suspendProcessing(false);
    sendChangeMessage();
}

juce::AudioProcessor* PatchLinkProcessor::getNodeProcessor(juce::AudioProcessorGraph::NodeID id)
{
    if (auto* n = graph.getNodeForId(id)) return n->getProcessor();
    return nullptr;
}

void PatchLinkProcessor::setNodeBypass(juce::AudioProcessorGraph::NodeID id, bool b)
{
    if (auto* n = graph.getNodeForId(id)) n->setBypassed(b);
    sendChangeMessage();
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
    const auto ids = getOrderedNodeIds();
    for (auto id : ids) graph.removeNode(id);
    nextOrder = 0;
    graph.suspendProcessing(false);
    sendChangeMessage();
}

bool PatchLinkProcessor::connectNodes(juce::AudioProcessorGraph::NodeID src, int srcCh,
                                       juce::AudioProcessorGraph::NodeID dst, int dstCh)
{
    const auto result = graph.addConnection({ { src, srcCh }, { dst, dstCh } });
    if (result) sendChangeMessage();
    return result;
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
    sendChangeMessage();
}

void PatchLinkProcessor::disconnect(juce::AudioProcessorGraph::NodeID src,
                                    juce::AudioProcessorGraph::NodeID dst)
{
    graph.suspendProcessing(true);
    juce::Array<juce::AudioProcessorGraph::Connection> toRemove;
    for (auto& c : graph.getConnections())
        if (c.source.nodeID == src && c.destination.nodeID == dst)
            toRemove.add(c);
    for (auto& c : toRemove) graph.removeConnection(c);
    graph.suspendProcessing(false);
    sendChangeMessage();
}

void PatchLinkProcessor::setPatchName(const juce::String& name)
{
    patchName = name.trim().isEmpty() ? "Untitled chain" : name.trim();
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
        graph.removeConnection(c, juce::AudioProcessorGraph::UpdateKind::none);

    auto allIds = getOrderedNodeIds();
    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    for (auto id : allIds)
    {
        if (auto* n = graph.getNodeForId(id))
        {
            auto* p = n->getProcessor();
            if (p != nullptr && p->getTotalNumInputChannels() > 0 && p->getTotalNumOutputChannels() > 0)
                ids.push_back(id);
        }
    }

    if (ids.empty())
    {
        graph.addConnection({ { inputId, 0 }, { outputId, 0 } }, juce::AudioProcessorGraph::UpdateKind::none);
        graph.addConnection({ { inputId, 1 }, { outputId, 1 } }, juce::AudioProcessorGraph::UpdateKind::none);
        graph.rebuild();
        return;
    }

    auto getOutChans = [&](juce::AudioProcessorGraph::NodeID id) -> int {
        if (id == inputId) return 2;
        if (auto* n = graph.getNodeForId(id))
            if (auto* p = n->getProcessor())
                return p->getBusCount(false) > 0 ? juce::jmax(1, p->getChannelCountOfBus(false, 0))
                                                 : juce::jmax(1, p->getTotalNumOutputChannels());
        return 2;
    };

    auto getInChans = [&](juce::AudioProcessorGraph::NodeID id) -> int {
        if (id == outputId) return 2;
        if (auto* n = graph.getNodeForId(id))
            if (auto* p = n->getProcessor())
                return p->getBusCount(true) > 0 ? juce::jmax(1, p->getChannelCountOfBus(true, 0))
                                                : juce::jmax(1, p->getTotalNumInputChannels());
        return 2;
    };

    auto connectPair = [&](juce::AudioProcessorGraph::NodeID src, juce::AudioProcessorGraph::NodeID dst) {
        const int srcOuts = getOutChans(src);
        const int dstIns  = getInChans(dst);
        graph.addConnection({ { src, 0 }, { dst, 0 } }, juce::AudioProcessorGraph::UpdateKind::none);
        if (dstIns >= 2)
            graph.addConnection({ { src, srcOuts >= 2 ? 1 : 0 }, { dst, 1 } }, juce::AudioProcessorGraph::UpdateKind::none);
    };

    connectPair(inputId, ids.front());
    for (size_t i = 0; i + 1 < ids.size(); ++i)
        connectPair(ids[i], ids[i + 1]);
    connectPair(ids.back(), outputId);
    graph.rebuild();
}

// ---------- patches ----------
juce::var PatchLinkProcessor::graphToVar()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("app", "PatchLink");
    root->setProperty("chainFormat", chainformat::kFormatVersion);
    root->setProperty("name", patchName);

    juce::Array<juce::var> jnodes;
    for (auto id : getOrderedNodeIds())
    {
        auto* n = graph.getNodeForId(id);
        if (n == nullptr) continue;
        auto* proc = n->getProcessor();
        juce::PluginDescription desc;
        if (auto* inst = dynamic_cast<juce::AudioPluginInstance*>(proc))
            desc = inst->getPluginDescription();
        auto nodeVar = chainformat::makeNodeVar(desc, patchlink::stateToBase64(proc),
                                                (int)n->nodeID.uid,
                                                (int)n->properties.getWithDefault("x", 60),
                                                (int)n->properties.getWithDefault("y", 60),
                                                n->isBypassed());
        nodeVar.getDynamicObject()->setProperty("order", (int)n->properties.getWithDefault("order", 0));
        jnodes.add(nodeVar);
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
        int order = 0;
    };
    std::vector<NodeSpec> nodes;
    std::vector<std::tuple<int,int,int,int>> wires; // src, srcCh, dst, dstCh (old uids; 1=in, 2=out)
    size_t index = 0;
    std::map<int, juce::AudioProcessorGraph::NodeID> uidMap;
    juce::StringArray missing;
};

static juce::String normalizePluginLookupKey(const juce::String& text)
{
    juce::String out;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = juce::CharacterFunctions::toLowerCase(text[i]);
        if (juce::CharacterFunctions::isLetterOrDigit(ch))
            out << ch;
    }
    return out;
}

bool PatchLinkProcessor::resolvePluginDescription(int uniqueId, const juce::String& identifier,
                                                  const juce::String& fileOrId, const juce::String& name,
                                                  const juce::String& format, const juce::String& slotName,
                                                  juce::PluginDescription& out) const
{
    const auto types = knownList.getTypes();

    if (identifier.isNotEmpty())
    {
        for (const auto& t : types)
            if (t.createIdentifierString() == identifier) { out = t; return true; }
    }

    if (uniqueId != 0)
    {
        for (const auto& t : types)
            if (t.uniqueId == uniqueId && (format.isEmpty() || t.pluginFormatName.equalsIgnoreCase(format)))
                { out = t; return true; }
        for (const auto& t : types)
            if (t.uniqueId == uniqueId) { out = t; return true; }
    }

    if (fileOrId.isNotEmpty())
    {
        int fileMatches = 0;
        juce::PluginDescription singleMatch;
        const auto normName = normalizePluginLookupKey(name);
        const auto normSlot = normalizePluginLookupKey(slotName);
        for (const auto& t : types)
        {
            if (t.fileOrIdentifier == fileOrId || t.fileOrIdentifier.equalsIgnoreCase(fileOrId))
            {
                if (format.isNotEmpty() && !t.pluginFormatName.equalsIgnoreCase(format))
                    continue;
                const auto tn = normalizePluginLookupKey(t.name);
                const auto tvn = normalizePluginLookupKey(t.manufacturerName + " " + t.name);
                if ((normSlot.isNotEmpty() && (tn == normSlot || tvn == normSlot))
                    || (normName.isNotEmpty() && (tn == normName || tvn == normName)))
                {
                    out = t;
                    return true;
                }
                singleMatch = t;
                ++fileMatches;
            }
        }
        if (fileMatches == 1)
        {
            out = singleMatch;
            return true;
        }
    }

    juce::StringArray candidates;
    if (slotName.isNotEmpty()) candidates.add(slotName);
    if (name.isNotEmpty() && !candidates.contains(name)) candidates.add(name);

    for (const auto& candidate : candidates)
    {
        for (const auto& t : types)
            if (t.name.equalsIgnoreCase(candidate) && (format.isEmpty() || t.pluginFormatName.equalsIgnoreCase(format)))
                { out = t; return true; }

        const auto normCandidate = normalizePluginLookupKey(candidate);
        if (normCandidate.isEmpty()) continue;

        for (const auto& t : types)
        {
            if (format.isNotEmpty() && !t.pluginFormatName.equalsIgnoreCase(format))
                continue;
            if (normalizePluginLookupKey(t.name) == normCandidate
                || normalizePluginLookupKey(t.manufacturerName + " " + t.name) == normCandidate)
            {
                out = t;
                return true;
            }
        }
    }

    return false;
}

void PatchLinkProcessor::loadFromVarAsync(const juce::var& root,
    std::function<void(juce::StringArray missing, juce::String err)> done)
{
    juce::String err;
    if (!loadFromVarSyncLayout(root, err)) { if (done) done({}, err); return; }
    if (auto* object = root.getDynamicObject())
    {
        const auto name = object->getProperty("name").toString();
        setPatchName(name.isNotEmpty() ? name : "Untitled chain");
    }

    auto state = std::make_shared<PatchLoadState>();
    if (auto* jnodes = root.getDynamicObject()->getProperty("nodes").getArray())
    {
        for (auto& jn : *jnodes)
        {
            auto* no = jn.getDynamicObject();
            if (no == nullptr) continue;
            PatchLoadState::NodeSpec spec;
            juce::String identifier;
            int uniqueId = 0;
            // v2 stores ids.* ; v1 stores flat fields — read both.
            if (auto* ids = no->getProperty("ids").getDynamicObject())
            {
                spec.desc.name = ids->getProperty("name").toString();
                spec.desc.manufacturerName = ids->getProperty("manufacturer").toString();
                spec.desc.version = ids->getProperty("version").toString();
                spec.desc.pluginFormatName = ids->getProperty("format").toString();
                spec.desc.fileOrIdentifier = ids->getProperty("fileOrId").toString();
                identifier = ids->getProperty("identifier").toString();
                const auto uidVar = ids->getProperty("uniqueId");
                if (!uidVar.isVoid()) uniqueId = static_cast<int>(uidVar);
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
            juce::PluginDescription resolvedDesc;
            if (resolvePluginDescription(uniqueId, identifier, spec.desc.fileOrIdentifier,
                                         spec.desc.name, spec.desc.pluginFormatName, {}, resolvedDesc))
            {
                spec.desc = resolvedDesc;
            }
            spec.stateB64 = no->getProperty("stateBase64").toString();
            spec.oldUid = (int)no->getProperty("uid");
            spec.x = (int)no->getProperty("x");
            spec.y = (int)no->getProperty("y");
            spec.bypass = (bool)no->getProperty("bypass");
            const auto order = no->getProperty("order");
            spec.order = order.isVoid() ? (int)state->nodes.size() : (int)order;
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
            self->sendChangeMessage();
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
                configureMainBusesOnly(*inst);
                patchlink::base64ToState(inst.get(), spec.stateB64);
                self->graph.suspendProcessing(true);
                auto node = self->graph.addNode(std::move(inst),
                                                juce::AudioProcessorGraph::NodeID(self->nextUid++));
                if (node != nullptr)
                {
                    node->properties.set("x", spec.x);
                    node->properties.set("y", spec.y);
                    node->properties.set("order", spec.order);
                    self->nextOrder = juce::jmax(self->nextOrder, spec.order + 1);
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
    if (file.hasFileExtension(".fst"))
    {
        const auto index = static_cast<int>(getLoadedNodes().size());
        importFlStudioFstAsync(file, 282 + (index % 5) * 224, 208 + (index / 5) * 132, true,
            [done](bool, int, juce::StringArray skippedOrMissing, juce::String err) {
                if (done) done(std::move(skippedOrMissing), std::move(err));
            });
        return;
    }
    auto txt = file.loadFileAsString();
    if (txt.isEmpty()) { if (done) done({}, "Empty file"); return; }
    auto v = juce::JSON::parse(txt);
    if (!v.isObject()) { if (done) done({}, "Invalid JSON"); return; }
    // v1 files carry "version" instead of "chainFormat" — still readable (see loader).
    loadFromVarAsync(v, done);
}

void PatchLinkProcessor::importFlStudioFstAsync(const juce::File& file, int dropX, int dropY,
    bool appendSingleToChain,
    std::function<void(bool isMixerTrack, int loadedCount, juce::StringArray skippedOrMissing, juce::String err)> done)
{
    importParsedFstAsync(flstudio::parseFstFile(file), dropX, dropY, appendSingleToChain, std::move(done));
}

void PatchLinkProcessor::importParsedFstAsync(const flstudio::ParsedFst& parsed, int dropX, int dropY,
    bool appendSingleToChain,
    std::function<void(bool isMixerTrack, int loadedCount, juce::StringArray skippedOrMissing, juce::String err)> done)
{
    if (!parsed.valid)
    {
        if (done) done(false, 0, {}, parsed.error.isNotEmpty() ? parsed.error : "Invalid .fst file");
        return;
    }

    if (parsed.plugins.empty())
    {
        if (done)
            done(parsed.isMixerTrack, 0, parsed.skipped,
                 parsed.skipped.isEmpty() ? "No plugins found in this .fst file." : juce::String{});
        return;
    }

    if (!parsed.isMixerTrack && parsed.plugins.size() == 1 && appendSingleToChain)
    {
        const auto& entry = parsed.plugins.front();
        const auto displayName = entry.slotName.isNotEmpty() ? entry.slotName : entry.pluginName;
        juce::PluginDescription desc;

        if (entry.isVst2)
        {
            if (resolvePluginDescription(0, {}, {}, entry.pluginName, "VST3", entry.slotName, desc))
            {
                addPluginAsync(desc, dropX, dropY, {}, entry.bypass, [done, displayName](const juce::String& err) {
                    juce::StringArray notes;
                    if (err.isEmpty())
                        notes.add(displayName + " (loaded VST3; VST2 preset state cannot transfer)");
                    if (done) done(false, err.isEmpty() ? 1 : 0, std::move(notes), err);
                });
            }
            else if (done)
            {
                done(false, 0, { displayName + " (VST2 - VST3 required)" }, {});
            }
            return;
        }

        if (!resolvePluginDescription(entry.juceUniqueId, {}, entry.filePath, entry.pluginName,
                                      "VST3", entry.slotName, desc))
        {
            if (done) done(false, 0, { displayName }, {});
            return;
        }

        addPluginAsync(desc, dropX, dropY, entry.stateBase64, entry.bypass, [done, displayName](const juce::String& err) {
            juce::StringArray notes;
            if (err.isNotEmpty()) notes.add(displayName);
            if (done) done(false, err.isEmpty() ? 1 : 0, std::move(notes), err);
        });
        return;
    }

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("app", "PatchLink");
    root->setProperty("chainFormat", chainformat::kFormatVersion);
    root->setProperty("name", parsed.trackName.isNotEmpty() ? parsed.trackName : "FL Mixer Chain");

    juce::Array<juce::var> jnodes;
    juce::StringArray extraNotes = parsed.skipped;
    int idx = 0;

    for (const auto& entry : parsed.plugins)
    {
        const auto displayName = entry.slotName.isNotEmpty() ? entry.slotName : entry.pluginName;
        juce::PluginDescription desc;

        if (entry.isVst2)
        {
            if (resolvePluginDescription(0, {}, {}, entry.pluginName, "VST3", entry.slotName, desc))
            {
                extraNotes.add(displayName + " (loaded VST3 without VST2 state)");
            }
            else
            {
                extraNotes.add(displayName + " (VST2 - VST3 required)");
                continue;
            }
        }
        else if (!resolvePluginDescription(entry.juceUniqueId, {}, entry.filePath, entry.pluginName,
                                           "VST3", entry.slotName, desc))
        {
            desc.name = displayName;
            desc.manufacturerName = entry.vendor;
            desc.pluginFormatName = "VST3";
            desc.fileOrIdentifier = entry.filePath;
            desc.uniqueId = entry.juceUniqueId;
        }

        const int nx = 280 + (idx % 4) * 230;
        const int ny = 200 + (idx / 4) * 140;
        auto nodeVar = chainformat::makeNodeVar(desc, entry.isVst2 ? juce::String{} : entry.stateBase64,
                                                100 + idx, nx, ny, entry.bypass);
        nodeVar.getDynamicObject()->setProperty("order", idx);
        if (desc.uniqueId != 0)
            if (auto* ids = nodeVar.getDynamicObject()->getProperty("ids").getDynamicObject())
                ids->setProperty("uniqueId", desc.uniqueId);
        jnodes.add(nodeVar);
        ++idx;
    }

    if (jnodes.isEmpty())
    {
        if (done) done(parsed.isMixerTrack, 0, extraNotes, {});
        return;
    }

    root->setProperty("nodes", jnodes);
    root->setProperty("connections", juce::Array<juce::var>());

    const int attempted = jnodes.size();
    const bool isMixer = parsed.isMixerTrack;
    loadFromVarAsync(juce::var(root.get()),
        [done, extraNotes, attempted, isMixer](juce::StringArray missing, juce::String err) mutable {
            for (const auto& note : extraNotes)
                missing.add(note);
            const int loaded = err.isEmpty() ? juce::jmax(0, attempted - (missing.size() - extraNotes.size())) : 0;
            if (done) done(isMixer, loaded, std::move(missing), std::move(err));
        });
}

void PatchLinkProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream mos(dest, false);
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
