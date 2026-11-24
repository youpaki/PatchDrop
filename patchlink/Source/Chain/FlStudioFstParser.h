#pragma once

#include <JuceHeader.h>
#include <array>
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
    juce::String fuidHex;
    uint32_t reaperHash = 0;
    int juceUniqueId = 0;
    bool bypass = false;
    bool isVst2 = false;
    juce::String stateBase64;
};

struct ParsedFst
{
    bool valid = false;
    bool isMixerTrack = false;
    juce::String sourceDaw { "FL Studio" };
    juce::String trackName;
    std::vector<FstPluginEntry> plugins;
    juce::StringArray skipped;
    juce::String error;
};

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

inline juce::String tuidBytesToFuidHex(const uint8_t* raw16)
{
    if (raw16 == nullptr)
        return {};
    const uint32_t d1 = juce::ByteOrder::littleEndianInt(raw16);
    const uint16_t d2 = juce::ByteOrder::littleEndianShort(raw16 + 4);
    const uint16_t d3 = juce::ByteOrder::littleEndianShort(raw16 + 6);
    juce::String s = juce::String::formatted("%08X%04X%04X", d1, d2, d3);
    for (int i = 8; i < 16; ++i)
        s << juce::String::formatted("%02X", raw16[i]);
    return s;
}

inline bool fuidHexToTuidBytes(const juce::String& rawFuid, uint8_t out16[16]) noexcept
{
    const auto clean = rawFuid.retainCharacters("0123456789abcdefABCDEF");
    if (clean.length() != 32 || out16 == nullptr)
        return false;

    const uint32_t d1 = static_cast<uint32_t>(clean.substring(0, 8).getHexValue64());
    const uint16_t d2 = static_cast<uint16_t>(clean.substring(8, 12).getHexValue32());
    const uint16_t d3 = static_cast<uint16_t>(clean.substring(12, 16).getHexValue32());
    out16[0] = static_cast<uint8_t>(d1 & 0xffu);
    out16[1] = static_cast<uint8_t>((d1 >> 8) & 0xffu);
    out16[2] = static_cast<uint8_t>((d1 >> 16) & 0xffu);
    out16[3] = static_cast<uint8_t>((d1 >> 24) & 0xffu);
    out16[4] = static_cast<uint8_t>(d2 & 0xffu);
    out16[5] = static_cast<uint8_t>((d2 >> 8) & 0xffu);
    out16[6] = static_cast<uint8_t>(d3 & 0xffu);
    out16[7] = static_cast<uint8_t>((d3 >> 8) & 0xffu);
    for (int i = 8; i < 16; ++i)
        out16[i] = static_cast<uint8_t>(clean.substring(i * 2, i * 2 + 2).getHexValue32());
    return true;
}

inline juce::String fuidHexToTuidHex(const juce::String& rawFuid)
{
    uint8_t raw16[16] {};
    if (!fuidHexToTuidBytes(rawFuid, raw16))
        return {};
    return bytesToLowerHex(raw16, 16);
}

inline int computeJuceUniqueIdFromFuidHex(const juce::String& rawFuid) noexcept
{
    const auto clean = rawFuid.retainCharacters("0123456789abcdefABCDEF");
    if (clean.length() != 32)
        return 0;

    bool allZero = true;
    for (int i = 0; i < 32; ++i)
        if (clean[i] != '0') { allZero = false; break; }
    if (allZero)
        return 0;

    uint32_t hash = 0;
    for (int i = 0; i < 4; ++i)
    {
        const uint32_t item = static_cast<uint32_t>(clean.substring(i * 8, i * 8 + 8).getHexValue64());
        hash = (hash * 31u) + item;
    }
    return static_cast<int>(hash);
}

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

