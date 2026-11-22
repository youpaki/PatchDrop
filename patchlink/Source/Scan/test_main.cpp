// ScanTest: headless end-to-end check of DAW detection + scanning.
// Usage: ScanTest [--full] [--folder "C:\path"] — default: quick (first 3 folders only).
#include <JuceHeader.h>
#include "../Scan/PluginScanManager.h"

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI lib;
    bool full = false;
    juce::String singleFolder;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String(argv[i]) == "--full") full = true;
        if (juce::String(argv[i]) == "--folder" && i + 1 < argc) singleFolder = argv[++i];
    }

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
