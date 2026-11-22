#pragma once
#include <JuceHeader.h>

// Client HTTP minimal vers l'API PatchDrop (gg.trenas.fr/patchdrop).
// Utilisé par les boutons "Partager via code" / "Ouvrir via code".
// Aucune dépendance externe : juce::URL + JSON.
struct ShareClient
{
    juce::String apiBase = "https://gg.trenas.fr/patchdrop";

    // Publie le var patch (graphToVar). Retourne le code, ou "" + err rempli.
    static juce::String publish(const juce::var& patchJson, const juce::String& title,
                                     const juce::String& author, const juce::String& desc,
                                     const juce::String& apiBase, juce::String& err)
    {
        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty("title", title);
        body->setProperty("author", author);
        body->setProperty("description", desc);
        body->setProperty("isPublic", true);
        body->setProperty("patch", patchJson);
        juce::String payload = juce::JSON::toString(juce::var(body.get()));

        juce::URL url(apiBase.trimCharactersAtEnd("/") + "/api/publish");
        url = url.withPOSTData(payload);
        juce::StringPairArray respHeaders;
        int status = 0;
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                        .withExtraHeaders("Content-Type: application/json")
                        .withConnectionTimeoutMs(15000)
                        .withResponseHeaders(&respHeaders)
                        .withStatusCode(&status);
        std::unique_ptr<juce::InputStream> in(url.createInputStream(opts));
        if (in == nullptr) { err = "Connexion impossible a PatchDrop. Utilise Export fichier."; return {}; }
        juce::String txt = in->readEntireStreamAsString();
        if (status < 200 || status >= 300) { err = "HTTP " + juce::String(status) + " : " + txt.substring(0, 200); return {}; }
        auto v = juce::JSON::parse(txt);
        juce::String code = v.getProperty("code", "").toString();
        if (code.isEmpty()) { err = "Reponse inattendue : " + txt.substring(0, 200); return {}; }
        return code;
    }

    // Récupère {title, patch} depuis un code. Retourne true si ok.
    static bool fetch(const juce::String& code, const juce::String& apiBase,
                      juce::var& patchOut, juce::String& titleOut, juce::String& err)
    {
        juce::String c = code.toUpperCase().trim();
        juce::URL url(apiBase.trimCharactersAtEnd("/") + "/api/p/" + c);
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(15000);
        std::unique_ptr<juce::InputStream> in(url.createInputStream(opts));
        if (in == nullptr) { err = "Serveur injoignable."; return false; }
        auto v = juce::JSON::parse(in->readEntireStreamAsString());
        if (!v.isObject() || v.getProperty("patch", juce::var()).isVoid()) { err = "Code introuvable. Demande a ton pote de re-publier."; return false; }
        patchOut = v.getProperty("patch", juce::var());
        titleOut = v.getProperty("title", "").toString();
        return true;
    }
};