inline juce::String packRawVst3ChunksToBase64(const void* compData, size_t compLen,
                                              const void* contData, size_t contLen)
{
    if ((compData == nullptr || compLen == 0) && (contData == nullptr || contLen == 0))
        return {};

    juce::XmlElement state("VST3PluginState");
    if (compData != nullptr && compLen > 0)
    {
        juce::MemoryBlock mb(compData, compLen);
        state.createNewChildElement("IComponent")->addTextElement(mb.toBase64Encoding());
    }
    if (contData != nullptr && contLen > 0)
    {
        juce::MemoryBlock mb(contData, contLen);
        state.createNewChildElement("IEditController")->addTextElement(mb.toBase64Encoding());
    }

    juce::MemoryBlock vst3StateBlock;
    juce::AudioProcessor::copyXmlToBinary(state, vst3StateBlock);
    return juce::Base64::toBase64(vst3StateBlock.getData(), vst3StateBlock.getSize());
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

    return packRawVst3ChunksToBase64(iComponent.getData(), iComponent.getSize(),
                                     iEditController.getData(), iEditController.getSize());
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

namespace dawparse
{
inline juce::StringArray tokenizeReaperArgs(const juce::String& line)
{
    juce::StringArray tokens;
    int i = 0;
    const int n = line.length();
    while (i < n)
    {
        while (i < n && juce::CharacterFunctions::isWhitespace(line[i]))
            ++i;
        if (i >= n)
            break;
        const auto ch = line[i];
        if (ch == '"' || ch == '\'')
        {
            const auto quote = ch;
            ++i;
            const int start = i;
            while (i < n && line[i] != quote)
                ++i;
            tokens.add(line.substring(start, i));
            if (i < n && line[i] == quote)
                ++i;
        }
        else
        {
            const int start = i;
            while (i < n && !juce::CharacterFunctions::isWhitespace(line[i]))
                ++i;
            tokens.add(line.substring(start, i));
        }
    }
    return tokens;
}

inline void decodeHexToMemoryBlock(const juce::String& text, juce::MemoryBlock& out)
{
    out.reset();
    const auto hex = text.retainCharacters("0123456789abcdefABCDEF");
    const int pairs = hex.length() / 2;
    if (pairs <= 0)
        return;
    out.ensureSize(static_cast<size_t>(pairs));
    auto* dst = static_cast<uint8_t*>(out.getData());
    auto hexVal = [](juce::juce_wchar c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
        return 0;
    };
    for (int i = 0; i < pairs; ++i)
        dst[i] = static_cast<uint8_t>((hexVal(hex[i * 2]) << 4) | hexVal(hex[i * 2 + 1]));
}

inline bool parseSingleVstPresetChunk(const uint8_t* ptr, size_t maxBytes,
                                      const juce::String& fallbackSlotName,
                                      FstPluginEntry& outEntry, size_t& outEndPos)
{
    if (ptr == nullptr || maxBytes < 48 || std::memcmp(ptr, "VST3", 4) != 0)
        return false;
    const uint32_t ver = juce::ByteOrder::littleEndianInt(ptr + 4);
    if (ver != 1u)
        return false;

    juce::String fuid;
    for (int i = 0; i < 32; ++i)
    {
        const char ch = static_cast<char>(ptr[8 + i]);
        const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!isHex)
            return false;
        fuid << static_cast<juce::juce_wchar>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(ch)));
    }

    const int64_t listOffset = static_cast<int64_t>(juce::ByteOrder::littleEndianInt64(ptr + 40));
    if (listOffset < 48 || static_cast<size_t>(listOffset) + 8 > maxBytes)
        return false;
    if (std::memcmp(ptr + listOffset, "List", 4) != 0)
        return false;

    const int32_t count = static_cast<int32_t>(juce::ByteOrder::littleEndianInt(ptr + listOffset + 4));
    if (count < 0 || count > 64)
        return false;

    const size_t tableEnd = static_cast<size_t>(listOffset) + 8 + static_cast<size_t>(count) * 20;
    if (tableEnd > maxBytes)
        return false;

    juce::MemoryBlock comp, cont;
    juce::String metaName, metaVendor;
    size_t maxExtent = tableEnd;

    for (int i = 0; i < count; ++i)
    {
        const uint8_t* row = ptr + listOffset + 8 + static_cast<size_t>(i) * 20;
        const int64_t off = static_cast<int64_t>(juce::ByteOrder::littleEndianInt64(row + 4));
        const int64_t sz = static_cast<int64_t>(juce::ByteOrder::littleEndianInt64(row + 12));
        if (off < 48 || sz < 0 || static_cast<size_t>(off + sz) > maxBytes)
            continue;
        maxExtent = juce::jmax(maxExtent, static_cast<size_t>(off + sz));

        if (std::memcmp(row, "Comp", 4) == 0 && sz > 0)
        {
            comp.append(ptr + off, static_cast<size_t>(sz));
        }
        else if (std::memcmp(row, "Cont", 4) == 0 && sz > 0)
        {
            cont.append(ptr + off, static_cast<size_t>(sz));
        }
        else if (std::memcmp(row, "Info", 4) == 0 && sz > 0)
        {
            const auto xmlStr = juce::String::fromUTF8(reinterpret_cast<const char*>(ptr + off), static_cast<int>(sz));
            if (auto xml = juce::XmlDocument::parse(xmlStr))
            {
                std::function<void(const juce::XmlElement*)> walkInfo = [&](const juce::XmlElement* el) {
                    if (el == nullptr) return;
                    const auto id = el->getStringAttribute("id");
                    if (id.equalsIgnoreCase("PlugInName") || id.equalsIgnoreCase("Name"))
                        if (metaName.isEmpty())
                            metaName = el->getStringAttribute("value").trim();
                    if (id.equalsIgnoreCase("PlugInVendor") || id.equalsIgnoreCase("Vendor"))
                        if (metaVendor.isEmpty())
                            metaVendor = el->getStringAttribute("value").trim();
                    for (auto* child : el->getChildIterator())
                        walkInfo(child);
                };
                walkInfo(xml.get());
            }
        }
    }

    outEntry = FstPluginEntry{};
    outEntry.fuidHex = fuid;
    outEntry.tuidHex = fuidHexToTuidHex(fuid);
    outEntry.juceUniqueId = computeJuceUniqueIdFromFuidHex(fuid);
    outEntry.pluginName = metaName.isNotEmpty() ? metaName : fallbackSlotName;
    outEntry.slotName = outEntry.pluginName;
    outEntry.vendor = metaVendor;
    outEntry.stateBase64 = packRawVst3ChunksToBase64(comp.getData(), comp.getSize(), cont.getData(), cont.getSize());
    outEndPos = maxExtent;
    return true;
}

