#pragma once
#include <JuceHeader.h>
#include <map>
#include <utility>
#include <vector>

// Client HTTP minimal vers l'API PatchDrop (gg.trenas.fr/patchdrop).
// Utilisé par les boutons "Partager via code" / "Ouvrir via code" et le navigateur Presets en ligne.
// Aucune dépendance externe : juce::URL + JSON.
struct ShareClient
{
    struct OnlinePresetSummary
    {
        juce::String code;
        juce::String title;
        juce::String author;
        juce::String description;
        juce::String category;
        juce::String categoryLabel;
        juce::String createdAt;
        juce::StringArray tags;
        juce::StringArray plugins;
        int downloads = 0;
    };

    struct CatalogResult
    {
        bool ok = false;
        juce::String error;
        std::vector<OnlinePresetSummary> presets;
        int total = 0;
        int allTotal = 0;
        std::map<juce::String, int> categoryCounts;
        std::vector<std::pair<juce::String, int>> popularTags;
    };

    juce::String apiBase = "https://gg.trenas.fr/patchdrop";

    // Publie le var patch (graphToVar). Retourne le code, ou "" + err rempli.
    static juce::String publish(const juce::var& patchJson, const juce::String& title,
                                     const juce::String& author, const juce::String& desc,
                                     const juce::String& apiBase, juce::String& err, bool isPublic = true)
    {
        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty("title", title);
        body->setProperty("author", author);
        body->setProperty("description", desc);
        body->setProperty("isPublic", isPublic);
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
        if (in == nullptr) { err = "Could not connect to PatchDrop. You can still use Export to save a file."; return {}; }
        juce::String txt = in->readEntireStreamAsString();
        if (status < 200 || status >= 300) { err = "HTTP " + juce::String(status) + " : " + txt.substring(0, 200); return {}; }
        auto v = juce::JSON::parse(txt);
        juce::String code = v.getProperty("code", "").toString();
        if (code.isEmpty()) { err = "Unexpected server response: " + txt.substring(0, 200); return {}; }
        return code;
    }

    // Récupère {title, patch} depuis un code. Retourne true si ok.
    static bool fetch(const juce::String& code, const juce::String& apiBase,
                      juce::var& patchOut, juce::String& titleOut, juce::String& err)
    {
        OnlinePresetSummary meta;
        return fetchWithMeta(code, apiBase, patchOut, meta, err)
            && ([&]() { titleOut = meta.title; return true; })();
    }

    // Récupère patch + métadonnées complètes depuis un code.
    static bool fetchWithMeta(const juce::String& code, const juce::String& apiBase,
                              juce::var& patchOut, OnlinePresetSummary& metaOut, juce::String& err)
    {
        juce::String c = code.toUpperCase().trim();
        juce::URL url(apiBase.trimCharactersAtEnd("/") + "/api/p/" + c);
        int status = 0;
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(15000)
                        .withStatusCode(&status);
        std::unique_ptr<juce::InputStream> in(url.createInputStream(opts));
        if (in == nullptr) { err = "Could not connect to PatchDrop."; return false; }
        auto v = juce::JSON::parse(in->readEntireStreamAsString());
        if (!v.isObject() || v.getProperty("patch", juce::var()).isVoid())
        {
            err = v.getProperty("error", "This code was not found. Check it and try again.").toString();
            return false;
        }
        patchOut = v.getProperty("patch", juce::var());
        metaOut.code = v.getProperty("code", c).toString().toUpperCase();
        metaOut.title = v.getProperty("title", "").toString();
        metaOut.author = v.getProperty("author", "").toString();
        metaOut.description = v.getProperty("description", "").toString();
        metaOut.category = v.getProperty("category", "").toString();
        metaOut.createdAt = v.getProperty("createdAt", "").toString();
        metaOut.downloads = static_cast<int>(v.getProperty("downloads", 0));
        if (auto* tagsArr = v.getProperty("tags", juce::var()).getArray())
            for (const auto& t : *tagsArr)
                if (t.toString().isNotEmpty()) metaOut.tags.add(t.toString());
        if (auto* pluginsArr = v.getProperty("plugins", juce::var()).getArray())
            for (const auto& p : *pluginsArr)
                if (p.toString().isNotEmpty()) metaOut.plugins.add(p.toString());
        return true;
    }

