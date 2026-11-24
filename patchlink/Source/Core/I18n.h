#pragma once

#include <JuceHeader.h>

namespace i18n
{
enum class Lang { en = 0, fr = 1 };

inline Lang currentLang = Lang::en;

inline bool isFrench() noexcept
{
    return currentLang == Lang::fr;
}

inline juce::String tr(const char* en, const char* fr)
{
    return juce::String::fromUTF8(isFrench() ? fr : en);
}

inline juce::String langCode()
{
    return isFrench() ? "FR" : "EN";
}

inline juce::File langConfigFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("PatchLink")
               .getChildFile("ui_lang.txt");
}

inline void setLanguage(Lang lang, bool persist = true)
{
    currentLang = lang;
    if (persist)
    {
        const auto file = langConfigFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(lang == Lang::fr ? "fr" : "en");
    }
}

inline void loadSavedLanguage()
{
    const auto file = langConfigFile();
    if (file.existsAsFile())
    {
        const auto raw = file.loadFileAsString().trim().toLowerCase();
        if (raw == "fr") { currentLang = Lang::fr; return; }
        if (raw == "en") { currentLang = Lang::en; return; }
    }

    const auto sysLang = juce::SystemStats::getUserLanguage().toLowerCase();
    currentLang = sysLang.startsWith("fr") ? Lang::fr : Lang::en;
}
} // namespace i18n
