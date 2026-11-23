// ScanTest: headless end-to-end check of DAW detection, scanning, and FL Studio .fst parsing/export.
// Usage: ScanTest [--full] [--folder "C:\path"] [--test-fst "C:\path\preset.fst" [--instantiate]] [--test-export "C:\path\preset.fst"]
#include <JuceHeader.h>
#include "../Scan/PluginScanManager.h"
#include "../Chain/FlStudioFstParser.h"
#include "../Chain/DawExport.h"

namespace
{
juce::String normalizeName(juce::String s)
{
    s = s.toLowerCase();
    for (;;)
    {
        const int open = s.lastIndexOfChar('(');
        if (open <= 0) break;
        const int close = s.indexOfChar(open, ')');
        if (close < 0) break;
        const auto inner = s.substring(open + 1, close).trim();
        if (inner == "x64" || inner == "x86" || inner == "vst3" || inner == "vst" || inner == "mono" || inner == "stereo")
            s = (s.substring(0, open) + s.substring(close + 1)).trim();
        else
            break;
    }
    return s.removeCharacters(" _-").trim();
}

bool resolveFromList(const juce::KnownPluginList& list, const flstudio::FstPluginEntry& entry, juce::PluginDescription& out)
{
    const auto types = list.getTypes();
    if (entry.juceUniqueId != 0)
        for (const auto& t : types)
            if (t.pluginFormatName == "VST3" && t.uniqueId == entry.juceUniqueId) { out = t; return true; }

    if (entry.filePath.isNotEmpty())
    {
        const auto leaf = juce::File(entry.filePath).getFileNameWithoutExtension().toLowerCase();
        std::vector<juce::PluginDescription> matches;
        for (const auto& t : types)
            if (t.pluginFormatName == "VST3" && juce::File(t.fileOrIdentifier).getFileNameWithoutExtension().toLowerCase() == leaf)
                matches.push_back(t);
        if (matches.size() == 1) { out = matches.front(); return true; }
    }

    const auto nPlugin = normalizeName(entry.pluginName);
    const auto nSlot = normalizeName(entry.slotName);
    for (const auto& t : types)
    {
        if (t.pluginFormatName != "VST3") continue;
        const auto tn = normalizeName(t.name);
        if ((nPlugin.isNotEmpty() && tn == nPlugin) || (nSlot.isNotEmpty() && tn == nSlot))
        { out = t; return true; }
    }
    return false;
}

int runFstTest(const juce::String& fstPath, bool instantiate)
{
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::KnownPluginList list;
    PluginScanManager mgr(fm, list);
    mgr.loadSettings();

    const juce::File file(fstPath);
    const auto parsed = flstudio::parseFstFile(file);
    printf("file: %s\n", file.getFullPathName().toRawUTF8());
    printf("valid: %s  mixerTrack: %s  trackName: \"%s\"  plugins: %d  skipped: %d\n",
           parsed.valid ? "true" : "false",
           parsed.isMixerTrack ? "true" : "false",
           parsed.trackName.toRawUTF8(),
           (int)parsed.plugins.size(),
           parsed.skipped.size());
    if (parsed.error.isNotEmpty())
        printf("error: %s\n", parsed.error.toRawUTF8());
    for (const auto& s : parsed.skipped)
        printf("  SKIP: %s\n", s.toRawUTF8());
    if (!parsed.valid)
        return 2;

    juce::AudioProcessorGraph testGraph;
    testGraph.setPlayConfigDetails(2, 2, 48000.0, 512);
    testGraph.prepareToPlay(48000.0, 512);
    auto graphIn = testGraph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), juce::AudioProcessorGraph::NodeID(10));
    auto graphOut = testGraph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), juce::AudioProcessorGraph::NodeID(20));
    std::vector<juce::AudioProcessorGraph::NodeID> graphChainIds;

    int resolvedCount = 0;
    for (size_t i = 0; i < parsed.plugins.size(); ++i)
    {
        const auto& e = parsed.plugins[i];
        juce::MemoryBlock stateBytes;
        if (e.stateBase64.isNotEmpty())
        {
            juce::MemoryOutputStream mos;
            if (juce::Base64::convertFromBase64(mos, e.stateBase64))
                stateBytes.append(mos.getData(), mos.getDataSize());
        }

        juce::PluginDescription desc;
        const bool resolved = resolveFromList(list, e, desc);
        if (resolved) ++resolvedCount;

        printf("  [%d] slot=\"%s\" plugin=\"%s\" vst2=%s bypass=%s uid=%08x stateBytes=%d -> %s (%s)\n",
               (int)i,
               e.slotName.toRawUTF8(),
               e.pluginName.toRawUTF8(),
               e.isVst2 ? "true" : "false",
               e.bypass ? "true" : "false",
               (unsigned)e.juceUniqueId,
               (int)stateBytes.getSize(),
               resolved ? desc.name.toRawUTF8() : "NOT FOUND",
               resolved ? desc.fileOrIdentifier.toRawUTF8() : "");

        if (instantiate && resolved)
        {
            juce::String err;
            auto inst = fm.createPluginInstance(desc, 48000.0, 512, err);
            if (!inst)
            {
                printf("      INSTANTIATE FAIL: %s\n", err.toRawUTF8());
                return 3;
            }
            inst->enableAllBuses();
            if (auto* bIn = inst->getBus(true, 0)) bIn->enable(true);
            if (auto* bOut = inst->getBus(false, 0)) bOut->enable(true);
            if (stateBytes.getSize() > 0)
                inst->setStateInformation(stateBytes.getData(), (int)stateBytes.getSize());
            juce::MemoryBlock verify;
            inst->getStateInformation(verify);
            printf("      INSTANTIATE OK: in=%d out=%d applied %d bytes, read back %d bytes\n",
                   inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(),
                   (int)stateBytes.getSize(), (int)verify.getSize());

            auto node = testGraph.addNode(std::move(inst), juce::AudioProcessorGraph::NodeID((uint32_t)(100 + i)));
            if (node != nullptr)
            {
                node->setBypassed(e.bypass);
                if (stateBytes.getSize() > 0)
                    node->getProcessor()->setStateInformation(stateBytes.getData(), (int)stateBytes.getSize());
                graphChainIds.push_back(node->nodeID);
            }
        }
    }

    if (instantiate && !graphChainIds.empty() && graphIn != nullptr && graphOut != nullptr)
    {
        auto getOutChans = [&](juce::AudioProcessorGraph::NodeID id) -> int {
            if (id == graphIn->nodeID) return 2;
            if (auto* n = testGraph.getNodeForId(id))
                if (auto* p = n->getProcessor())
                    return p->getBusCount(false) > 0 ? juce::jmax(1, p->getChannelCountOfBus(false, 0))
                                                     : juce::jmax(1, p->getTotalNumOutputChannels());
            return 2;
        };
        auto getInChans = [&](juce::AudioProcessorGraph::NodeID id) -> int {
            if (id == graphOut->nodeID) return 2;
            if (auto* n = testGraph.getNodeForId(id))
                if (auto* p = n->getProcessor())
                    return p->getBusCount(true) > 0 ? juce::jmax(1, p->getChannelCountOfBus(true, 0))
                                                    : juce::jmax(1, p->getTotalNumInputChannels());
            return 2;
        };
        auto connectPair = [&](juce::AudioProcessorGraph::NodeID src, juce::AudioProcessorGraph::NodeID dst) {
            const int srcOuts = getOutChans(src);
            const int dstIns  = getInChans(dst);
            testGraph.addConnection({ { src, 0 }, { dst, 0 } }, juce::AudioProcessorGraph::UpdateKind::none);
            if (dstIns >= 2)
                testGraph.addConnection({ { src, srcOuts >= 2 ? 1 : 0 }, { dst, 1 } }, juce::AudioProcessorGraph::UpdateKind::none);
        };
        connectPair(graphIn->nodeID, graphChainIds.front());
        for (size_t i = 0; i + 1 < graphChainIds.size(); ++i)
            connectPair(graphChainIds[i], graphChainIds[i + 1]);
        connectPair(graphChainIds.back(), graphOut->nodeID);
        testGraph.rebuild();

        juce::AudioBuffer<float> audioBuf(2, 512);
        juce::MidiBuffer midiBuf;
        double sumSqL = 0.0, sumSqR = 0.0;
        double phase = 0.0;
        const double phaseInc = 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0;
        for (int blk = 0; blk < 30; ++blk)
        {
            for (int s = 0; s < 512; ++s)
            {
                const float val = 0.25f * static_cast<float>(std::sin(phase));
                phase += phaseInc;
                audioBuf.setSample(0, s, val);
                audioBuf.setSample(1, s, val);
            }
            midiBuf.clear();
            testGraph.processBlock(audioBuf, midiBuf);
            if (blk >= 10)
            {
                for (int s = 0; s < 512; ++s)
                {
                    const float l = audioBuf.getSample(0, s);
                    const float r = audioBuf.getSample(1, s);
                    sumSqL += static_cast<double>(l) * l;
                    sumSqR += static_cast<double>(r) * r;
                }
            }
        }
        const float rmsL = static_cast<float>(std::sqrt(sumSqL / (20.0 * 512.0)));
        const float rmsR = static_cast<float>(std::sqrt(sumSqR / (20.0 * 512.0)));
        printf("  GRAPH AUDIO RMS: L=%.4f R=%.4f (connections=%d)\n",
               rmsL, rmsR, (int)testGraph.getConnections().size());
        if (rmsL < 0.0001f || rmsR < 0.0001f)
            return 4;
    }

    return (parsed.plugins.empty() && parsed.skipped.isEmpty()) ? 1 : (resolvedCount == 0 && !parsed.plugins.empty() ? 1 : 0);
}