inline void scanBufferForVstPresets(const uint8_t* ptr, size_t size,
                                    const juce::String& slotFallback,
                                    std::vector<FstPluginEntry>& outPlugins)
{
    if (ptr == nullptr || size < 48)
        return;

    size_t pos = 0;
    while (pos + 48 <= size)
    {
        if (ptr[pos] == 'V' && ptr[pos + 1] == 'S' && ptr[pos + 2] == 'T' && ptr[pos + 3] == '3'
            && juce::ByteOrder::littleEndianInt(ptr + pos + 4) == 1u)
        {
            FstPluginEntry entry;
            size_t endPos = 0;
            if (parseSingleVstPresetChunk(ptr + pos, size - pos, slotFallback, entry, endPos) && endPos >= 48)
            {
                outPlugins.push_back(std::move(entry));
                pos += endPos;
                continue;
            }
        }
        ++pos;
    }
}

inline bool gunzipIfNeeded(const void* rawData, size_t totalSize, juce::MemoryBlock& out)
{
    out.reset();
    const auto* bytes = static_cast<const uint8_t*>(rawData);
    if (bytes == nullptr || totalSize == 0)
        return false;
    if (totalSize >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b)
    {
        juce::MemoryInputStream mis(rawData, totalSize, false);
        juce::GZIPDecompressorInputStream gz(&mis, false, juce::GZIPDecompressorInputStream::gzipFormat);
        gz.readIntoMemoryBlock(out);
        return out.getSize() > 0;
    }
    out.append(rawData, totalSize);
    return true;
}
} // namespace dawparse

