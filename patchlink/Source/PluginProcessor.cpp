#include "PluginProcessor.h"
#include "PluginEditor.h"

PatchLinkProcessor::PatchLinkProcessor()
    : AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::stereo(), true)
                                       .withOutput("Out", juce::AudioChannelSet::stereo(), true))
{
    formatManager.addDefaultFormats();

    // Restaure la liste des plugins déjà scannés
    auto listFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("PatchLink").getChildFile("knownPlugins.xml");
    if (listFile.existsAsFile())
        if (auto xml = juce::parseXML(listFile.loadFileAsString()))
            knownList.recreateFromXml(*xml);

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

// ---------- Hosting ----------
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

juce::String PatchLinkProcessor::addPluginByDescription(const juce::PluginDescription& desc, int x, int y)
{
    juce::String err;
    graph.suspendProcessing(true);
    auto inst = formatManager.createPluginInstance(desc, getSampleRate() > 0 ? getSampleRate() : 44100.0,
                                                   getBlockSize() > 0 ? getBlockSize() : 512, err);
    if (inst == nullptr) { graph.suspendProcessing(false); return err; }

    auto node = graph.addNode(std::move(inst), juce::AudioProcessorGraph::NodeID(nextUid++));
    if (node == nullptr) { graph.suspendProcessing(false); return "addNode failed"; }
    node->properties.set("x", x);
    node->properties.set("y", y);

    if (autoConnectSerial)
        rebuildSerialIfNeeded();
    graph.suspendProcessing(false);
    return {};
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
        // Ignore la connexion directe in->out quand la chaîne est vide
        if (c.source.nodeID == inputId && c.destination.nodeID == outputId) continue;
        w.push_back({ c.source.nodeID.uid, c.destination.nodeID.uid,
                      c.source.channelIndex, c.destination.channelIndex });
    }
    return w;
}

