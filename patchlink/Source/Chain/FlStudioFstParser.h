#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace flstudio
{
struct FstPluginEntry
{
    juce::String slotName;
    juce::String pluginName;
    juce::String vendor;
    juce::String filePath;
    juce::String tuidHex;
    int juceUniqueId = 0;
    bool bypass = false;
    bool isVst2 = false;
    juce::String stateBase64;
};

struct ParsedFst
{
    bool valid = false;
    bool isMixerTrack = false;
    juce::String trackName;
    std::vector<FstPluginEntry> plugins;
    juce::StringArray skipped;
    juce::String error;
};

inline int computeJuceUniqueIdFromTuid(const uint8_t* raw16) noexcept
{
    bool allZero = true;
    for (int i = 0; i < 16; ++i)
    {
        if (raw16[i] != 0)
        {
            allZero = false;
            break;
        }
    }
    if (allZero)
        return 0;

    const uint32_t d1 = juce::ByteOrder::littleEndianInt(raw16);
    const uint16_t d2 = juce::ByteOrder::littleEndianShort(raw16 + 4);
    const uint16_t d3 = juce::ByteOrder::littleEndianShort(raw16 + 6);
    const uint32_t l1 = d1;
    const uint32_t l2 = (static_cast<uint32_t>(d2) << 16) | static_cast<uint32_t>(d3);
    const uint32_t l3 = juce::ByteOrder::bigEndianInt(raw16 + 8);
    const uint32_t l4 = juce::ByteOrder::bigEndianInt(raw16 + 12);

    uint32_t hash = 0;
    for (uint32_t item : { l1, l2, l3, l4 })
        hash = (hash * 31u) + item;
    return static_cast<int>(hash);
}

inline juce::String decodeFlText(const uint8_t* bytes, size_t len)
{
    if (bytes == nullptr || len == 0)
        return {};

    // FL Studio 11+ / 20+ stores event strings (201, 203, 204) as UTF-16LE.
    if (len >= 2 && (len % 2) == 0)
    {
        size_t zeroHighBytes = 0;
        const size_t codeUnits = len / 2;
        for (size_t i = 0; i < codeUnits; ++i)
            if (bytes[i * 2 + 1] == 0)
                ++zeroHighBytes;

        if (zeroHighBytes * 2 >= codeUnits)
        {
            std::vector<juce::CharPointer_UTF16::CharType> buf(codeUnits + 1, 0);
            for (size_t i = 0; i < codeUnits; ++i)
                buf[i] = static_cast<juce::CharPointer_UTF16::CharType>(juce::ByteOrder::littleEndianShort(bytes + i * 2));
            return juce::String(juce::CharPointer_UTF16(buf.data())).trim();
        }
    }

    return juce::String::fromUTF8(reinterpret_cast<const char*>(bytes), static_cast<int>(len)).trim();
}

inline juce::String extractVst3StateBase64(const uint8_t* subData, size_t subLen)
{
    if (subData == nullptr || subLen < 4)
        return {};

    if (juce::ByteOrder::littleEndianInt(subData) != 1u)
        return {};

    juce::MemoryBlock iComponent;
    juce::MemoryBlock iEditController;

    size_t pos = 4;
    while (pos + 12 <= subLen)
    {
        const uint32_t cid = juce::ByteOrder::littleEndianInt(subData + pos);
        const int64_t clen = static_cast<int64_t>(juce::ByteOrder::littleEndianInt64(subData + pos + 4));
        if (clen < 0 || pos + 12 + static_cast<size_t>(clen) > subLen)
            break;

        const uint8_t* cdata = subData + pos + 12;
        pos += 12 + static_cast<size_t>(clen);

        if (cid == 3 && clen > 0)
            iComponent.append(cdata, static_cast<size_t>(clen));
        else if (cid == 2 && clen > 0)
            iEditController.append(cdata, static_cast<size_t>(clen));
    }

    if (iComponent.getSize() == 0 && iEditController.getSize() == 0)
        return {};

    juce::XmlElement state("VST3PluginState");
    if (iComponent.getSize() > 0)
        state.createNewChildElement("IComponent")->addTextElement(iComponent.toBase64Encoding());
    if (iEditController.getSize() > 0)
        state.createNewChildElement("IEditController")->addTextElement(iEditController.toBase64Encoding());

    juce::MemoryBlock vst3StateBlock;
    juce::AudioProcessor::copyXmlToBinary(state, vst3StateBlock);
    return juce::Base64::toBase64(vst3StateBlock.getData(), vst3StateBlock.getSize());
}

inline bool extractRawVst3ChunksFromBase64(const juce::String& stateBase64,
                                           juce::MemoryBlock& outComp,
                                           juce::MemoryBlock& outCont)
{
    outComp.reset();
    outCont.reset();
    if (stateBase64.isEmpty())
        return false;

    juce::MemoryOutputStream mos;
    if (!juce::Base64::convertFromBase64(mos, stateBase64) || mos.getDataSize() == 0)
        return false;

    auto xml = juce::AudioProcessor::getXmlFromBinary(mos.getData(), static_cast<int>(mos.getDataSize()));
    if (xml == nullptr)
        return false;

    if (auto* comp = xml->getChildByName("IComponent"))
        outComp.fromBase64Encoding(comp->getAllSubText().trim());
    if (auto* cont = xml->getChildByName("IEditController"))
        outCont.fromBase64Encoding(cont->getAllSubText().trim());

    return xml->hasTagName("VST3PluginState") || outComp.getSize() > 0 || outCont.getSize() > 0;
}

inline juce::MemoryBlock buildVst3WrapperSub53(const juce::String& stateBase64)
{
    juce::MemoryBlock comp, cont;
    if (!extractRawVst3ChunksFromBase64(stateBase64, comp, cont))
        return {};

    juce::MemoryBlock out;
    juce::MemoryOutputStream mos(out, false);
    mos.writeInt(1); // VST3 wrapper blob version 1

    // cid 1: 64-byte header flags
    mos.writeInt(1);
    mos.writeInt64(64);
    mos.writeInt(1);
    for (int i = 0; i < 60; ++i)
        mos.writeByte(0);

    // cid 3: IComponent state
    mos.writeInt(3);
    mos.writeInt64(static_cast<juce::int64>(comp.getSize()));
    if (comp.getSize() > 0)
        mos.write(comp.getData(), comp.getSize());

    // cid 2: IEditController state
    mos.writeInt(2);
    mos.writeInt64(static_cast<juce::int64>(cont.getSize()));
    if (cont.getSize() > 0)
        mos.write(cont.getData(), cont.getSize());

    mos.flush();
    return out;
}

inline juce::String bytesToLowerHex(const uint8_t* ptr, size_t len)
{
    static constexpr char kHex[] = "0123456789abcdef";
    juce::String s;
    s.preallocateBytes(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        const uint8_t b = ptr[i];
        s << kHex[(b >> 4) & 0xF] << kHex[b & 0xF];
    }
    return s;
}

inline ParsedFst parseFstData(const void* rawData, size_t totalSize)
{
    ParsedFst result;
    const auto* data = static_cast<const uint8_t*>(rawData);
    if (data == nullptr || totalSize < 16)
    {
        result.error = "File is too small to be an FL Studio preset.";
        return result;
    }

    if (std::memcmp(data, "FLhd", 4) != 0)
    {
        result.error = "Not a valid FL Studio .fst file (missing FLhd header).";
        return result;
    }

    const uint32_t hdLen = juce::ByteOrder::littleEndianInt(data + 4);
    size_t pos = 8 + static_cast<size_t>(hdLen);
    if (pos + 8 > totalSize || std::memcmp(data + pos, "FLdt", 4) != 0)
    {
        result.error = "Invalid FL Studio .fst structure (missing FLdt chunk).";
        return result;
    }

    const uint32_t dtLen = juce::ByteOrder::littleEndianInt(data + pos + 4);
    pos += 8;
    const size_t end = juce::jmin(totalSize, pos + static_cast<size_t>(dtLen));

    bool sawSlotEvents = false;
    bool sawTrackName = false;
    bool sawMixerBlocks = false;

    juce::String curPlugin;
    juce::String curSlotName;
    bool hasD4 = false;
    uint32_t d4_2 = 0;
    uint32_t d4_4 = 0;

    auto resetSlot = [&]()
    {
        curPlugin.clear();
        curSlotName.clear();
        hasD4 = false;
        d4_2 = 0;
        d4_4 = 0;
    };

    while (pos < end)
    {
        const uint8_t ev = data[pos++];
        if (ev < 64)
        {
            if (pos + 1 > end) break;
            pos += 1;
        }
        else if (ev < 128)
        {
            if (pos + 2 > end) break;
            pos += 2;
            if (ev == 98) // 0x62: end of mixer slot
            {
                sawSlotEvents = true;
                resetSlot();
            }
        }
        else if (ev < 192)
        {
            if (pos + 4 > end) break;
            pos += 4;
        }
        else
        {
            size_t length = 0;
            int shift = 0;
            bool ok = true;
            while (pos < end)
            {
                const uint8_t b = data[pos++];
                length |= static_cast<size_t>(b & 0x7Fu) << shift;
                if ((b & 0x80u) == 0)
                    break;
                shift += 7;
                if (shift > 28)
                {
                    ok = false;
                    break;
                }
            }
            if (!ok || pos + length > end)
                break;

            const uint8_t* payload = data + pos;
            pos += length;

            if (ev == 204) // 0xCC: mixer track name
            {
                const auto decoded = decodeFlText(payload, length);
                if (decoded.isNotEmpty() && result.trackName.isEmpty())
                    result.trackName = decoded;
                sawTrackName = true;
            }
            else if (ev == 236 || ev == 225) // mixer routing / EQ blocks
            {
                sawMixerBlocks = true;
            }
            else if (ev == 201) // 0xC9: FL internal plugin name
            {
                curPlugin = decodeFlText(payload, length);
            }
            else if (ev == 203) // 0xCB: slot display name
            {
                curSlotName = decodeFlText(payload, length);
            }
            else if (ev == 212) // 0xD4: mixer slot flags
            {
                if (length == 52)
                {
                    hasD4 = true;
                    d4_2 = juce::ByteOrder::littleEndianInt(payload + 8);
                    d4_4 = juce::ByteOrder::littleEndianInt(payload + 16);
                }
            }
            else if (ev == 213) // 0xD5: plugin state chunk
            {
                const bool isWrapper = curPlugin.isEmpty() || curPlugin.equalsIgnoreCase("Fruity Wrapper");
                const uint32_t wrapperVersion = length >= 4 ? juce::ByteOrder::littleEndianInt(payload) : 0;

                if (isWrapper && wrapperVersion >= 4u && wrapperVersion <= 32u)
                {
                    FstPluginEntry entry;
                    entry.slotName = curSlotName;
                    entry.bypass = hasD4 && (d4_4 != 0u) && (d4_2 == 0u);

                    size_t spos = 4;
                    while (spos + 12 <= length)
                    {
                        const uint32_t subId = juce::ByteOrder::littleEndianInt(payload + spos);
                        const int64_t subLen = static_cast<int64_t>(juce::ByteOrder::littleEndianInt64(payload + spos + 4));
                        if (subLen < 0 || spos + 12 + static_cast<size_t>(subLen) > length)
                            break;

                        const uint8_t* subData = payload + spos + 12;
                        spos += 12 + static_cast<size_t>(subLen);

                        if (subId == 52 && subLen >= 16)
                        {
                            entry.juceUniqueId = computeJuceUniqueIdFromTuid(subData);
                            entry.tuidHex = bytesToLowerHex(subData, 16);
                        }
                        else if (subId == 54)
                        {
                            entry.pluginName = juce::String::fromUTF8(reinterpret_cast<const char*>(subData),
                                                                      static_cast<int>(subLen)).trim();
                        }
                        else if (subId == 55)
                        {
                            entry.filePath = juce::String::fromUTF8(reinterpret_cast<const char*>(subData),
                                                                    static_cast<int>(subLen)).trim();
                        }
                        else if (subId == 56)
                        {
                            entry.vendor = juce::String::fromUTF8(reinterpret_cast<const char*>(subData),
                                                                  static_cast<int>(subLen)).trim();
                        }
                        else if (subId == 53 && subLen > 0)
                        {
                            const bool looksLikeVst3 = subLen >= 4 && juce::ByteOrder::littleEndianInt(subData) == 1u;
                            if (looksLikeVst3)
                                entry.stateBase64 = extractVst3StateBase64(subData, static_cast<size_t>(subLen));
                            else
                                entry.isVst2 = true;
                        }
                    }

                    if (entry.filePath.endsWithIgnoreCase(".dll"))
                        entry.isVst2 = true;

                    if (entry.pluginName.isNotEmpty() || entry.slotName.isNotEmpty() || entry.filePath.isNotEmpty())
                        result.plugins.push_back(std::move(entry));
                }
                else if (curPlugin.isNotEmpty() && !curPlugin.equalsIgnoreCase("Fruity Wrapper"))
                {
                    result.skipped.add(curPlugin + " (FL Studio native plugin)");
                }
            }
        }
    }

    result.valid = true;
    result.isMixerTrack = sawSlotEvents || sawTrackName || sawMixerBlocks || result.plugins.size() > 1;
    return result;
}

inline ParsedFst parseFstFile(const juce::File& file)
{
    juce::MemoryBlock mb;
    if (!file.existsAsFile() || !file.loadFileAsData(mb))
    {
        ParsedFst r;
        r.error = "Could not read .fst file.";
        return r;
    }
    auto parsed = parseFstData(mb.getData(), mb.getSize());
    if (parsed.valid && parsed.trackName.isEmpty())
        parsed.trackName = file.getFileNameWithoutExtension();
    return parsed;
}

inline juce::var parsedFstToPatchVar(const ParsedFst& parsed, const juce::KnownPluginList* knownList = nullptr)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("app", "PatchLink");
    root->setProperty("chainFormat", 2);
    root->setProperty("name", parsed.trackName.isNotEmpty() ? parsed.trackName : "FL Chain");

    auto matchType = [&](const FstPluginEntry& entry, juce::PluginDescription& out) -> bool {
        if (knownList == nullptr || entry.isVst2)
            return false;
        const auto types = knownList->getTypes();
        if (entry.juceUniqueId != 0)
            for (const auto& t : types)
                if (t.pluginFormatName == "VST3" && t.uniqueId == entry.juceUniqueId)
                    { out = t; return true; }
        if (entry.filePath.isNotEmpty())
            for (const auto& t : types)
                if (t.pluginFormatName == "VST3" && t.fileOrIdentifier.equalsIgnoreCase(entry.filePath)
                    && (t.name.equalsIgnoreCase(entry.slotName) || t.name.equalsIgnoreCase(entry.pluginName)))
                    { out = t; return true; }
        for (const auto& t : types)
            if (t.pluginFormatName == "VST3" && (t.name.equalsIgnoreCase(entry.slotName) || t.name.equalsIgnoreCase(entry.pluginName)))
                { out = t; return true; }
        return false;
    };

    juce::Array<juce::var> jnodes;
    int idx = 0;
    for (const auto& entry : parsed.plugins)
    {
        const auto displayName = entry.slotName.isNotEmpty() ? entry.slotName : entry.pluginName;
        juce::PluginDescription desc;
        const bool resolved = matchType(entry, desc);
        if (!resolved)
        {
            desc.name = displayName;
            desc.manufacturerName = entry.vendor;
            desc.pluginFormatName = entry.isVst2 ? "VST" : "VST3";
            desc.fileOrIdentifier = entry.filePath;
            desc.uniqueId = entry.juceUniqueId;
        }

        auto* no = new juce::DynamicObject();
        no->setProperty("uid", 100 + idx);
        no->setProperty("x", 280 + (idx % 4) * 230);
        no->setProperty("y", 200 + (idx / 4) * 140);
        no->setProperty("order", idx);
        no->setProperty("bypass", entry.bypass);
        no->setProperty("wet", 1.0);
        no->setProperty("gainDb", 0.0);

        auto* ids = new juce::DynamicObject();
        ids->setProperty("name", desc.name);
        ids->setProperty("manufacturer", desc.manufacturerName);
        ids->setProperty("version", desc.version);
        ids->setProperty("format", desc.pluginFormatName);
        ids->setProperty("fileOrId", desc.fileOrIdentifier);
        if (resolved)
            ids->setProperty("identifier", desc.createIdentifierString());
        if (desc.uniqueId != 0)
            ids->setProperty("uniqueId", desc.uniqueId);
        if (entry.tuidHex.isNotEmpty())
            ids->setProperty("tuidHex", entry.tuidHex);
        if (entry.pluginName.isNotEmpty())
            ids->setProperty("flPluginName", entry.pluginName);
        no->setProperty("ids", juce::var(ids));
        no->setProperty("stateBase64", entry.isVst2 ? juce::String{} : entry.stateBase64);

        jnodes.add(juce::var(no));
        ++idx;
    }

    root->setProperty("nodes", jnodes);
    root->setProperty("connections", juce::Array<juce::var>());
    return juce::var(root.get());
}
} // namespace flstudio
