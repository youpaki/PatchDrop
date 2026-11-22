#pragma once
#include <JuceHeader.h>
#include "DawDetector.h"

// PluginScanManager: background, crash-durable plugin scanning.
// - One PluginDirectoryScanner per format (JUCE 8 API), sequential on a ThreadPool(1).
// - Dead-man's-pedal file + KnownPluginList blacklist isolate crashing plugins.
// - Incremental persistence: settings XML (sources) + plugin list XML after every scan.
// - UI polls getProgress()/getCurrentFile() on a Timer; cancelScan() is cooperative.
// - CustomScanner hook left open for a future out-of-process worker (JUCE Host pattern).
class PluginScanManager : public juce::ChangeBroadcaster
{
public:
    struct Source
    {
        juce::String path;
        juce::String origin;      // "VST3 system", "FL Studio extra search", "My folder"...
        bool enabled = true;
        bool recursive = true;
    };

    PluginScanManager(juce::AudioPluginFormatManager& fm, juce::KnownPluginList& list);
    ~PluginScanManager() override;

    void setSources(const std::vector<Source>& s);
    std::vector<Source> getSources() const;
    void addSource(const Source& s); // dedupes by path

    // First run: standards (on) + DAW folders (on if they exist). Never rescans silently.
    void buildDefaultSources();

    void startScan(bool fullRescan);
    void cancelScan();
    bool isScanning() const { return scanning.get() != 0; }
    float getProgress() const { return progress.get(); }
    juce::String getCurrentFile() const;
    int getNumFound() const { return numFound.get(); }
    juce::StringArray getFailedFiles() const;

    static juce::File getSettingsDir();
    static juce::File getPedalFile();
    void saveSettings();
    void loadSettings(); // sources + plugin list; call once at startup

    juce::KnownPluginList& getList() { return knownList; }

private:
    struct ScanJob : public juce::ThreadPoolJob
    {
        PluginScanManager& owner;
        bool fullRescan;
        ScanJob(PluginScanManager& o, bool full) : ThreadPoolJob("pluginscan"), owner(o), fullRescan(full) {}
        JobStatus runJob() override;
    };

    juce::AudioPluginFormatManager& formats;
    juce::KnownPluginList& knownList;
    juce::ThreadPool pool { 1 };
    juce::CriticalSection stateLock;
    std::vector<Source> sources;
    juce::Atomic<int> scanning { 0 };
    juce::Atomic<float> progress { 0.0f };
    juce::Atomic<int> numFound { 0 };
    juce::String currentFile;
    juce::StringArray failedFiles;
    juce::Atomic<int> cancelFlag { 0 };

    friend struct ScanJob;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanManager)
};
