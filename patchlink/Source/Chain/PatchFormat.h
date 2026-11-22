#pragma once
#include <JuceHeader.h>

namespace patchlink {
static constexpr int kFileVersion = 1;

struct Wire { uint32_t src = 0, dst = 0; int srcCh = 0, dstCh = 0; };

// Encode un MemoryBlock d'état plugin en base64 pour le JSON
inline juce::String stateToBase64(juce::AudioProcessor* p)
{
    if (p == nullptr) return {};
    juce::MemoryBlock mb;
    p->getStateInformation(mb);
    if (mb.getSize() == 0) return {};
    return juce::Base64::toBase64(mb.getData(), mb.getSize());
}

inline bool base64ToState(juce::AudioProcessor* p, const juce::String& b64)
{
    if (p == nullptr || b64.isEmpty()) return false;
    juce::MemoryOutputStream mos;
    if (!juce::Base64::convertFromBase64(mos, b64)) return false;
    auto data = mos.getData();
    // getData() de MemoryOutputStream -> MemoryBlock
    juce::MemoryBlock mb(data, mos.getDataSize());
    p->setStateInformation(mb.getData(), (int)mb.getSize());
    return true;
}
} // namespace patchlink
