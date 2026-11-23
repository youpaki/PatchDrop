#pragma once

#include <JuceHeader.h>
#include "FlStudioFstParser.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace dawexport
{
enum class TargetFormat
{
    autoDetect = 0,
    flStudioFst,
    reaperRfxChain,
    vstPreset,
    patchlinkFile
};

struct HostDetection
{
    bool isStandalone = false;
    TargetFormat detectedFormat = TargetFormat::autoDetect;
    juce::String hostName;
};

inline juce::String formatLabel(TargetFormat fmt)
{
    switch (fmt)
    {
        case TargetFormat::flStudioFst:    return "FL Studio (.fst)";
        case TargetFormat::reaperRfxChain: return "REAPER (.RfxChain)";
        case TargetFormat::vstPreset:      return "VST3 Preset (.vstpreset)";
        case TargetFormat::patchlinkFile:  return "PatchLink (.patchlink)";
        case TargetFormat::autoDetect:     return "Choose DAW format...";
    }
    return "DAW Preset";
}

inline juce::String formatExtension(TargetFormat fmt)
{
    switch (fmt)
    {
        case TargetFormat::flStudioFst:    return "fst";
        case TargetFormat::reaperRfxChain: return "RfxChain";
        case TargetFormat::vstPreset:      return "vstpreset";
        case TargetFormat::patchlinkFile:  return "patchlink";
        case TargetFormat::autoDetect:     return "fst";
    }
    return "fst";
}

inline HostDetection detectHostDaw(juce::AudioProcessor::WrapperType wrapperType)
{
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        return { true, TargetFormat::autoDetect, "Standalone" };

    const juce::PluginHostType host;
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getFileNameWithoutExtension()
                         .toLowerCase();

    if (host.isFruityLoops() || exe == "fl" || exe == "fl64" || exe.startsWith("fl studio"))
        return { false, TargetFormat::flStudioFst, "FL Studio" };

    if (host.isReaper() || exe.contains("reaper"))
        return { false, TargetFormat::reaperRfxChain, "REAPER" };

    juce::String desc(host.getHostDescription());
    if (desc.isEmpty() || desc.equalsIgnoreCase("Unknown"))
        desc = exe.isNotEmpty() ? exe : juce::String("DAW");
    return { false, TargetFormat::vstPreset, desc };
}

namespace detail
{
struct FlVst3Record
{
    std::array<uint8_t, 16> tuid {};
    int juceUniqueId = 0;
    juce::String flPluginName;
    juce::String vendor;
    juce::String filePath;
    juce::String normName;
};

inline juce::String normalizeKey(const juce::String& text)
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

inline bool hexTo16Bytes(const juce::String& hex, std::array<uint8_t, 16>& out)
{
    const auto clean = hex.retainCharacters("0123456789abcdefABCDEF");
    if (clean.length() != 32)
        return false;
    for (int i = 0; i < 16; ++i)
    {
        out[(size_t)i] = static_cast<uint8_t>(clean.substring(i * 2, i * 2 + 2).getHexValue32());
    }
    return true;
}

inline int parseUniqueIdFromIdentifier(const juce::String& ident)
{
    const int dash = ident.lastIndexOfChar('-');
    if (dash < 0)
        return 0;
    const auto tail = ident.substring(dash + 1).trim();
    if (tail.isEmpty())
        return 0;
    return static_cast<int>(static_cast<uint32_t>(tail.getHexValue32()));
}

inline const std::vector<FlVst3Record>& getFlVst3Cache()
{
    static std::vector<FlVst3Record> cache;
    static bool built = false;
    if (built)
        return cache;
    built = true;

    std::vector<juce::File> searchDirs;
    const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    searchDirs.push_back(docs.getChildFile("Image-Line/FL Studio/Presets/Plugin database/Installed/Effects/VST3"));
    searchDirs.push_back(home.getChildFile("OneDrive/Documents/Image-Line/FL Studio/Presets/Plugin database/Installed/Effects/VST3"));

    for (const auto& dir : searchDirs)
    {
        if (!dir.isDirectory())
            continue;
        for (const auto& file : dir.findChildFiles(juce::File::findFiles, false, "*.fst"))
        {
            const auto parsed = flstudio::parseFstFile(file);
            if (!parsed.valid || parsed.plugins.empty())
                continue;
            const auto& e = parsed.plugins.front();
            FlVst3Record rec;
            if (!hexTo16Bytes(e.tuidHex, rec.tuid))
                continue;
            rec.juceUniqueId = e.juceUniqueId;
            rec.flPluginName = e.pluginName.isNotEmpty() ? e.pluginName : file.getFileNameWithoutExtension();
            rec.vendor = e.vendor;
            rec.filePath = e.filePath;
            rec.normName = normalizeKey(rec.flPluginName);
            cache.push_back(std::move(rec));
        }
        if (!cache.empty())
            break;
    }
    return cache;
}

struct NodeInfo
{
    juce::String name;
    juce::String flPluginName;
    juce::String manufacturer;
    juce::String format;
    juce::String fileOrId;
    juce::String identifier;
    juce::String stateBase64;
    int uniqueId = 0;
    bool bypass = false;
    bool hasTuid = false;
    std::array<uint8_t, 16> tuid {};
};

inline NodeInfo extractNodeInfo(const juce::var& nodeVar)
{
    NodeInfo info;
    auto* no = nodeVar.getDynamicObject();
    if (no == nullptr)
        return info;

    info.stateBase64 = no->getProperty("stateBase64").toString();
    info.bypass = static_cast<bool>(no->getProperty("bypass"));

    if (auto* ids = no->getProperty("ids").getDynamicObject())
    {
        info.name = ids->getProperty("name").toString();
        info.flPluginName = ids->getProperty("flPluginName").toString();
        info.manufacturer = ids->getProperty("manufacturer").toString();
        info.format = ids->getProperty("format").toString();
        info.fileOrId = ids->getProperty("fileOrId").toString();
        info.identifier = ids->getProperty("identifier").toString();
        const auto uidVar = ids->getProperty("uniqueId");
        if (!uidVar.isVoid())
            info.uniqueId = static_cast<int>(uidVar);
        const auto tuidHex = ids->getProperty("tuidHex").toString();
        if (hexTo16Bytes(tuidHex, info.tuid))
            info.hasTuid = true;
    }
    else
    {
        info.name = no->getProperty("name").toString();
        info.manufacturer = no->getProperty("manufacturer").toString();
        info.format = no->getProperty("format").toString();
        info.fileOrId = no->getProperty("fileOrId").toString();
    }

    if (info.uniqueId == 0 && info.identifier.isNotEmpty())
        info.uniqueId = parseUniqueIdFromIdentifier(info.identifier);

    // Enrich missing FL TUID / pluginName from local FL Studio VST3 database if available.
    const auto& cache = getFlVst3Cache();
    const FlVst3Record* matched = nullptr;
    if (info.uniqueId != 0)
    {
        for (const auto& rec : cache)
            if (rec.juceUniqueId == info.uniqueId) { matched = &rec; break; }
    }
    if (matched == nullptr && info.name.isNotEmpty())
    {
        const auto n = normalizeKey(info.name);
        const auto leaf = info.fileOrId.isNotEmpty()
                              ? normalizeKey(juce::File(info.fileOrId).getFileNameWithoutExtension())
                              : juce::String();
        for (const auto& rec : cache)
        {
            if (rec.normName == n || (leaf.isNotEmpty() && rec.normName == leaf))
            {
                matched = &rec;
                break;
            }
        }
    }

    if (matched != nullptr)
    {
        if (!info.hasTuid)
        {
            info.tuid = matched->tuid;
            info.hasTuid = true;
        }
        if (info.flPluginName.isEmpty())
            info.flPluginName = matched->flPluginName;
        if (info.manufacturer.isEmpty())
            info.manufacturer = matched->vendor;
        if (info.fileOrId.isEmpty())
            info.fileOrId = matched->filePath;
    }

    if (info.flPluginName.isEmpty())
        info.flPluginName = info.name;
    if (info.fileOrId.isEmpty() && info.name.isNotEmpty())
        info.fileOrId = "C:\\Program Files\\Common Files\\VST3\\" + info.name + ".vst3";

    return info;
}

inline std::vector<NodeInfo> collectExportableVst3Nodes(const juce::var& patchVar)
{
    std::vector<NodeInfo> out;
    auto* root = patchVar.getDynamicObject();
    if (root == nullptr)
        return out;
    auto* arr = root->getProperty("nodes").getArray();
    if (arr == nullptr)
        return out;

    struct Indexed { int order; int seq; NodeInfo info; };
    std::vector<Indexed> tmp;
    int seq = 0;
    for (const auto& n : *arr)
    {
        auto* no = n.getDynamicObject();
        if (no == nullptr) continue;
        auto info = extractNodeInfo(n);
        if (info.format.isNotEmpty() && !info.format.equalsIgnoreCase("VST3"))
            continue;
        if (info.name.isEmpty() && info.fileOrId.isEmpty())
            continue;
        const auto ordVar = no->getProperty("order");
        const int order = ordVar.isVoid() ? seq : static_cast<int>(ordVar);
        tmp.push_back({ order, seq, std::move(info) });
        ++seq;
    }
    std::stable_sort(tmp.begin(), tmp.end(), [](const Indexed& a, const Indexed& b) {
        return a.order == b.order ? a.seq < b.seq : a.order < b.order;
    });
    for (auto& item : tmp)
        out.push_back(std::move(item.info));
    return out;
}

inline void writeVarInt(juce::MemoryOutputStream& mos, size_t v)
{
    do
    {
        uint8_t b = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7;
        if (v != 0)
            b |= 0x80u;
        mos.writeByte(static_cast<char>(b));
    } while (v != 0);
}

inline void writeFlVarEvent(juce::MemoryOutputStream& mos, uint8_t ev, const void* data, size_t len)
{
    mos.writeByte(static_cast<char>(ev));
    writeVarInt(mos, len);
    if (len > 0 && data != nullptr)
        mos.write(data, len);
}

inline void writeFlUtf16Event(juce::MemoryOutputStream& mos, uint8_t ev, const juce::String& text)
{
    const auto* utf16 = text.toUTF16().getAddress();
    size_t units = 0;
    if (utf16 != nullptr)
        while (utf16[units] != 0)
            ++units;

    juce::MemoryBlock mb;
    juce::MemoryOutputStream ts(mb, false);
    for (size_t i = 0; i < units; ++i)
        ts.writeShort(static_cast<short>(utf16[i]));
    ts.writeShort(0);
    ts.flush();
    writeFlVarEvent(mos, ev, mb.getData(), mb.getSize());
}

inline void appendWrapperSubChunk(juce::MemoryOutputStream& mos, uint32_t subId, const void* data, size_t len)
{
    mos.writeInt(static_cast<int>(subId));
    mos.writeInt64(static_cast<juce::int64>(len));
    if (len > 0 && data != nullptr)
        mos.write(data, len);
}

inline juce::MemoryBlock buildFruityWrapperPayload(const NodeInfo& info, bool mixerSlotStyle)
{
    juce::MemoryBlock sub;
    juce::MemoryOutputStream ws(sub, false);

    ws.writeInt(10); // Fruity Wrapper chunk version 10 (compatible with FL 20+ and FL 2026)

    if (mixerSlotStyle)
    {
        static const uint8_t s1[20] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        appendWrapperSubChunk(ws, 1, s1, sizeof(s1));
    }

    static const uint8_t s2[25] = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x08, 0x7d, 0x50, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    appendWrapperSubChunk(ws, 2, s2, sizeof(s2));

    if (mixerSlotStyle)
    {
        static const uint8_t s30[16] = {
            0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static const uint8_t s31[24] = {
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static const uint8_t s32[12] = {
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        appendWrapperSubChunk(ws, 30, s30, sizeof(s30));
        appendWrapperSubChunk(ws, 31, s31, sizeof(s31));
        appendWrapperSubChunk(ws, 32, s32, sizeof(s32));
    }

    static const uint8_t s50[16] = {
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    appendWrapperSubChunk(ws, 50, s50, sizeof(s50));
    appendWrapperSubChunk(ws, 52, info.tuid.data(), info.tuid.size());

    if (info.flPluginName.isNotEmpty())
        appendWrapperSubChunk(ws, 54, info.flPluginName.toRawUTF8(), info.flPluginName.getNumBytesAsUTF8());
    if (info.fileOrId.isNotEmpty())
        appendWrapperSubChunk(ws, 55, info.fileOrId.toRawUTF8(), info.fileOrId.getNumBytesAsUTF8());
    if (info.manufacturer.isNotEmpty())
        appendWrapperSubChunk(ws, 56, info.manufacturer.toRawUTF8(), info.manufacturer.getNumBytesAsUTF8());

    const auto sub53 = flstudio::buildVst3WrapperSub53(info.stateBase64);
    if (sub53.getSize() > 0)
        appendWrapperSubChunk(ws, 53, sub53.getData(), sub53.getSize());

    ws.flush();
    return sub;
}

inline juce::MemoryBlock finalizeFlContainer(uint16_t hdType, uint16_t hdTracks, uint16_t hdPpq,
                                             const juce::MemoryBlock& dtBody)
{
    juce::MemoryBlock out;
    juce::MemoryOutputStream mos(out, false);
    mos.write("FLhd", 4);
    mos.writeInt(6);
    mos.writeShort(static_cast<short>(hdType));
    mos.writeShort(static_cast<short>(hdTracks));
    mos.writeShort(static_cast<short>(hdPpq));
    mos.write("FLdt", 4);
    mos.writeInt(static_cast<int>(dtBody.getSize()));
    if (dtBody.getSize() > 0)
        mos.write(dtBody.getData(), dtBody.getSize());
    mos.flush();
    return out;
}

inline juce::String steinbergFuidString(const std::array<uint8_t, 16>& raw16)
{
#if JUCE_WINDOWS
    const uint32_t d1 = juce::ByteOrder::littleEndianInt(raw16.data());
    const uint16_t d2 = juce::ByteOrder::littleEndianShort(raw16.data() + 4);
    const uint16_t d3 = juce::ByteOrder::littleEndianShort(raw16.data() + 6);
    juce::String s = juce::String::formatted("%08X%04X%04X", d1, d2, d3);
    for (int i = 8; i < 16; ++i)
        s << juce::String::formatted("%02X", raw16[(size_t)i]);
    return s;
#else
    juce::String s;
    for (int i = 0; i < 16; ++i)
        s << juce::String::formatted("%02X", raw16[(size_t)i]);
    return s;
#endif
}

inline juce::String sanitizeFileBase(juce::String s, const juce::String& fallback)
{
    s = juce::File::createLegalFileName(s).trim();
    return s.isNotEmpty() ? s : fallback;
}
} // namespace detail

inline bool buildFlStudioSingleFst(const juce::var& nodeVar, juce::MemoryBlock& out, juce::String& err)
{
    const auto info = detail::extractNodeInfo(nodeVar);
    if (info.name.isEmpty() && info.fileOrId.isEmpty())
    {
        err = "Missing plugin metadata.";
        return false;
    }
    if (info.format.isNotEmpty() && !info.format.equalsIgnoreCase("VST3"))
    {
        err = info.name + " is not a VST3 plugin.";
        return false;
    }

    juce::MemoryBlock dt;
    juce::MemoryOutputStream mos(dt, false);

    static constexpr char kFlVer[] = "20.8.4.2576";
    detail::writeFlVarEvent(mos, 199, kFlVer, sizeof(kFlVer)); // includes null terminator
    mos.writeByte(28);
    mos.writeByte(3);

    detail::writeFlUtf16Event(mos, 201, "Fruity Wrapper");
    uint32_t d4[13] {};
    d4[2] = info.bypass ? 0u : 2u;
    detail::writeFlVarEvent(mos, 212, d4, sizeof(d4));
    detail::writeFlUtf16Event(mos, 203, info.name.isNotEmpty() ? info.name : info.flPluginName);

    const auto wrapperChunk = detail::buildFruityWrapperPayload(info, false);
    detail::writeFlVarEvent(mos, 213, wrapperChunk.getData(), wrapperChunk.getSize());
    mos.flush();

    out = detail::finalizeFlContainer(0x0030u, 0x0002u, 0x0030u, dt);
    return true;
}

inline bool buildFlStudioMixerTrackFst(const juce::var& patchVar, juce::MemoryBlock& out, juce::String& err)
{
    const auto nodes = detail::collectExportableVst3Nodes(patchVar);
    if (nodes.empty())
    {
        err = "No VST3 plugins in this chain to export as FL Studio .fst.";
        return false;
    }

    juce::String trackName = "PatchLink";
    if (auto* root = patchVar.getDynamicObject())
    {
        const auto n = root->getProperty("name").toString().trim();
        if (n.isNotEmpty())
            trackName = n;
    }

    juce::MemoryBlock dt;
    juce::MemoryOutputStream mos(dt, false);

    static constexpr char kFlVer[] = "20.8.4.2576";
    detail::writeFlVarEvent(mos, 199, kFlVer, sizeof(kFlVer));
    mos.writeByte(static_cast<char>(159)); mos.writeInt(0x0a10);
    mos.writeByte(28); mos.writeByte(1);
    mos.writeByte(37); mos.writeByte(1);
    mos.writeByte(static_cast<char>(149)); mos.writeInt(0x00c04184);
    detail::writeFlUtf16Event(mos, 204, trackName);

    static const uint8_t ev236[12] = { 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    detail::writeFlVarEvent(mos, 236, ev236, sizeof(ev236));

    const size_t slotCount = juce::jmin<size_t>(10, nodes.size());
    for (size_t slot = 0; slot < 10; ++slot)
    {
        if (slot < slotCount)
        {
            const auto& info = nodes[slot];
            detail::writeFlUtf16Event(mos, 201, "Fruity Wrapper");

            uint32_t d4[13] {};
            d4[0] = 11u;
            d4[1] = static_cast<uint32_t>(slot);
            d4[2] = info.bypass ? 0u : 2u;
            d4[4] = 320u;
            detail::writeFlVarEvent(mos, 212, d4, sizeof(d4));

            detail::writeFlUtf16Event(mos, 203, info.name.isNotEmpty() ? info.name : info.flPluginName);
            mos.writeByte(static_cast<char>(155)); mos.writeInt(0);
            mos.writeByte(static_cast<char>(128)); mos.writeInt(0x00565148);

            const auto wrapperChunk = detail::buildFruityWrapperPayload(info, true);
            detail::writeFlVarEvent(mos, 213, wrapperChunk.getData(), wrapperChunk.getSize());
        }
        mos.writeByte(98);
        mos.writeShort(static_cast<short>(slot));
    }

    mos.flush();
    out = detail::finalizeFlContainer(0x0040u, 0x0006u, 0x0060u, dt);
    return true;
}

inline bool buildSteinbergVstPreset(const juce::var& nodeVar, juce::MemoryBlock& out, juce::String& err)
{
    const auto info = detail::extractNodeInfo(nodeVar);
    if (info.format.isNotEmpty() && !info.format.equalsIgnoreCase("VST3"))
    {
        err = info.name + " is not a VST3 plugin.";
        return false;
    }

    juce::MemoryBlock comp, cont;
    flstudio::extractRawVst3ChunksFromBase64(info.stateBase64, comp, cont);
    if (comp.getSize() == 0 && cont.getSize() == 0)
    {
        err = info.name + " has no VST3 state data.";
        return false;
    }

    const auto fuidStr = detail::steinbergFuidString(info.tuid);
    juce::MemoryOutputStream mos(out, false);
    mos.write("VST3", 4);
    mos.writeInt(1); // format version 1
    mos.write(fuidStr.toRawUTF8(), 32);

    const juce::int64 compOffset = 48;
    const juce::int64 compSize = static_cast<juce::int64>(comp.getSize());
    const juce::int64 contOffset = compOffset + compSize;
    const juce::int64 contSize = static_cast<juce::int64>(cont.getSize());
    const juce::int64 listOffset = contOffset + contSize;

    mos.writeInt64(listOffset);
    if (compSize > 0)
        mos.write(comp.getData(), comp.getSize());
    if (contSize > 0)
        mos.write(cont.getData(), cont.getSize());

    mos.write("List", 4);
    const int entryCount = (compSize > 0 ? 1 : 0) + (contSize > 0 ? 1 : 0);
    mos.writeInt(entryCount);
    if (compSize > 0)
    {
        mos.write("Comp", 4);
        mos.writeInt64(compOffset);
        mos.writeInt64(compSize);
    }
    if (contSize > 0)
    {
        mos.write("Cont", 4);
        mos.writeInt64(contOffset);
        mos.writeInt64(contSize);
    }
    mos.flush();
    return true;
}

inline bool buildReaperRfxChain(const juce::var& patchVar, juce::String& outText, juce::String& err)
{
    const auto nodes = detail::collectExportableVst3Nodes(patchVar);
    if (nodes.empty())
    {
        err = "No VST3 plugins in this chain to export as REAPER .RfxChain.";
        return false;
    }

    juce::String lines;
    for (const auto& info : nodes)
    {
        lines << "BYPASS " << (info.bypass ? "1" : "0") << " 0 0\n";
        const auto leaf = juce::File(info.fileOrId).getFileName();
        const auto fuid = detail::steinbergFuidString(info.tuid);
        const auto mfr = info.manufacturer.isNotEmpty() ? " (" + info.manufacturer + ")" : juce::String();
        lines << "<VST \"VST3: " << info.name << mfr << "\" \"" << (leaf.isNotEmpty() ? leaf : info.name + ".vst3")
              << "\" 0 \"\" 0{" << fuid << "} \"\"\n";

        juce::MemoryBlock comp, cont;
        flstudio::extractRawVst3ChunksFromBase64(info.stateBase64, comp, cont);
        if (comp.getSize() > 0 || cont.getSize() > 0)
        {
            juce::MemoryBlock blob;
            juce::MemoryOutputStream mos(blob, false);
            mos.writeInt(static_cast<int>(comp.getSize()));
            mos.writeInt(1);
            if (comp.getSize() > 0)
                mos.write(comp.getData(), comp.getSize());
            if (cont.getSize() > 0)
            {
                mos.writeInt(static_cast<int>(cont.getSize()));
                mos.write(cont.getData(), cont.getSize());
            }
            mos.flush();
            const auto b64 = blob.toBase64Encoding();
            for (int pos = 0; pos < b64.length(); pos += 128)
                lines << "  " << b64.substring(pos, pos + 128) << "\n";
        }
        lines << ">\nPRESETNAME \"\"\nFLOATPOS 0 0 0 0\nWAK 0 0\n";
    }
    outText = lines;
    return true;
}

inline juce::StringArray prepareDragFiles(const juce::var& patchVar,
                                          TargetFormat fmt,
                                          juce::String& err,
                                          int singleNodeIndex = -1)
{
    juce::StringArray outFiles;
    auto* root = patchVar.getDynamicObject();
    if (root == nullptr)
    {
        err = "No chain data to export.";
        return {};
    }
    auto* arr = root->getProperty("nodes").getArray();
    if (arr == nullptr || arr->isEmpty())
    {
        err = "Chain has no plugins.";
        return {};
    }

    const auto dragDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("PatchLink_Drag");
    dragDir.createDirectory();
    for (const auto& old : dragDir.findChildFiles(juce::File::findFilesAndDirectories, false))
        old.deleteRecursively();

    const auto baseTitle = detail::sanitizeFileBase(root->getProperty("name").toString(), "PatchLink_Chain");
    const auto effectiveFmt = fmt == TargetFormat::autoDetect ? TargetFormat::flStudioFst : fmt;

    if (singleNodeIndex >= 0 && singleNodeIndex < arr->size())
    {
        const auto& nodeVar = arr->getReference(singleNodeIndex);
        const auto info = detail::extractNodeInfo(nodeVar);
        const auto fileTitle = detail::sanitizeFileBase(info.name, "Plugin");

        if (effectiveFmt == TargetFormat::flStudioFst)
        {
            juce::MemoryBlock fst;
            if (!buildFlStudioSingleFst(nodeVar, fst, err)) return {};
            const auto target = dragDir.getChildFile(fileTitle + ".fst");
            if (!target.replaceWithData(fst.getData(), fst.getSize())) { err = "Could not write temp .fst"; return {}; }
            outFiles.add(target.getFullPathName());
            return outFiles;
        }
        if (effectiveFmt == TargetFormat::vstPreset)
        {
            juce::MemoryBlock vp;
            if (!buildSteinbergVstPreset(nodeVar, vp, err)) return {};
            const auto target = dragDir.getChildFile(fileTitle + ".vstpreset");
            if (!target.replaceWithData(vp.getData(), vp.getSize())) { err = "Could not write temp .vstpreset"; return {}; }
            outFiles.add(target.getFullPathName());
            return outFiles;
        }
    }

    if (effectiveFmt == TargetFormat::patchlinkFile)
    {
        const auto target = dragDir.getChildFile(baseTitle + ".patchlink");
        if (!target.replaceWithText(juce::JSON::toString(patchVar, true)))
        {
            err = "Could not write temp .patchlink";
            return {};
        }
        outFiles.add(target.getFullPathName());
        return outFiles;
    }

    if (effectiveFmt == TargetFormat::flStudioFst)
    {
        juce::MemoryBlock fst;
        if (arr->size() == 1 && singleNodeIndex == 0)
        {
            if (!buildFlStudioSingleFst(arr->getReference(0), fst, err)) return {};
        }
        else if (arr->size() == 1)
        {
            if (!buildFlStudioSingleFst(arr->getReference(0), fst, err)) return {};
        }
        else
        {
            if (!buildFlStudioMixerTrackFst(patchVar, fst, err)) return {};
        }
        const auto target = dragDir.getChildFile(baseTitle + ".fst");
        if (!target.replaceWithData(fst.getData(), fst.getSize()))
        {
            err = "Could not write temp .fst";
            return {};
        }
        outFiles.add(target.getFullPathName());
        return outFiles;
    }

    if (effectiveFmt == TargetFormat::reaperRfxChain)
    {
        juce::String rfx;
        if (!buildReaperRfxChain(patchVar, rfx, err)) return {};
        const auto target = dragDir.getChildFile(baseTitle + ".RfxChain");
        if (!target.replaceWithText(rfx))
        {
            err = "Could not write temp .RfxChain";
            return {};
        }
        outFiles.add(target.getFullPathName());
        return outFiles;
    }

    // VST3 Preset (.vstpreset): one file per VST3 plugin
    const auto subDir = dragDir.getChildFile(baseTitle);
    subDir.createDirectory();
    for (int i = 0; i < arr->size(); ++i)
    {
        const auto& nodeVar = arr->getReference(i);
        const auto info = detail::extractNodeInfo(nodeVar);
        juce::MemoryBlock vp;
        juce::String nodeErr;
        if (!buildSteinbergVstPreset(nodeVar, vp, nodeErr))
            continue;
        const auto prefix = arr->size() > 1 ? juce::String::formatted("%02d_", i + 1) : juce::String();
        const auto target = (arr->size() > 1 ? subDir : dragDir).getChildFile(prefix + detail::sanitizeFileBase(info.name, "Preset") + ".vstpreset");
        if (target.replaceWithData(vp.getData(), vp.getSize()))
            outFiles.add(target.getFullPathName());
    }
    if (outFiles.isEmpty())
    {
        err = "No exportable VST3 preset states found in this chain.";
        return {};
    }
    return outFiles;
}
} // namespace dawexport
