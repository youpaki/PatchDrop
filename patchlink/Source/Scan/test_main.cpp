// ScanTest: headless end-to-end check of DAW detection, scanning, and FL Studio .fst parsing.
// Usage: ScanTest [--full] [--folder "C:\path"] [--test-fst "C:\path\preset.fst" [--instantiate]]
#include <JuceHeader.h>
#include "../Scan/PluginScanManager.h"
#include "../Chain/FlStudioFstParser.h"

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
            if (stateBytes.getSize() > 0)
                inst->setStateInformation(stateBytes.getData(), (int)stateBytes.getSize());
            juce::MemoryBlock verify;
            inst->getStateInformation(verify);
            printf("      INSTANTIATE OK: applied %d bytes, read back %d bytes\n",
                   (int)stateBytes.getSize(), (int)verify.getSize());
        }
    }
    return (parsed.plugins.empty() && parsed.skipped.isEmpty()) ? 1 : (resolvedCount == 0 && !parsed.plugins.empty() ? 1 : 0);
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
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String(argv[i]) == "--full") full = true;
        if (juce::String(argv[i]) == "--instantiate") instantiate = true;
        if (juce::String(argv[i]) == "--folder" && i + 1 < argc) singleFolder = argv[++i];
        if (juce::String(argv[i]) == "--test-fst" && i + 1 < argc) testFstPath = argv[++i];
    }

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