inline ParsedFst parseReaperRfxChainData(const void* rawData, size_t totalSize, const juce::String& fallbackName = {})
{
    ParsedFst result;
    result.sourceDaw = "REAPER";
    result.trackName = fallbackName;

    if (rawData == nullptr || totalSize == 0)
    {
        result.error = "Empty REAPER file.";
        return result;
    }

    const auto text = juce::String::fromUTF8(reinterpret_cast<const char*>(rawData), static_cast<int>(totalSize));
    const auto lines = juce::StringArray::fromLines(text);

    bool pendingBypass = false;
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto trimmed = lines[i].trim();
        if (trimmed.isEmpty())
            continue;

        if (trimmed.startsWith("BYPASS "))
        {
            const auto parts = dawparse::tokenizeReaperArgs(trimmed);
            pendingBypass = parts.size() > 1 && parts[1].getIntValue() != 0;
            continue;
        }

        if (trimmed.startsWithIgnoreCase("<JS ") || trimmed.startsWithIgnoreCase("<AU ") || trimmed.startsWithIgnoreCase("<DX "))
        {
            const auto args = dawparse::tokenizeReaperArgs(trimmed.substring(1));
            const auto label = args.size() > 1 ? args[1] : trimmed;
            result.skipped.add(label + " (REAPER non-VST3 FX)");
            while (++i < lines.size() && lines[i].trim() != ">") {}
            pendingBypass = false;
            continue;
        }

        if (trimmed.startsWithIgnoreCase("<VST "))
        {
            const auto args = dawparse::tokenizeReaperArgs(trimmed.substring(4));
            const auto rawDisplay = args.size() > 0 ? args[0] : juce::String("Plugin");
            const auto fileToken = args.size() > 1 ? args[1] : juce::String();

            juce::String idToken;
            for (int t = args.size(); --t >= 2;)
            {
                if (args[t].containsChar('{') || args[t].containsChar('<'))
                {
                    idToken = args[t];
                    break;
                }
            }

            const bool isVst3 = rawDisplay.startsWithIgnoreCase("VST3:") || fileToken.endsWithIgnoreCase(".vst3")
                              || idToken.containsChar('{');

            juce::String cleanName = rawDisplay;
            if (cleanName.startsWithIgnoreCase("VST3:"))
                cleanName = cleanName.substring(5).trim();
            else if (cleanName.startsWithIgnoreCase("VST:"))
                cleanName = cleanName.substring(4).trim();

            juce::String vendor;
            const int lp = cleanName.lastIndexOfChar('(');
            const int rp = cleanName.lastIndexOfChar(')');
            if (lp > 0 && rp > lp + 1 && rp == cleanName.length() - 1)
            {
                vendor = cleanName.substring(lp + 1, rp).trim();
                cleanName = cleanName.substring(0, lp).trim();
            }

            juce::StringArray b64Lines;
            while (++i < lines.size())
            {
                const auto inner = lines[i].trim();
                if (inner == ">")
                    break;
                if (inner.isNotEmpty() && !inner.startsWithChar('<'))
                    b64Lines.add(inner);
            }

            if (!isVst3)
            {
                FstPluginEntry vst2Entry;
                vst2Entry.isVst2 = true;
                vst2Entry.slotName = cleanName;
                vst2Entry.pluginName = cleanName.replace("_x64", "", true).replace("_x86", "", true).trim();
                vst2Entry.vendor = vendor;
                vst2Entry.filePath = fileToken;
                vst2Entry.bypass = pendingBypass;
                pendingBypass = false;
                if (vst2Entry.pluginName.isNotEmpty())
                    result.plugins.push_back(std::move(vst2Entry));
                else
                    result.skipped.add(cleanName + " (VST2 plugin)");
                continue;
            }

            FstPluginEntry entry;
            entry.slotName = cleanName;
            entry.pluginName = cleanName;
            entry.vendor = vendor;
            entry.filePath = fileToken;
            entry.bypass = pendingBypass;
            pendingBypass = false;

            const int ob = idToken.indexOfChar('{');
            const int cb = idToken.indexOfChar(ob + 1, '}');
            if (ob >= 0 && cb > ob)
            {
                const auto hashStr = idToken.substring(0, ob).trim();
                if (hashStr.isNotEmpty())
                    entry.reaperHash = static_cast<uint32_t>(hashStr.getIntValue());
                entry.fuidHex = idToken.substring(ob + 1, cb).retainCharacters("0123456789abcdefABCDEF").toUpperCase();
                if (entry.fuidHex.length() == 32)
                {
                    entry.tuidHex = fuidHexToTuidHex(entry.fuidHex);
                    entry.juceUniqueId = computeJuceUniqueIdFromFuidHex(entry.fuidHex);
                }
            }

            // Decode REAPER 3-chunk base64 layout: chunk0 (76/60B header), chunk1 (comp + cont state), chunk2 (6B flags)
            std::vector<juce::MemoryBlock> decodedChunks;
            juce::String curB64;
            size_t expectedChunk1Bytes = 0;

            auto flushChunk = [&](bool isFirstChunk) {
                if (curB64.isEmpty())
                    return;
                juce::MemoryOutputStream dec;
                if (juce::Base64::convertFromBase64(dec, curB64) && dec.getDataSize() > 0)
                {
                    juce::MemoryBlock mb = dec.getMemoryBlock();
                    if (isFirstChunk && mb.getSize() >= 16)
                    {
                        const auto* b = static_cast<const uint8_t*>(mb.getData());
                        if (juce::ByteOrder::littleEndianInt(b + 4) == 0xFEED5EEEu)
                            expectedChunk1Bytes = static_cast<size_t>(juce::ByteOrder::littleEndianInt(b + mb.getSize() - 12));
                    }
                    decodedChunks.push_back(std::move(mb));
                }
                curB64.clear();
            };

            for (const auto& l : b64Lines)
            {
                curB64 << l;
                const bool chunkIdxIsZero = decodedChunks.empty();
                const bool endedByLength = l.length() < 128 || l.endsWithChar('=');
                bool endedBySize = false;
                if (decodedChunks.size() == 1 && expectedChunk1Bytes > 0)
                {
                    const size_t approxDecoded = (static_cast<size_t>(curB64.length()) * 3) / 4;
                    if (approxDecoded >= expectedChunk1Bytes && (curB64.length() % 4) == 0)
                        endedBySize = true;
                }
                if (endedByLength || endedBySize)
                    flushChunk(chunkIdxIsZero);
            }
            if (curB64.isNotEmpty())
                flushChunk(decodedChunks.empty());

            if (decodedChunks.size() >= 2)
            {
                const auto& c1 = decodedChunks[1];
                const auto* b = static_cast<const uint8_t*>(c1.getData());
                const size_t sz = c1.getSize();
                if (sz >= 8)
                {
                    const uint32_t compLen = juce::ByteOrder::littleEndianInt(b);
                    if (static_cast<size_t>(compLen) <= sz - 8)
                    {
                        const uint8_t* compPtr = b + 8;
                        const size_t remOff = 8 + static_cast<size_t>(compLen);
                        const uint8_t* contPtr = nullptr;
                        size_t contLen = 0;
                        if (remOff + 8 <= sz)
                        {
                            const uint32_t candLen = juce::ByteOrder::littleEndianInt(b + remOff);
                            if (static_cast<size_t>(candLen) <= sz - (remOff + 8))
                            {
                                contLen = static_cast<size_t>(candLen);
                                contPtr = b + remOff + 8;
                            }
                        }
                        entry.stateBase64 = packRawVst3ChunksToBase64(compPtr, compLen, contPtr, contLen);
                    }
                }
            }

            result.plugins.push_back(std::move(entry));
        }
    }

    result.valid = !result.plugins.empty() || !result.skipped.isEmpty();
    if (!result.valid)
        result.error = "No VST3 effects found in REAPER file.";
    result.isMixerTrack = result.plugins.size() > 1;
    return result;
}