    // Liste le catalogue public PatchDrop avec filtres / recherche / tri.
    static CatalogResult fetchCatalog(const juce::String& apiBase,
                                      const juce::String& query = {},
                                      const juce::String& category = {},
                                      const juce::String& tag = {},
                                      const juce::String& sort = "newest",
                                      int limit = 100)
    {
        CatalogResult res;
        juce::URL url(apiBase.trimCharactersAtEnd("/") + "/api/list");
        url = url.withParameter("limit", juce::String(juce::jlimit(1, 100, limit)));
        if (query.trim().isNotEmpty())
            url = url.withParameter("q", query.trim());
        if (category.trim().isNotEmpty() && !category.equalsIgnoreCase("all"))
            url = url.withParameter("category", category.trim().toLowerCase());
        if (tag.trim().isNotEmpty())
            url = url.withParameter("tag", tag.trim().toLowerCase());
        if (sort.trim().isNotEmpty())
            url = url.withParameter("sort", sort.trim().toLowerCase());

        int status = 0;
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(12000)
                        .withStatusCode(&status);
        std::unique_ptr<juce::InputStream> in(url.createInputStream(opts));
        if (in == nullptr)
        {
            res.error = "Could not reach PatchDrop server.";
            return res;
        }
        const auto txt = in->readEntireStreamAsString();
        if (status != 0 && (status < 200 || status >= 300))
        {
            res.error = "HTTP " + juce::String(status);
            return res;
        }

        const auto v = juce::JSON::parse(txt);
        auto* obj = v.getDynamicObject();
        if (obj == nullptr)
        {
            res.error = "Invalid catalog response.";
            return res;
        }

        res.total = static_cast<int>(obj->getProperty("total"));
        res.allTotal = static_cast<int>(obj->getProperty("allTotal"));

        if (auto* cats = obj->getProperty("categories").getDynamicObject())
        {
            for (const auto& kv : cats->getProperties())
                res.categoryCounts[kv.name.toString()] = static_cast<int>(kv.value);
        }

        if (auto* pTags = obj->getProperty("popularTags").getArray())
        {
            for (const auto& item : *pTags)
            {
                if (auto* to = item.getDynamicObject())
                {
                    const auto t = to->getProperty("tag").toString();
                    const int c = static_cast<int>(to->getProperty("count"));
                    if (t.isNotEmpty())
                        res.popularTags.emplace_back(t, c);
                }
            }
        }

        if (auto* arr = obj->getProperty("presets").getArray())
        {
            res.presets.reserve(static_cast<size_t>(arr->size()));
            for (const auto& item : *arr)
            {
                auto* po = item.getDynamicObject();
                if (po == nullptr)
                    continue;
                OnlinePresetSummary s;
                s.code = po->getProperty("code").toString().toUpperCase();
                s.title = po->getProperty("title").toString();
                s.author = po->getProperty("author").toString();
                s.description = po->getProperty("description").toString();
                s.category = po->getProperty("category").toString();
                s.categoryLabel = po->getProperty("categoryLabel").toString();
                s.createdAt = po->getProperty("createdAt").toString();
                s.downloads = static_cast<int>(po->getProperty("downloads"));
                if (auto* tArr = po->getProperty("tags").getArray())
                    for (const auto& t : *tArr)
                        if (t.toString().isNotEmpty()) s.tags.add(t.toString());
                if (auto* pArr = po->getProperty("plugins").getArray())
                    for (const auto& p : *pArr)
                        if (p.toString().isNotEmpty()) s.plugins.add(p.toString());
                if (s.code.isNotEmpty())
                    res.presets.push_back(std::move(s));
            }
        }

        res.ok = true;
        return res;
    }
};