// Série auto : in -> n1 -> n2 -> ... -> out (stéréo ch 0/1)
void PatchLinkProcessor::rebuildSerialIfNeeded()
{
    for (auto& c : graph.getConnections())
        graph.removeConnection(c);

    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    for (auto* n : graph.getNodes())
        if (n->nodeID != inputId && n->nodeID != outputId)
            ids.push_back(n->nodeID);
    // tri par uid = ordre d'ajout
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

void PatchLinkProcessor::scanDefaultPathsAsync()
{
    // Scan rapide en thread de fond : VST3 (+ AU sur mac automatiquement via le format)
    struct Scanner : juce::Thread
    {
        PatchLinkProcessor& o;
        Scanner(PatchLinkProcessor& p) : Thread("scan"), o(p) {}
        void run() override
        {
            juce::FileSearchPath paths;
           #if JUCE_MAC
            paths.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
            paths.add(juce::File("~/Library/Audio/Plug-Ins/VST3"));
           #else
            paths.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
            paths.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
           #endif
            for (int fi = 0; fi < o.formatManager.getNumFormats(); ++fi)
            {
                if (threadShouldExit()) break;
                auto* fmt = o.formatManager.getFormat(fi);
                if (fmt == nullptr) continue;
                juce::PluginDirectoryScanner scanner(o.knownList, *fmt, paths, true, juce::File());
                juce::String failed;
                while (scanner.scanNextFile(true, failed) && !threadShouldExit()) {}
            }
            auto listFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("PatchLink").getChildFile("knownPlugins.xml");
            listFile.getParentDirectory().createDirectory();
            if (auto xml = o.knownList.createXml())
                listFile.replaceWithText(xml->toString());
        }
    };
    // Self-owned background thread: lives until the scan finishes.
    (new Scanner(*this))->startThread();
}

// ---------- Patch save/load ----------
juce::var PatchLinkProcessor::graphToVar()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("app", "PatchLink");
    root->setProperty("version", patchlink::kFileVersion);

    juce::Array<juce::var> jnodes;
    for (auto* n : graph.getNodes())
    {
        if (n->nodeID == inputId || n->nodeID == outputId) continue;
        auto* proc = n->getProcessor();
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("uid", (int)n->nodeID.uid);
        o->setProperty("stateBase64", patchlink::stateToBase64(proc));
        o->setProperty("x", (int)n->properties.getWithDefault("x", 60));
        o->setProperty("y", (int)n->properties.getWithDefault("y", 60));
        o->setProperty("bypass", n->isBypassed());
        if (auto* inst = dynamic_cast<juce::AudioPluginInstance*>(proc))
        {
            auto& d = inst->getPluginDescription();
            o->setProperty("name", d.name);
            o->setProperty("manufacturer", d.manufacturerName);
            o->setProperty("pversion", d.version);
            o->setProperty("format", d.pluginFormatName);
            o->setProperty("fileOrId", d.fileOrIdentifier);
        }
        jnodes.add(juce::var(o.get()));
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

bool PatchLinkProcessor::loadFromVar(const juce::var& root, juce::StringArray& missing)
{
    missing.clear();
    if (!root.isObject()) return false;
    auto* o = root.getDynamicObject();
    if (o == nullptr || !o->hasProperty("nodes")) return false;

    graph.suspendProcessing(true);
    clearChain();
    // suspend déjà géré : on dégèle à la fin
    graph.suspendProcessing(false);
    graph.suspendProcessing(true);

    std::map<int, juce::AudioProcessorGraph::NodeID> uidMap;
    uidMap[1] = inputId; uidMap[2] = outputId;

    if (auto* jnodes = o->getProperty("nodes").getArray())
    {
        for (auto& jn : *jnodes)
        {
            auto* no = jn.getDynamicObject();
            if (no == nullptr) continue;
            juce::String name = no->getProperty("name").toString();
            juce::String fileOrId = no->getProperty("fileOrId").toString();
            juce::String fmt = no->getProperty("format").toString();

            // cherche dans la liste connue (tolère changement de version)
            juce::PluginDescription found;
            bool hasFound = false;
            for (auto& t : knownList.getTypes())
            {
                if (t.fileOrIdentifier == fileOrId) { found = t; hasFound = true; break; }
            }
            if (!hasFound)
                for (auto& t : knownList.getTypes())
                    if (t.name == name && t.pluginFormatName == fmt) { found = t; hasFound = true; break; }

            if (!hasFound) // dernier essai : reconstruit depuis le patch
            {
                found.name = name;
                found.fileOrIdentifier = fileOrId;
                found.pluginFormatName = fmt;
                found.manufacturerName = no->getProperty("manufacturer").toString();
                found.version = no->getProperty("pversion").toString();
            }

            juce::String err;
            auto inst = formatManager.createPluginInstance(found, getSampleRate() > 0 ? getSampleRate() : 44100.0,
                                                           getBlockSize() > 0 ? getBlockSize() : 512, err);
            if (inst == nullptr)
            {
                missing.add(name.isNotEmpty() ? name : fileOrId);
                continue;
            }
            int oldUid = (int)no->getProperty("uid");
            auto node = graph.addNode(std::move(inst), juce::AudioProcessorGraph::NodeID(nextUid++));
            if (node == nullptr) { missing.add(name); continue; }
            node->properties.set("x", (int)no->getProperty("x"));
            node->properties.set("y", (int)no->getProperty("y"));
            node->setBypassed((bool)no->getProperty("bypass"));
            patchlink::base64ToState(node->getProcessor(), no->getProperty("stateBase64").toString());
            uidMap[oldUid] = node->nodeID;
        }
    }

    bool hasWires = false;
    if (auto* jw = o->getProperty("connections").getArray())
    {
        for (auto& j : *jw)
        {
            auto* co = j.getDynamicObject();
            if (co == nullptr) continue;
            int s = (int)co->getProperty("src"), d = (int)co->getProperty("dst");
            if (!uidMap.count(s) || !uidMap.count(d)) continue;
            graph.addConnection({ { uidMap[s], (int)co->getProperty("srcCh") },
                                  { uidMap[d], (int)co->getProperty("dstCh") } });
            hasWires = true;
        }
    }
    if (!hasWires) rebuildSerialIfNeeded(); // vieux patch ou tout manquant
    graph.suspendProcessing(false);
    return true;
}

bool PatchLinkProcessor::exportPatch(const juce::File& file, juce::String& err)
{
    auto v = graphToVar();
    auto json = juce::JSON::toString(v, true);
    if (!file.replaceWithText(json)) { err = "Ecriture impossible"; return false; }
    return true;
}

bool PatchLinkProcessor::importPatch(const juce::File& file, juce::StringArray& missing, juce::String& err)
{
    auto txt = file.loadFileAsString();
    if (txt.isEmpty()) { err = "Fichier vide"; return false; }
    auto v = juce::JSON::parse(txt);
    if (!v.isObject()) { err = "JSON invalide"; return false; }
    if (!loadFromVar(v, missing)) { err = "Patch illisible"; return false; }
    return true;
}

void PatchLinkProcessor::getStateInformation(juce::MemoryBlock& dest){
    juce::MemoryOutputStream mos(dest, true);
    mos.writeString(juce::JSON::toString(graphToVar()));
    mos.writeInt(preferredMode);
}

void PatchLinkProcessor::setStateInformation(const void* data, int size)
{
    juce::MemoryInputStream mis(data, (size_t)size, false);
    auto json = mis.readString();
    juce::StringArray missing;
    auto v = juce::JSON::parse(json);
    if (v.isObject()) loadFromVar(v, missing);
    if (!mis.isExhausted()) preferredMode = mis.readInt();
}

// JUCE entry point: creates a new instance of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PatchLinkProcessor();
}