inline ParsedFst parseAbletonAdgData(const void* rawData, size_t totalSize, const juce::String& fallbackName = {})
{
    ParsedFst result;
    result.sourceDaw = "Ableton Live";
    result.trackName = fallbackName;

    juce::MemoryBlock xmlData;
    if (!dawparse::gunzipIfNeeded(rawData, totalSize, xmlData))
    {
        result.error = "Could not decompress Ableton preset.";
        return result;
    }

    const auto xmlText = xmlData.toString();
    auto xml = juce::XmlDocument::parse(xmlText);
    if (xml == nullptr)
    {
        result.error = "Invalid Ableton XML preset.";
        return result;
    }

    auto readChildValue = [](const juce::XmlElement* parent, const char* childName) -> juce::String {
        if (parent == nullptr) return {};
        if (auto* c = parent->getChildByName(childName))
            return c->getStringAttribute("Value").trim();
        return {};
    };

    if (auto* gdp = xml->getChildByName("GroupDevicePreset"))
    {
        if (auto* dev = gdp->getChildByName("Device"))
            if (auto* rack = dev->getChildByName("AudioEffectGroupDevice"))
            {
                const auto uname = readChildValue(rack, "UserName");
                if (uname.isNotEmpty())
                    result.trackName = uname;
            }
    }

    std::function<void(const juce::XmlElement*)> walk = [&](const juce::XmlElement* el) {
        if (el == nullptr)
            return;

        if (el->hasTagName("PluginDevice"))
        {
            bool bypassed = false;
            if (auto* onEl = el->getChildByName("On"))
                if (readChildValue(onEl, "Manual").equalsIgnoreCase("false"))
                    bypassed = true;

            if (auto* descEl = el->getChildByName("PluginDesc"))
            {
                if (auto* v3 = descEl->getChildByName("Vst3PluginInfo"))
                {
                    FstPluginEntry entry;
                    entry.bypass = bypassed;

                    auto plugName = readChildValue(v3, "PlugName");
                    if (plugName.isEmpty())
                        plugName = readChildValue(v3, "Name");

                    const auto* uidEl = v3->getChildByName("Uid");
                    const juce::XmlElement* vst3PresetEl = nullptr;
                    if (auto* pEl = v3->getChildByName("Preset"))
                        vst3PresetEl = pEl->getChildByName("Vst3Preset");

                    if (plugName.isEmpty() && vst3PresetEl != nullptr)
                    {
                        plugName = readChildValue(vst3PresetEl, "PlugName");
                        if (plugName.isEmpty())
                            plugName = readChildValue(vst3PresetEl, "Name");
                    }
                    if (plugName.isEmpty())
                    {
                        const auto fn = readChildValue(v3, "FileName");
                        if (fn.isNotEmpty())
                            plugName = juce::File::createFileWithoutCheckingPath(fn).getFileNameWithoutExtension();
                    }

                    if (uidEl == nullptr && vst3PresetEl != nullptr)
                        uidEl = vst3PresetEl->getChildByName("Uid");

                    if (uidEl != nullptr)
                    {
                        const uint32_t f0 = static_cast<uint32_t>(static_cast<int64_t>(readChildValue(uidEl, "Fields.0").getLargeIntValue()));
                        const uint32_t f1 = static_cast<uint32_t>(static_cast<int64_t>(readChildValue(uidEl, "Fields.1").getLargeIntValue()));
                        const uint32_t f2 = static_cast<uint32_t>(static_cast<int64_t>(readChildValue(uidEl, "Fields.2").getLargeIntValue()));
                        const uint32_t f3 = static_cast<uint32_t>(static_cast<int64_t>(readChildValue(uidEl, "Fields.3").getLargeIntValue()));
                        entry.fuidHex = juce::String::formatted("%08X%08X%08X%08X", f0, f1, f2, f3);
                        entry.tuidHex = fuidHexToTuidHex(entry.fuidHex);
                        entry.juceUniqueId = computeJuceUniqueIdFromFuidHex(entry.fuidHex);
                    }

                    juce::MemoryBlock comp, cont;
                    if (vst3PresetEl != nullptr)
                    {
                        if (auto* ps = vst3PresetEl->getChildByName("ProcessorState"))
                            dawparse::decodeHexToMemoryBlock(ps->getAllSubText(), comp);
                        if (auto* cs = vst3PresetEl->getChildByName("ControllerState"))
                            dawparse::decodeHexToMemoryBlock(cs->getAllSubText(), cont);
                    }

                    entry.pluginName = plugName;
                    entry.slotName = plugName;
                    entry.stateBase64 = packRawVst3ChunksToBase64(comp.getData(), comp.getSize(),
                                                                  cont.getData(), cont.getSize());
                    if (entry.pluginName.isNotEmpty() || entry.fuidHex.length() == 32)
                        result.plugins.push_back(std::move(entry));
                    return;
                }
                if (auto* v2 = descEl->getChildByName("VstPluginInfo"))
                {
                    auto n = readChildValue(v2, "PlugName");
                    if (n.isEmpty()) n = readChildValue(v2, "Name");
                    if (n.isEmpty())
                    {
                        const auto p = readChildValue(v2, "Path");
                        if (p.isNotEmpty())
                            n = juce::File::createFileWithoutCheckingPath(p).getFileNameWithoutExtension();
                    }
                    if (n.isNotEmpty())
                    {
                        FstPluginEntry vst2Entry;
                        vst2Entry.isVst2 = true;
                        vst2Entry.bypass = bypassed;
                        vst2Entry.slotName = n;
                        vst2Entry.pluginName = n.replace("_x64", "", true).replace("_x86", "", true).replace(" (Mono)", "", true).trim();
                        result.plugins.push_back(std::move(vst2Entry));
                    }
                    else
                    {
                        result.skipped.add("VST2 Plugin (missing plugin name)");
                    }
                    return;
                }
                if (auto* au = descEl->getChildByName("AuPluginInfo"))
                {
                    auto n = readChildValue(au, "Name");
                    if (n.isEmpty()) n = readChildValue(au, "PlugName");
                    if (n.isNotEmpty())
                    {
                        FstPluginEntry auEntry;
                        auEntry.isVst2 = true; // resolve by name to local VST3
                        auEntry.bypass = bypassed;
                        auEntry.slotName = n;
                        auEntry.pluginName = n;
                        result.plugins.push_back(std::move(auEntry));
                    }
                    else
                    {
                        result.skipped.add("Audio Unit (missing plugin name)");
                    }
                    return;
                }
            }
            return;
        }

        for (auto* child : el->getChildIterator())
            walk(child);
    };

    walk(xml.get());

    result.valid = !result.plugins.empty() || !result.skipped.isEmpty();
    if (!result.valid)
        result.error = "No third-party VST3 plugins found in Ableton preset (Ableton stock devices cannot run outside Live).";
    result.isMixerTrack = result.plugins.size() > 1;
    return result;
}

