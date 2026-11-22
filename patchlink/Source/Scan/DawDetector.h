#pragma once
#include <JuceHeader.h>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

// DawDetector: READ-ONLY discovery of installed DAWs + their custom VST folders.
// Never writes into DAW configs — results only feed our own scanner input.
// Every function fails soft (empty result) when keys/files are absent.
// Must-verify-on-real-machine items are handled with graceful fallbacks.
namespace daws {

struct DawFolder
{
    juce::String path;        // normalized absolute dir (existing only, unless includeMissing)
    juce::String origin;      // e.g. "FL Studio extra search"
    bool enabledByDefault = true;
    bool exists = false;
};

struct DawHit
{
    juce::String name;        // "FL Studio"
    juce::String version;     // "2024" (may be empty)
    juce::String installPath;
    juce::Array<DawFolder> folders;
};

// ---------- helpers ----------
inline juce::String normaliseDir(const juce::String& raw, bool* existed = nullptr)
{
    auto p = raw.trim().unquoted().trim();
    if (p.isEmpty()) return {};
#if JUCE_WINDOWS
    if (p.contains("%"))
    {
        DWORD need = ExpandEnvironmentStringsA(p.toRawUTF8(), nullptr, 0);
        if (need > 1) { juce::HeapBlock<char> b(need); ExpandEnvironmentStringsA(p.toRawUTF8(), b, need); p = juce::String(b); }
    }
#endif
    juce::File dir(p);
    bool ok = dir.isDirectory();
    if (existed != nullptr) *existed = ok;
    return ok ? dir.getFullPathName() : juce::String{};
}

inline void addUniqueFolder(juce::Array<DawFolder>& out, const juce::String& raw,
                            const juce::String& origin, bool onByDefault = true,
                            bool includeMissing = false)
{
    bool existed = false;
    auto n = normaliseDir(raw, &existed);
    if (n.isEmpty() && !(includeMissing && raw.trim().isNotEmpty())) return;
    juce::String key = n.isNotEmpty() ? n : raw.trim();
#if JUCE_WINDOWS
    key = key.toLowerCase();
#endif
    for (auto& f : out)
    {
#if JUCE_WINDOWS
        if (f.path.toLowerCase() == key) return;
#else
        if (f.path == key) return;
#endif
    }
    out.add({ n.isNotEmpty() ? n : raw.trim(), origin, onByDefault && existed, existed });
}

// OS-standard plugin locations — always proposed, no DAW needed.
inline juce::Array<DawFolder> standardFolders()
{
    juce::Array<DawFolder> out;
#if JUCE_WINDOWS
    addUniqueFolder(out, "C:\\Program Files\\Common Files\\VST3", "VST3 system", true);
    addUniqueFolder(out, "C:\\Program Files (x86)\\Common Files\\VST3", "VST3 32-bit", true);
    {
        // Per-user VST3 lives under %LOCALAPPDATA% (Roaming is wrong — JUCE brief).
        DWORD need = ExpandEnvironmentStringsA("%LOCALAPPDATA%\\Programs\\Common\\VST3", nullptr, 0);
        if (need > 1) { juce::HeapBlock<char> b(need); ExpandEnvironmentStringsA("%LOCALAPPDATA%\\Programs\\Common\\VST3", b, need); addUniqueFolder(out, juce::String(b), "VST3 per-user", true); }
    }
    if (const char* vst3Path = std::getenv("VST3_PATH"))
        for (auto p : juce::StringArray::fromTokens(vst3Path, ";", ""))
            addUniqueFolder(out, p, "VST3_PATH env", true);
    addUniqueFolder(out, "C:\\Program Files\\Common Files\\CLAP", "CLAP system", true);
    if (const char* clapPath = std::getenv("CLAP_PATH"))
        for (auto p : juce::StringArray::fromTokens(clapPath, ";", ""))
            addUniqueFolder(out, p, "CLAP_PATH env", true);
#elif JUCE_MAC
    addUniqueFolder(out, "/Library/Audio/Plug-Ins/VST3", "VST3 system", true);
    addUniqueFolder(out, "~/Library/Audio/Plug-Ins/VST3", "VST3 per-user", true);
    addUniqueFolder(out, "/Library/Audio/Plug-Ins/Components", "AU system", true);
    addUniqueFolder(out, "~/Library/Audio/Plug-Ins/Components", "AU per-user", true);
    addUniqueFolder(out, "/Library/Audio/Plug-Ins/CLAP", "CLAP system", true);
    addUniqueFolder(out, "~/Library/Audio/Plug-Ins/CLAP", "CLAP per-user", true);
#endif
    return out;
}

#if JUCE_WINDOWS
// ---------- Windows registry: installed programs ----------
struct UninstallEntry { juce::String name, version, location; };

inline juce::Array<UninstallEntry> readUninstallKeys()
{
    juce::Array<UninstallEntry> out;
    const char* hives[] = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };
    for (auto hive : hives)
    {
        HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
        juce::String sub = juce::String(hive).fromFirstOccurrenceOf("\\", false, false);
        HKEY h = nullptr;
        if (RegOpenKeyExA(roots[&hive - hives], sub.toRawUTF8(), 0, KEY_READ, &h) != ERROR_SUCCESS) continue;
        char subName[256]; DWORD sn;
        for (DWORD i = 0;; ++i)
        {
            sn = 256;
            if (RegEnumKeyExA(h, i, subName, &sn, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY hs = nullptr;
            if (RegOpenKeyExA(h, subName, 0, KEY_READ, &hs) != ERROR_SUCCESS) continue;
            auto get = [&](const char* v) {
                BYTE dd[1024]; DWORD dn = sizeof(dd), dt = REG_SZ;
                if (RegQueryValueExA(hs, v, nullptr, &dt, dd, &dn) == ERROR_SUCCESS && (dt == REG_SZ || dt == REG_EXPAND_SZ))
                    return juce::String((char*)dd, dn).trim();
                return juce::String{};
            };
            auto name = get("DisplayName");
            if (name.isNotEmpty()) out.add({ name, get("DisplayVersion"), get("InstallLocation") });
            RegCloseKey(hs);
        }
        RegCloseKey(h);
    }
    return out;
}

inline void readImageLinePaths(juce::Array<DawFolder>& out)
{
    for (auto hiveRoot : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
    {
        HKEY h = nullptr;
        if (RegOpenKeyExA(hiveRoot, "Software\\Image-Line\\Shared\\Plugin search paths",
                          0, KEY_READ, &h) != ERROR_SUCCESS) continue;
        char valName[256]; DWORD vn; DWORD type; BYTE data[4096]; DWORD dn;
        for (DWORD i = 0;; ++i)
        {
            vn = 256; dn = sizeof(data);
            if (RegEnumValueA(h, i, valName, &vn, nullptr, &type, data, &dn) != ERROR_SUCCESS) break;
            if (type == REG_SZ || type == REG_EXPAND_SZ)
                addUniqueFolder(out, juce::String((char*)data, dn), "FL Studio extra search");
        }
        char subName[256]; DWORD sn;
        for (DWORD i = 0;; ++i)
        {
            sn = 256;
            if (RegEnumKeyExA(h, i, subName, &sn, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY hs = nullptr;
            if (RegOpenKeyExA(h, subName, 0, KEY_READ, &hs) == ERROR_SUCCESS)
            {
                BYTE dd[4096]; DWORD dn2 = sizeof(dd), dt = REG_SZ;
                if (RegQueryValueExA(hs, nullptr, nullptr, &dt, dd, &dn2) == ERROR_SUCCESS
                    && (dt == REG_SZ || dt == REG_EXPAND_SZ))
                    addUniqueFolder(out, juce::String((char*)dd, dn2), "FL Studio extra search");
                RegCloseKey(hs);
            }
        }
        RegCloseKey(h);
    }
    auto regPath = juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\VST\\VSTPluginsPath");
    if (regPath.isEmpty())
        regPath = juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\VST\\VSTPluginsPath");
    addUniqueFolder(out, regPath, "VST installers default (registry)");
}

inline void readReaperPaths(juce::Array<DawFolder>& out)
{
    juce::File ini(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("REAPER").getChildFile("reaper.ini"));
    if (!ini.existsAsFile()) return; // portable installs keep it next to the exe
    for (auto line : juce::StringArray::fromLines(ini.loadFileAsString()))
        for (auto key : { juce::String("vstpath64"), juce::String("vstpath") })
            if (line.upToFirstOccurrenceOf("=", false, false).trim().equalsIgnoreCase(key))
                for (auto p : juce::StringArray::fromTokens(line.fromFirstOccurrenceOf("=", false, false), ";", ""))
                    addUniqueFolder(out, p, "REAPER VST path");
}

// Ableton: PluginScanner.txt is plain text ("... scanning plugins in \"<path>\" (user)").
// Prefer it over the binary Preferences.cfg.
inline void readAbletonPaths(juce::Array<DawFolder>& out)
{
    juce::File prefsRoot(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("Ableton"));
    if (!prefsRoot.isDirectory()) return;
    juce::Array<juce::File> liveDirs;
    prefsRoot.findChildFiles(liveDirs, juce::File::findDirectories, false, "Live *");
    // newest version dir first is nice-to-have; order from the FS is fine.
    for (auto d : liveDirs)
    {
        juce::File log = d.getChildFile("Preferences").getChildFile("PluginScanner.txt");
        if (!log.existsAsFile()) continue;
        for (auto line : juce::StringArray::fromLines(log.loadFileAsString()))
        {
            auto q1 = line.indexOfChar('"');
            auto q2 = line.lastIndexOfChar('"');
            if (q1 >= 0 && q2 > q1)
                addUniqueFolder(out, line.substring(q1 + 1, q2),
                                line.contains("(user)") ? "Ableton custom folder"
                                                       : "Ableton scanned folder",
                                line.contains("(user)"));
        }
        if (!out.isEmpty()) return; // newest log with data wins
    }
}

// Cubase: Vst2xPlugin SearchPaths XML (exact filename varies — glob it).
inline void readCubasePaths(juce::Array<DawFolder>& out)
{
    juce::File steinberg(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("Steinberg"));
    if (!steinberg.isDirectory()) return;
    juce::Array<juce::File> prefDirs;
    steinberg.findChildFiles(prefDirs, juce::File::findDirectories, false, "Cubase *_64");
    for (auto d : prefDirs)
    {
        juce::Array<juce::File> xmls;
        d.findChildFiles(xmls, juce::File::findFiles, false, "Vst2xPlugin SearchPaths*.xml");
        for (auto x : xmls)
        {
            // generously extract anything that looks like a directory (validation inside)
            for (auto tok : juce::StringArray::fromTokens(x.loadFileAsString(), "\"'<>", ""))
                addUniqueFolder(out, tok, "Cubase VST2 search path");
            if (!xmls.isEmpty()) return;
        }
    }
}
#endif // JUCE_WINDOWS

// ---------- main entry: every DAW we can find + its folders ----------
inline juce::Array<DawHit> detectInstalledDaws()
{
    juce::Array<DawHit> hits;
#if JUCE_WINDOWS
    struct Known { const char* match; const char* name; };
    const Known known[] = {
        { "FL Studio", "FL Studio" }, { "Ableton Live", "Ableton Live" },
        { "REAPER", "REAPER" }, { "Cubase", "Cubase" }, { "Nuendo", "Nuendo" },
        { "Studio One", "Studio One" }, { "Bitwig", "Bitwig Studio" }, { "Pro Tools", "Pro Tools" },
    };
    auto entries = readUninstallKeys();
    for (auto k : known)
    {
        DawHit hit; hit.name = k.name;
        juce::String kname = k.name;
        for (auto& e : entries)
            if (e.name.containsIgnoreCase(k.match)) { hit.version = e.version; hit.installPath = e.location; break; }
        // default-path existence check even without uninstall entry
        bool installed = hit.installPath.isNotEmpty();
        if (kname == "FL Studio")
        {
            juce::Array<DawFolder> f; readImageLinePaths(f); hit.folders = f;
            installed = installed || juce::File("C:\\Program Files\\Image-Line").isDirectory();
        }
        else if (kname == "Ableton Live")
        {
            juce::Array<DawFolder> f; readAbletonPaths(f); hit.folders = f;
            installed = installed || juce::File("C:\\ProgramData\\Ableton").isDirectory();
        }
        else if (kname == "REAPER")
        {
            juce::Array<DawFolder> f; readReaperPaths(f); hit.folders = f;
            installed = installed || juce::File("C:\\Program Files\\REAPER (x64)\\reaper.exe").existsAsFile();
        }
        else if (kname == "Cubase" || kname == "Nuendo")
        {
            juce::Array<DawFolder> f; readCubasePaths(f); hit.folders = f;
            installed = installed || juce::File("C:\\Program Files\\Steinberg").isDirectory();
        }
        else if (kname == "Studio One")
        {
            installed = installed || juce::File("C:\\Program Files\\PreSonus").isDirectory();
            if (installed) // settings filename unconfirmed — propose documented default if it exists
                addUniqueFolder(hit.folders, "C:\\Program Files\\Vstplugins", "Studio One documented default");
        }
        else if (kname == "Bitwig Studio")
        {
            installed = installed || juce::File("C:\\Program Files\\Bitwig Studio\\Bitwig Studio.exe").existsAsFile();
        }
        if (installed) hits.add(hit);
    }
#elif JUCE_MAC
    struct App { const char* file; const char* name; };
    const App apps[] = {
        { "/Applications/FL Studio.app", "FL Studio" },
        { "/Applications/Ableton Live 12 Suite.app", "Ableton Live" },
        { "/Applications/REAPER64.app", "REAPER" },
        { "/Applications/Bitwig Studio.app", "Bitwig Studio" },
        { "/Applications/Logic Pro.app", "Logic Pro" },
        { "/Applications/Pro Tools.app", "Pro Tools" },
    };
    for (auto a : apps)
        if (juce::File(a.file).isDirectory()) { DawHit h; h.name = a.name; h.installPath = a.file; hits.add(h); }
    // REAPER mac prefs (case-sensitive path, best effort)
    juce::File rini(juce::File("~/Library/Application Support/REAPER/reaper.ini"));
    if (rini.existsAsFile())
        for (auto line : juce::StringArray::fromLines(rini.loadFileAsString()))
            for (auto key : { juce::String("vstpath"), juce::String("vstpath64") })
                if (line.upToFirstOccurrenceOf("=", false, false).trim().equalsIgnoreCase(key))
                    for (auto p : juce::StringArray::fromTokens(line.fromFirstOccurrenceOf("=", false, false), ";", ":"))
                        if (!hits.isEmpty()) addUniqueFolder(hits.getReference(hits.size() - 1).folders, p, "REAPER VST path");
#endif
    return hits;
}

} // namespace daws