int runExportRoundtripTest(const juce::String& fstPath)
{
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::KnownPluginList list;
    PluginScanManager mgr(fm, list);
    mgr.loadSettings();

    const auto parsed = flstudio::parseFstFile(juce::File(fstPath));
    if (!parsed.valid || parsed.plugins.empty())
    {
        printf("EXPORT TEST FAIL: could not parse input %s\n", fstPath.toRawUTF8());
        return 2;
    }

    const auto patchVar = flstudio::parsedFstToPatchVar(parsed, &list);
    auto* root = patchVar.getDynamicObject();
    auto* arr = root ? root->getProperty("nodes").getArray() : nullptr;
    if (arr == nullptr || arr->isEmpty())
    {
        printf("EXPORT TEST FAIL: parsedFstToPatchVar produced empty nodes\n");
        return 3;
    }

    juce::String err;
    juce::MemoryBlock fstOut;
    if (!dawexport::buildFlStudioMixerTrackFst(patchVar, fstOut, err))
    {
        printf("EXPORT TEST FAIL: buildFlStudioMixerTrackFst: %s\n", err.toRawUTF8());
        return 4;
    }

    const auto reparsed = flstudio::parseFstData(fstOut.getData(), fstOut.getSize());
    if (!reparsed.valid)
    {
        printf("EXPORT TEST FAIL: reparse error: %s\n", reparsed.error.toRawUTF8());
        return 5;
    }

    int expectedVst3 = 0;
    for (const auto& p : parsed.plugins)
        if (!p.isVst2) ++expectedVst3;

    printf("ROUNDTRIP FST: origVst3=%d reparsed=%d trackName=\"%s\" bytes=%d\n",
           expectedVst3, (int)reparsed.plugins.size(), reparsed.trackName.toRawUTF8(), (int)fstOut.getSize());
    if ((int)reparsed.plugins.size() != expectedVst3)
    {
        printf("EXPORT TEST FAIL: count mismatch\n");
        return 6;
    }

    for (size_t i = 0; i < reparsed.plugins.size(); ++i)
    {
        const auto& rp = reparsed.plugins[i];
        printf("  [%d] slot=\"%s\" tuid=%s bypass=%s stateB64Len=%d\n",
               (int)i, rp.slotName.toRawUTF8(), rp.tuidHex.toRawUTF8(),
               rp.bypass ? "true" : "false", rp.stateBase64.length());
        if (rp.tuidHex.length() != 32 || rp.stateBase64.isEmpty())
        {
            printf("EXPORT TEST FAIL: missing tuid or state on slot %d\n", (int)i);
            return 7;
        }
    }

    juce::MemoryBlock singleFst;
    if (!dawexport::buildFlStudioSingleFst(arr->getReference(0), singleFst, err))
    {
        printf("EXPORT TEST FAIL: single fst: %s\n", err.toRawUTF8());
        return 8;
    }
    const auto reparsedSingle = flstudio::parseFstData(singleFst.getData(), singleFst.getSize());
    if (!reparsedSingle.valid || reparsedSingle.plugins.size() != 1 || reparsedSingle.plugins[0].stateBase64.isEmpty())
    {
        printf("EXPORT TEST FAIL: single fst roundtrip failed\n");
        return 9;
    }
    printf("SINGLE FST OK: %s (%d bytes)\n", reparsedSingle.plugins[0].slotName.toRawUTF8(), (int)singleFst.getSize());

    juce::MemoryBlock vstPreset;
    if (!dawexport::buildSteinbergVstPreset(arr->getReference(0), vstPreset, err)
        || vstPreset.getSize() < 48 || std::memcmp(vstPreset.getData(), "VST3", 4) != 0)
    {
        printf("EXPORT TEST FAIL: .vstpreset: %s\n", err.toRawUTF8());
        return 10;
    }
    printf("VSTPRESET OK: %d bytes\n", (int)vstPreset.getSize());

    juce::String rfx;
    if (!dawexport::buildReaperRfxChain(patchVar, rfx, err) || !rfx.contains("<VST \"VST3:"))
    {
        printf("EXPORT TEST FAIL: .RfxChain: %s\n", err.toRawUTF8());
        return 11;
    }
    printf("RFXCHAIN OK: %d chars\n", rfx.length());

    const auto dragFiles = dawexport::prepareDragFiles(patchVar, dawexport::TargetFormat::flStudioFst, err, -1);
    if (dragFiles.isEmpty() || !juce::File(dragFiles[0]).existsAsFile())
    {
        printf("EXPORT TEST FAIL: prepareDragFiles: %s\n", err.toRawUTF8());
        return 12;
    }
    printf("PREPARE DRAG OK: %s\n", dragFiles[0].toRawUTF8());
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI lib;
    bool full = false;
    bool instantiate = false;
    juce::String singleFolder;
    juce::String testFstPath;
    juce::String testExportPath;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String(argv[i]) == "--full") full = true;
        if (juce::String(argv[i]) == "--instantiate") instantiate = true;
        if (juce::String(argv[i]) == "--folder" && i + 1 < argc) singleFolder = argv[++i];
        if (juce::String(argv[i]) == "--test-fst" && i + 1 < argc) testFstPath = argv[++i];
        if (juce::String(argv[i]) == "--test-export" && i + 1 < argc) testExportPath = argv[++i];
    }

    if (testExportPath.isNotEmpty())
        return runExportRoundtripTest(testExportPath);
    if (testFstPath.isNotEmpty())
        return runFstTest(testFstPath, instantiate);

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    printf("formats: %d\n", fm.getNumFormats());
    for (int i = 0; i < fm.getNumFormats(); ++i)
    {
        auto* f = fm.getFormat(i);
        printf("  [%s] defaults: %s\n", f->getName().toRawUTF8(),
               f->getDefaultLocationsToSearch().toString().toRawUTF8());
    }
    if (fm.getNumFormats() == 0) { printf("NO FORMATS - check JUCE_PLUGINHOST flags\n"); return 2; }

    juce::KnownPluginList list;
    PluginScanManager mgr(fm, list);
    mgr.loadSettings(); // pedal-file crash durability across runs
    mgr.buildDefaultSources();
    auto srcs = mgr.getSources();
    printf("sources: %d\n", (int)srcs.size());
    for (auto& s : srcs)
        printf("  %c %s  (%s)\n", s.enabled ? '+' : '-', s.path.toRawUTF8(), s.origin.toRawUTF8());
    printf("daw hits:\n");
    for (auto& h : daws::detectInstalledDaws())
        printf("  %s %s @ %s (%d folders)\n", h.name.toRawUTF8(), h.version.toRawUTF8(),
               h.installPath.toRawUTF8(), h.folders.size());

    if (singleFolder.isNotEmpty())
        mgr.setSources({ { singleFolder, "cli", true, true } });
    else if (!full && srcs.size() > 3)
    {
        std::vector<PluginScanManager::Source> cut(srcs.begin(), srcs.begin() + 3);
        mgr.setSources(cut);
    }

    juce::Time t0 = juce::Time::getCurrentTime();
    mgr.startScan(true);
    while (mgr.isScanning()) { printf("  ... %s\n", mgr.getCurrentFile().toRawUTF8()); juce::Thread::sleep(1000); }
    auto secs = (juce::Time::getCurrentTime() - t0).inSeconds();
    printf("scan done in %.0fs: %d types\n", secs, list.getNumTypes());
    int n = 0;
    for (auto& t : list.getTypes())
    {
        if (n++ >= 25) break;
        printf("  %s [%s] %s\n", t.name.toRawUTF8(), t.pluginFormatName.toRawUTF8(), t.version.toRawUTF8());
    }
    printf("failed: %d\n", mgr.getFailedFiles().size());
    for (auto f : mgr.getFailedFiles()) printf("  FAIL %s\n", f.toRawUTF8());
    printf("blacklisted: %d\n", list.getBlacklistedFiles().size());
    return list.getNumTypes() == 0 ? 1 : 0;
}