inline ParsedFst parseVstPresetData(const void* rawData, size_t totalSize,
                                    const juce::String& fallbackName = {},
                                    const juce::String& dawHint = "VST3 Preset")
{
    ParsedFst result;
    result.sourceDaw = dawHint;
    result.trackName = fallbackName;

    if (rawData == nullptr || totalSize == 0)
    {
        result.error = "Empty preset file.";
        return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(rawData);

    // Case 1: ZIP container (Studio One .fxchain / .multipreset, archives)
    if (totalSize >= 4 && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 0x03 && bytes[3] == 0x04)
    {
        juce::MemoryInputStream mis(rawData, totalSize, false);
        juce::ZipFile zip(&mis, false);
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            if (auto entryStream = std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(i)))
            {
                juce::MemoryBlock entryMb;
                entryStream->readIntoMemoryBlock(entryMb);
                const auto* eInfo = zip.getEntry(i);
                const auto leaf = eInfo != nullptr ? juce::File::createFileWithoutCheckingPath(eInfo->filename).getFileNameWithoutExtension()
                                                   : juce::String();
                dawparse::scanBufferForVstPresets(static_cast<const uint8_t*>(entryMb.getData()),
                                                  entryMb.getSize(), leaf, result.plugins);
            }
        }
    }
    else
    {
        // Case 2: Raw .vstpreset, .bwpreset, or gzipped XML/binary container
        juce::MemoryBlock mb;
        dawparse::gunzipIfNeeded(rawData, totalSize, mb);
        dawparse::scanBufferForVstPresets(static_cast<const uint8_t*>(mb.getData()), mb.getSize(),
                                          fallbackName, result.plugins);

        // Case 3: Plain XML encoding embedded base64 .vstpreset chunks (starts with VST3\x01... -> "VlNUMw")
        if (result.plugins.empty())
        {
            const auto text = mb.toString();
            int pos = 0;
            while (pos < text.length())
            {
                const int hit = text.indexOf(pos, "VlNUMw");
                if (hit < 0)
                    break;
                int start = hit;
                int end = hit;
                auto isB64 = [](juce::juce_wchar c) {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                        || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=' || juce::CharacterFunctions::isWhitespace(c);
                };
                while (end < text.length() && isB64(text[end]))
                    ++end;
                juce::MemoryOutputStream dec;
                if (juce::Base64::convertFromBase64(dec, text.substring(start, end)) && dec.getDataSize() >= 48)
                    dawparse::scanBufferForVstPresets(static_cast<const uint8_t*>(dec.getData()),
                                                      dec.getDataSize(), fallbackName, result.plugins);
                pos = juce::jmax(end, hit + 6);
            }
        }
    }

    result.valid = !result.plugins.empty();
    if (!result.valid)
        result.error = "No VST3 preset data found in " + fallbackName;
    result.isMixerTrack = result.plugins.size() > 1;
    return result;
}

