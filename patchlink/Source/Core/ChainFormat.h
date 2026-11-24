#pragma once
#include <JuceHeader.h>

// PatchLink chain format v2 — forward-compatible, IDs-not-paths.
// v1 (.patchlink) loads via migration: unknown fields ignored, missing defaulted.
namespace chainformat {
static constexpr int kFormatVersion = 2;

// Build one node entry from a live plugin instance + its state blob.
inline juce::var makeNodeVar(const juce::PluginDescription& d,
                             const juce::String& stateBase64,
                             int uid, int x, int y, bool bypass,
                             double wet = 1.0, double gainDb = 0.0)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("uid", uid);
    o->setProperty("x", x);
    o->setProperty("y", y);
    o->setProperty("bypass", bypass);
    o->setProperty("wet", wet);
    o->setProperty("gainDb", gainDb);
    // Multi-ID: every identifier we know, so recall resolves by ID never by path.
    auto* ids = new juce::DynamicObject();
    ids->setProperty("name", d.name);
    ids->setProperty("manufacturer", d.manufacturerName);
    ids->setProperty("version", d.version);
    ids->setProperty("format", d.pluginFormatName);
    ids->setProperty("fileOrId", d.fileOrIdentifier);
    ids->setProperty("identifier", d.createIdentifierString());
    if (d.uniqueId != 0)
        ids->setProperty("uniqueId", d.uniqueId);
    o->setProperty("ids", juce::var(ids));
    o->setProperty("stateBase64", stateBase64);
    return juce::var(o);
}

// Portability: green = travels anywhere, yellow = caveats, red = won't recall.
enum class Portability { Green, Yellow, Red };
struct ManifestEntry { juce::String name, version, format; size_t stateBytes = 0; Portability p = Portability::Green; juce::String note; };

inline Portability scoreNode(const juce::String& format, const juce::String& stateB64, juce::String& note)
{
    if (format.compareIgnoreCase("VST3") == 0 || format.compareIgnoreCase("CLAP") == 0) {
        if (stateB64.isEmpty()) { note = "no state saved"; return Portability::Yellow; }
        return Portability::Green;
    }
    if (format.compareIgnoreCase("AudioUnit") == 0) { note = "AU won't recall on Windows"; return Portability::Yellow; }
    note = "format may not travel cross-platform";
    return Portability::Yellow;
}

inline juce::StringArray manifestLines(const juce::var& chainRoot)
{
    juce::StringArray out;
    auto* rootObj = chainRoot.getDynamicObject();
    if (rootObj == nullptr)
        return out;
    if (auto* arr = rootObj->getProperty("nodes").getArray())
        for (auto& n : *arr)
            if (auto* o = n.getDynamicObject())
                if (auto* ids = o->getProperty("ids").getDynamicObject())
                {
                    juce::String note; juce::String fmt = ids->getProperty("format").toString();
                    auto p = scoreNode(fmt, o->getProperty("stateBase64").toString(), note);
                    juce::String icon = p == Portability::Green ? "[ok] " : "[!!] ";
                    out.add(icon + ids->getProperty("name").toString()
                            + " " + ids->getProperty("version").toString()
                            + " (" + fmt + ")" + (note.isNotEmpty() ? " - " + note : ""));
                }
    return out;
}

inline int formatVersionOf(const juce::var& root)
{
    auto* rootObj = root.getDynamicObject();
    if (rootObj == nullptr) return -1;
    auto v = rootObj->getProperty("chainFormat");
    if (v.isVoid()) return 1; // v1 files have "version" instead
    return (int)v;
}
} // namespace chainformat
