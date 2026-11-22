#include "PluginScanManager.h"

PluginScanManager::PluginScanManager(juce::AudioPluginFormatManager& fm, juce::KnownPluginList& list)
    : formats(fm), knownList(list)
{
}

PluginScanManager::~PluginScanManager()
{
    cancelFlag.set(1);
    pool.removeAllJobs(true, 3000);
}

void PluginScanManager::setSources(const std::vector<Source>& s)
{
    juce::ScopedLock l(stateLock);
    sources = s;
    saveSettings();
}

std::vector<PluginScanManager::Source> PluginScanManager::getSources() const
{
    juce::ScopedLock l(const_cast<juce::CriticalSection&>(stateLock));
    return sources;
}

void PluginScanManager::addSource(const Source& s)
{
    juce::ScopedLock l(stateLock);
    for (auto& e : sources)
#if JUCE_WINDOWS
        if (e.path.toLowerCase() == s.path.toLowerCase()) return;
#else
        if (e.path == s.path) return;
#endif
    sources.push_back(s);
    saveSettings();
}

void PluginScanManager::buildDefaultSources()
{
    std::vector<Source> def;
    for (auto& f : daws::standardFolders())
        if (f.exists) def.push_back({ f.path, f.origin, true, true });
    for (auto& hit : daws::detectInstalledDaws())
        for (auto& f : hit.folders)
            if (f.exists)
            {
                Source s { f.path, hit.name + ": " + f.origin, f.enabledByDefault, true };
                bool dup = false;
                for (auto& e : def)
#if JUCE_WINDOWS
                    if (e.path.toLowerCase() == s.path.toLowerCase()) { dup = true; break; }
#else
                    if (e.path == s.path) { dup = true; break; }
#endif
                if (!dup) def.push_back(s);
            }
    juce::ScopedLock l(stateLock);
    sources = def;
    saveSettings();
}

void PluginScanManager::startScan(bool fullRescan)
{
    if (isScanning()) return;
    cancelFlag.set(0);
    scanning.set(1);
    progress.set(0.0f);
    numFound.set(0);
    { juce::ScopedLock l(stateLock); failedFiles.clear(); }
    pool.addJob(new ScanJob(*this, fullRescan), true);
}

void PluginScanManager::cancelScan()
{
    cancelFlag.set(1);
}

juce::String PluginScanManager::getCurrentFile() const
{
    juce::ScopedLock l(const_cast<juce::CriticalSection&>(stateLock));
    return currentFile;
}

juce::StringArray PluginScanManager::getFailedFiles() const
{
    juce::ScopedLock l(const_cast<juce::CriticalSection&>(stateLock));
    return failedFiles;
}

juce::File PluginScanManager::getSettingsDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("leve").getChildFile("PatchLink");
}

juce::File PluginScanManager::getPedalFile()
{
    return getSettingsDir().getChildFile("RecentlyCrashedPluginsList");
}

void PluginScanManager::saveSettings()
{
    auto dir = getSettingsDir();
    dir.createDirectory();
    auto* root = new juce::XmlElement("PatchLinkScan");
    {
        juce::ScopedLock l(stateLock);
        auto* s = new juce::XmlElement("Sources");
        for (auto& src : sources)
        {
            auto* e = new juce::XmlElement("Source");
            e->setAttribute("path", src.path);
            e->setAttribute("origin", src.origin);
            e->setAttribute("enabled", src.enabled);
            e->setAttribute("recursive", src.recursive);
            s->addChildElement(e);
        }
        root->addChildElement(s);
    }
    if (auto listXml = knownList.createXml())
        root->addChildElement(listXml.release());
    root->writeTo(dir.getChildFile("scan-settings.xml"));
    delete root;
}

void PluginScanManager::loadSettings()
{
    auto file = getSettingsDir().getChildFile("scan-settings.xml");
    if (!file.existsAsFile()) return;
    if (auto xml = juce::parseXML(file.loadFileAsString()))
    {
        std::vector<Source> loaded;
        if (auto* s = xml->getChildByName("Sources"))
            for (auto* e : s->getChildIterator())
                if (e->hasTagName("Source"))
                    loaded.push_back({ e->getStringAttribute("path"), e->getStringAttribute("origin"),
                                       e->getBoolAttribute("enabled", true),
                                       e->getBoolAttribute("recursive", true) });
        juce::ScopedLock l(stateLock);
        if (!loaded.empty()) sources = loaded;
        // plugin list child is the element that isn't "Sources"
        for (auto* child : xml->getChildIterator())
            if (!child->hasTagName("Sources")) { knownList.recreateFromXml(*child); break; }
    }
    // Crash durability: blacklist whatever died mid-scan last time, then clear the pedal.
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(knownList, getPedalFile());
    getPedalFile().deleteFile();
}

juce::ThreadPoolJob::JobStatus PluginScanManager::ScanJob::runJob()
{
    auto& o = owner;
    // snapshot sources under lock
    std::vector<PluginScanManager::Source> srcs;
    { juce::ScopedLock l(o.stateLock); srcs = o.sources; }
    juce::FileSearchPath enabledRec, enabledFlat;
    for (auto& s : srcs)
        if (s.enabled)
            (s.recursive ? enabledRec : enabledFlat).add(juce::File(s.path));

    int nFormats = o.formats.getNumFormats();
    int done = 0;
    const int totalPasses = nFormats * 2;
    auto runScanner = [&](juce::AudioPluginFormat* fmt, const juce::FileSearchPath& paths, bool recursive, int& passDone) {
        if (paths.getNumPaths() == 0 && fmt->getDefaultLocationsToSearch().getNumPaths() == 0)
        {
            // Formats like AU enumerate the OS registry — still worth one pass with empty paths.
            // Formats that truly need a folder list skip when there is nothing to look at.
            if (paths.getNumPaths() == 0 && !fmt->getName().containsIgnoreCase("AudioUnit")) { ++passDone; return; }
        }
        bool allowAsync = true; // include AUv3-style plugins
        juce::PluginDirectoryScanner scanner(o.knownList, *fmt, paths, recursive,
                                             PluginScanManager::getPedalFile(), allowAsync);
        juce::String beingScanned;
        while (scanner.scanNextFile(!fullRescan, beingScanned))
        {
            if (o.cancelFlag.get() || shouldExit()) break;
            {
                juce::ScopedLock l(o.stateLock);
                o.currentFile = beingScanned;
                o.failedFiles = scanner.getFailedFiles();
            }
            o.numFound.set(o.knownList.getNumTypes());
            o.progress.set((passDone + scanner.getProgress()) / (float)juce::jmax(1, totalPasses));
        }
        {
            juce::ScopedLock l(o.stateLock);
            o.failedFiles = scanner.getFailedFiles();
        }
        o.numFound.set(o.knownList.getNumTypes());
        ++passDone;
        o.progress.set(passDone / (float)juce::jmax(1, totalPasses));
        o.saveSettings(); // incremental: a crash loses minimal work
    };

    for (int fi = 0; fi < nFormats; ++fi)
    {
        if (o.cancelFlag.get() || shouldExit()) break;
        auto* fmt = o.formats.getFormat(fi);
        if (fmt == nullptr) { done += 2; continue; }
        runScanner(fmt, enabledFlat, false, done);
        if (o.cancelFlag.get() || shouldExit()) break;
        runScanner(fmt, enabledRec, true, done);
    }

    o.scanning.set(0);
    o.progress.set(1.0f);
    o.saveSettings();
    o.sendChangeMessage(); // picked up by UI (message thread)
    return juce::ThreadPoolJob::jobHasFinished;
}