inline ParsedFst parseAnyDawFile(const juce::File& file, const void* rawData = nullptr, size_t rawSize = 0)
{
    juce::MemoryBlock loaded;
    if (rawData == nullptr || rawSize == 0)
    {
        if (!file.existsAsFile() || !file.loadFileAsData(loaded))
        {
            ParsedFst r;
            r.error = "Could not read " + file.getFileName();
            return r;
        }
        rawData = loaded.getData();
        rawSize = loaded.getSize();
    }

    const auto ext = file.getFileExtension().toLowerCase();
    const auto base = file.getFileNameWithoutExtension();
    const auto* bytes = static_cast<const uint8_t*>(rawData);

    ParsedFst parsed;
    if (ext == ".fst" || (rawSize >= 4 && std::memcmp(bytes, "FLhd", 4) == 0))
    {
        parsed = parseFstData(rawData, rawSize);
    }
    else if (ext == ".rfxchain" || ext == ".rpp")
    {
        parsed = parseReaperRfxChainData(rawData, rawSize, base);
    }
    else if (ext == ".adg" || ext == ".adv" || ext == ".als")
    {
        parsed = parseAbletonAdgData(rawData, rawSize, base);
    }
    else if (ext == ".fxchain" || ext == ".multipreset")
    {
        parsed = parseVstPresetData(rawData, rawSize, base, "Studio One");
    }
    else if (ext == ".fxchainpreset")
    {
        parsed = parseVstPresetData(rawData, rawSize, base, "Cubase");
    }
    else if (ext == ".bwpreset")
    {
        parsed = parseVstPresetData(rawData, rawSize, base, "Bitwig Studio");
    }
    else if (ext == ".vstpreset" || (rawSize >= 4 && std::memcmp(bytes, "VST3", 4) == 0))
    {
        parsed = parseVstPresetData(rawData, rawSize, base, "VST3 Preset");
    }
    else
    {
        const auto headText = juce::String::fromUTF8(reinterpret_cast<const char*>(bytes),
                                                     static_cast<int>(juce::jmin<size_t>(rawSize, 512)));
        if (headText.contains("<VST ") || headText.contains("<REAPER_PROJECT"))
            parsed = parseReaperRfxChainData(rawData, rawSize, base);
        else if (headText.contains("<Ableton") || (rawSize >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b))
            parsed = parseAbletonAdgData(rawData, rawSize, base);
        else
            parsed = parseVstPresetData(rawData, rawSize, base, "VST3 Preset");
    }

    if (parsed.valid && parsed.trackName.isEmpty())
        parsed.trackName = base;
    return parsed;
}

inline juce::var parsedFstToPatchVar(const ParsedFst& parsed, const juce::KnownPluginList* knownList = nullptr)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("app", "PatchLink");
    root->setProperty("chainFormat", 2);
    root->setProperty("name", parsed.trackName.isNotEmpty()
                                  ? parsed.trackName
                                  : (parsed.sourceDaw.isNotEmpty() ? parsed.sourceDaw + " Chain" : "FX Chain"));

    auto matchType = [&](const FstPluginEntry& entry, juce::PluginDescription& out) -> bool {
        if (knownList == nullptr || entry.isVst2)
            return false;
        const auto types = knownList->getTypes();
        if (entry.juceUniqueId != 0)
            for (const auto& t : types)
                if (t.pluginFormatName == "VST3" && t.uniqueId == entry.juceUniqueId)
                    { out = t; return true; }
        if (entry.filePath.isNotEmpty())
        {
            const auto entryLeaf = juce::File::createFileWithoutCheckingPath(entry.filePath).getFileName();
            for (const auto& t : types)
                if (t.pluginFormatName == "VST3"
                    && (t.fileOrIdentifier.equalsIgnoreCase(entry.filePath)
                        || juce::File::createFileWithoutCheckingPath(t.fileOrIdentifier).getFileName().equalsIgnoreCase(entryLeaf))
                    && (entry.pluginName.isEmpty() || t.name.equalsIgnoreCase(entry.slotName) || t.name.equalsIgnoreCase(entry.pluginName)))
                    { out = t; return true; }
        }
        for (const auto& t : types)
            if (t.pluginFormatName == "VST3"
                && ((entry.slotName.isNotEmpty() && t.name.equalsIgnoreCase(entry.slotName))
                    || (entry.pluginName.isNotEmpty() && t.name.equalsIgnoreCase(entry.pluginName))))
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
        if (entry.fuidHex.isNotEmpty())
            ids->setProperty("fuidHex", entry.fuidHex);
        if (entry.reaperHash != 0)
            ids->setProperty("reaperHash", static_cast<juce::int64>(entry.reaperHash));
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
