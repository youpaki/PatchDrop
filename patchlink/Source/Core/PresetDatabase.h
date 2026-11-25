#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace patchdb
{
struct LocalPreset
{
    juce::String id;
    juce::String title;
    juce::String folder;
    juce::String author;
    juce::String description;
    juce::String category;
    juce::String sourceCode;
    juce::String fileName;
    juce::String createdAt;
    juce::String updatedAt;
    juce::StringArray tags;
    juce::StringArray plugins;
    int pluginCount = 0;
    int useCount = 0;
    bool favorite = false;
};

enum class LocalSort
{
    newest = 0,
    nameAsc,
    nameDesc,
    mostUsed,
    mostPlugins,
    favoritesFirst
};

struct LocalQuery
{
    juce::String search;
    juce::String folder { "__ALL__" }; // "__ALL__", "__FAV__", "__UNSORTED__", or exact folder name
    juce::String category;             // "" / "all" or category id
    juce::String tag;                  // "" or "#tag"
    LocalSort sort = LocalSort::newest;
};

inline juce::String normalizeTag(const juce::String& raw)
{
    auto clean = raw.trim().toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789_-");
    if (clean.isEmpty())
        return {};
    return "#" + clean.substring(0, 24);
}

inline juce::StringArray parseTagsString(const juce::String& raw)
{
    juce::StringArray out;
    juce::StringArray tokens;
    tokens.addTokens(raw, " ,;\t\r\n", "");
    for (const auto& tok : tokens)
    {
        const auto t = normalizeTag(tok);
        if (t.isNotEmpty() && !out.contains(t))
            out.add(t);
    }
    return out;
}

inline juce::StringArray extractPluginNamesFromPatch(const juce::var& patchVar)
{
    juce::StringArray names;
    auto* root = patchVar.getDynamicObject();
    if (root == nullptr)
        return names;
    if (auto* arr = root->getProperty("nodes").getArray())
    {
        for (const auto& node : *arr)
        {
            if (auto* no = node.getDynamicObject())
            {
                juce::String n;
                if (auto* ids = no->getProperty("ids").getDynamicObject())
                    n = ids->getProperty("name").toString().trim();
                if (n.isEmpty())
                    n = no->getProperty("name").toString().trim();
                if (n.isNotEmpty())
                    names.add(n);
            }
        }
    }
    return names;
}

inline juce::String inferCategoryFromMeta(const juce::String& title,
                                          const juce::String& description,
                                          const juce::StringArray& plugins)
{
    const auto hay = (title + " " + description + " " + plugins.joinIntoString(" ")).toLowerCase();
    auto hasAny = [&](std::initializer_list<const char*> needles) {
        for (const char* s : needles)
            if (hay.contains(s)) return true;
        return false;
    };
    if (hasAny({ "vocal", "vox", "chant", "voix", "rap", "auto-tune", "autotune", "de-ess", "deess", "cla vocals" }))
        return "vocals";
    if (hasAny({ "master", "mix bus", "mixbus", "bus", "glue", "pro-l", "ozone", "limiter", "loudness", "ssl" }))
        return "master";
    if (hasAny({ "drum", "808", "kick", "snare", "hat", "perc", "bass", "sub", "punch", "transient" }))
        return "drums";
    if (hasAny({ "synth", "pad", "lead", "arp", "serum", "vital", "massive", "omnisphere", "diva", "chord" }))
        return "synths";
    if (hasAny({ "guitar", "guitare", "amp", "cabinet", "piano", "keys", "rhodes", "clav", "amplitube", "neural" }))
        return "inst";
    return "fx";
}

inline juce::String categoryLabel(const juce::String& catId, bool french = false)
{
    const auto c = catId.trim().toLowerCase();
    if (c == "vocals") return french ? "Voix" : "Vocals";
    if (c == "master") return "Mix Bus & Master";
    if (c == "drums")  return "Drums & 808";
    if (c == "synths") return french ? "Synthés & Pads" : "Synths & Pads";
    if (c == "inst")   return french ? "Guitares & Claviers" : "Guitars & Keys";
    return french ? "FX & Créatif" : "FX & Creative";
}

class PresetDatabase
{
public:
    explicit PresetDatabase(juce::File customRoot = {})
        : rootDir(customRoot.getFullPathName().isNotEmpty()
                      ? customRoot
                      : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("PatchLink")
                            .getChildFile("PresetLibrary"))
    {
    }

    juce::File getRootDir() const { return rootDir; }
    juce::File getPresetsDir() const { return rootDir.getChildFile("presets"); }
    juce::File getIndexFile() const { return rootDir.getChildFile("index.json"); }

    void load()
    {
        folders.clear();
        presets.clear();

        rootDir.createDirectory();
        getPresetsDir().createDirectory();

        const auto indexFile = getIndexFile();
        if (indexFile.existsAsFile())
        {
            const auto parsed = juce::JSON::parse(indexFile.loadFileAsString());
            if (auto* obj = parsed.getDynamicObject())
            {
                if (auto* fArr = obj->getProperty("folders").getArray())
                    for (const auto& f : *fArr)
                    {
                        const auto fn = f.toString().trim();
                        if (fn.isNotEmpty() && !folders.contains(fn))
                            folders.add(fn);
                    }

                if (auto* pArr = obj->getProperty("presets").getArray())
                    for (const auto& item : *pArr)
                        if (auto* po = item.getDynamicObject())
                        {
                            LocalPreset p = presetFromObject(po);
                            if (p.id.isNotEmpty())
                                presets.push_back(std::move(p));
                        }
            }
        }
        else
        {
            seedDefaultFolders();
            save();
        }

        // Auto-recover any orphaned .patchlink files placed directly inside presets/
        bool recoveredAny = false;
        for (const auto& f : getPresetsDir().findChildFiles(juce::File::findFiles, false, "*.patchlink"))
        {
            const auto fn = f.getFileName();
            const bool known = std::any_of(presets.begin(), presets.end(),
                                           [&](const LocalPreset& p) { return p.fileName == fn; });
            if (known)
                continue;

            const auto pv = juce::JSON::parse(f.loadFileAsString());
            if (!pv.isObject())
                continue;

            LocalPreset meta;
            meta.id = f.getFileNameWithoutExtension();
            if (auto* po = pv.getDynamicObject())
                meta.title = po->getProperty("name").toString().trim();
            if (meta.title.isEmpty())
                meta.title = meta.id;
            meta.fileName = fn;
            meta.plugins = extractPluginNamesFromPatch(pv);
            meta.pluginCount = meta.plugins.size();
            meta.category = inferCategoryFromMeta(meta.title, {}, meta.plugins);
            meta.createdAt = f.getLastModificationTime().toISO8601(true);
            meta.updatedAt = meta.createdAt;
            presets.push_back(std::move(meta));
            recoveredAny = true;
        }
        if (recoveredAny)
            save();
    }

    bool save() const
    {
        rootDir.createDirectory();
        getPresetsDir().createDirectory();

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("version", 1);

        juce::Array<juce::var> jFolders;
        for (const auto& f : folders)
            jFolders.add(f);
        root->setProperty("folders", jFolders);

        juce::Array<juce::var> jPresets;
        for (const auto& p : presets)
            jPresets.add(presetToVar(p));
        root->setProperty("presets", jPresets);

        const auto indexFile = getIndexFile();
        const auto tmpFile = rootDir.getChildFile("index.json.tmp");
        if (!tmpFile.replaceWithText(juce::JSON::toString(juce::var(root.get()), true)))
            return false;
        return tmpFile.moveFileTo(indexFile);
    }

    juce::String savePreset(const juce::var& patchVar, LocalPreset meta, bool overwriteExistingId = false)
    {
        if (!patchVar.isObject())
            return {};

        rootDir.createDirectory();
        getPresetsDir().createDirectory();

        const auto nowIso = juce::Time::getCurrentTime().toISO8601(true);
        if (meta.id.isEmpty())
            meta.id = makeUniqueId();
        meta.id = meta.id.retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");
        if (meta.id.isEmpty())
            meta.id = makeUniqueId();

        meta.title = meta.title.trim();
        if (meta.title.isEmpty())
        {
            if (auto* po = patchVar.getDynamicObject())
                meta.title = po->getProperty("name").toString().trim();
            if (meta.title.isEmpty())
                meta.title = "Untitled Preset";
        }
        meta.folder = meta.folder.trim();
        if (meta.folder.isNotEmpty() && !folders.contains(meta.folder))
            folders.add(meta.folder);

        meta.plugins = extractPluginNamesFromPatch(patchVar);
        meta.pluginCount = meta.plugins.size();
        if (meta.category.trim().isEmpty())
            meta.category = inferCategoryFromMeta(meta.title, meta.description, meta.plugins);
        else
            meta.category = meta.category.trim().toLowerCase();

        // Normalize tags
        juce::StringArray normTags;
        for (const auto& t : meta.tags)
        {
            const auto nt = normalizeTag(t);
            if (nt.isNotEmpty() && !normTags.contains(nt))
                normTags.add(nt);
        }
        meta.tags = normTags;

        if (meta.fileName.isEmpty())
            meta.fileName = meta.id + ".patchlink";

        // Keep chain name synced in stored .patchlink file
        juce::var patchCopy = patchVar.clone();
        if (auto* po = patchCopy.getDynamicObject())
            po->setProperty("name", meta.title);

        const auto targetFile = getPresetsDir().getChildFile(meta.fileName);
        if (!targetFile.replaceWithText(juce::JSON::toString(patchCopy, true)))
            return {};

        auto it = std::find_if(presets.begin(), presets.end(),
                               [&](const LocalPreset& p) { return p.id == meta.id; });
        if (it != presets.end() && overwriteExistingId)
        {
            if (meta.createdAt.isEmpty())
                meta.createdAt = it->createdAt;
            meta.useCount = juce::jmax(meta.useCount, it->useCount);
            meta.updatedAt = nowIso;
            *it = std::move(meta);
        }
        else
        {
            if (it != presets.end())
            {
                meta.id = makeUniqueId();
                meta.fileName = meta.id + ".patchlink";
                const auto altFile = getPresetsDir().getChildFile(meta.fileName);
                targetFile.moveFileTo(altFile);
            }
            if (meta.createdAt.isEmpty())
                meta.createdAt = nowIso;
            meta.updatedAt = nowIso;
            presets.push_back(std::move(meta));
        }

        save();
        return presets.back().id;
    }

    bool loadPresetPatch(const juce::String& id, juce::var& outPatch) const
    {
        const auto* p = findById(id);
        if (p == nullptr)
            return false;
        const auto file = getPresetsDir().getChildFile(p->fileName);
        if (!file.existsAsFile())
            return false;
        outPatch = juce::JSON::parse(file.loadFileAsString());
        return outPatch.isObject();
    }

    bool deletePreset(const juce::String& id)
    {
        auto it = std::find_if(presets.begin(), presets.end(),
                               [&](const LocalPreset& p) { return p.id == id; });
        if (it == presets.end())
            return false;
        const auto file = getPresetsDir().getChildFile(it->fileName);
        if (file.existsAsFile())
            file.deleteFile();
        presets.erase(it);
        save();
        return true;
    }

    bool toggleFavorite(const juce::String& id)
    {
        auto* p = findByIdMutable(id);
        if (p == nullptr)
            return false;
        p->favorite = !p->favorite;
        p->updatedAt = juce::Time::getCurrentTime().toISO8601(true);
        save();
        return p->favorite;
    }

    bool movePresetToFolder(const juce::String& id, const juce::String& newFolder)
    {
        auto* p = findByIdMutable(id);
        if (p == nullptr)
            return false;
        const auto cleanFolder = newFolder.trim();
        if (cleanFolder.isNotEmpty() && !folders.contains(cleanFolder))
            folders.add(cleanFolder);
        p->folder = cleanFolder;
        p->updatedAt = juce::Time::getCurrentTime().toISO8601(true);
        save();
        return true;
    }

    bool updatePresetMeta(const juce::String& id,
                          const juce::String& title,
                          const juce::String& folder,
                          const juce::String& author,
                          const juce::String& description,
                          const juce::String& category,
                          const juce::StringArray& tags,
                          bool favorite)
    {
        auto* p = findByIdMutable(id);
        if (p == nullptr)
            return false;
        if (title.trim().isNotEmpty())
            p->title = title.trim();
        p->folder = folder.trim();
        if (p->folder.isNotEmpty() && !folders.contains(p->folder))
            folders.add(p->folder);
        p->author = author.trim();
        p->description = description.trim();
        p->category = category.trim().isNotEmpty() ? category.trim().toLowerCase()
                                                   : inferCategoryFromMeta(p->title, p->description, p->plugins);
        juce::StringArray normTags;
        for (const auto& t : tags)
        {
            const auto nt = normalizeTag(t);
            if (nt.isNotEmpty() && !normTags.contains(nt))
                normTags.add(nt);
        }
        p->tags = normTags;
        p->favorite = favorite;
        p->updatedAt = juce::Time::getCurrentTime().toISO8601(true);
        save();
        return true;
    }

    void recordUse(const juce::String& id)
    {
        if (auto* p = findByIdMutable(id))
        {
            ++p->useCount;
            p->updatedAt = juce::Time::getCurrentTime().toISO8601(true);
            save();
        }
    }

    bool addFolder(const juce::String& name)
    {
        const auto clean = name.trim();
        if (clean.isEmpty() || clean.startsWith("__") || folders.contains(clean))
            return false;
        folders.add(clean);
        save();
        return true;
    }

    bool renameFolder(const juce::String& oldName, const juce::String& newName)
    {
        const auto oldClean = oldName.trim();
        const auto newClean = newName.trim();
        if (oldClean.isEmpty() || newClean.isEmpty() || newClean.startsWith("__") || oldClean == newClean)
            return false;
        const int idx = folders.indexOf(oldClean);
        if (idx < 0 || folders.contains(newClean))
            return false;
        folders.set(idx, newClean);
        for (auto& p : presets)
            if (p.folder == oldClean)
                p.folder = newClean;
        save();
        return true;
    }

    bool deleteFolder(const juce::String& name)
    {
        const auto clean = name.trim();
        const int idx = folders.indexOf(clean);
        if (idx < 0)
            return false;
        folders.remove(idx);
        for (auto& p : presets)
            if (p.folder == clean)
                p.folder.clear();
        save();
        return true;
    }

    bool hasSourceCode(const juce::String& code) const
    {
        const auto c = code.trim().toUpperCase();
        if (c.isEmpty())
            return false;
        return std::any_of(presets.begin(), presets.end(),
                           [&](const LocalPreset& p) { return p.sourceCode.equalsIgnoreCase(c); });
    }

    const LocalPreset* findById(const juce::String& id) const
    {
        for (const auto& p : presets)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    const juce::StringArray& getFolders() const { return folders; }
    const std::vector<LocalPreset>& getAllPresets() const { return presets; }

    int countInFolder(const juce::String& folderToken) const
    {
        if (folderToken == "__ALL__")
            return static_cast<int>(presets.size());
        if (folderToken == "__FAV__")
            return static_cast<int>(std::count_if(presets.begin(), presets.end(),
                                                  [](const LocalPreset& p) { return p.favorite; }));
        if (folderToken == "__UNSORTED__")
            return static_cast<int>(std::count_if(presets.begin(), presets.end(),
                                                  [](const LocalPreset& p) { return p.folder.isEmpty(); }));
        return static_cast<int>(std::count_if(presets.begin(), presets.end(),
                                              [&](const LocalPreset& p) { return p.folder == folderToken; }));
    }

    std::vector<std::pair<juce::String, int>> getPopularTags(int maxTags = 18) const
    {
        std::map<juce::String, int> counts;
        for (const auto& p : presets)
            for (const auto& t : p.tags)
                if (t.isNotEmpty())
                    ++counts[t];

        std::vector<std::pair<juce::String, int>> vec(counts.begin(), counts.end());
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        if (static_cast<int>(vec.size()) > maxTags)
            vec.resize(static_cast<size_t>(maxTags));
        return vec;
    }

    std::vector<LocalPreset> query(const LocalQuery& q) const
    {
        juce::StringArray searchTerms;
        searchTerms.addTokens(q.search.trim().toLowerCase(), " \t", "");
        searchTerms.removeEmptyStrings();

        const auto tagFilter = q.tag.trim().isNotEmpty() ? normalizeTag(q.tag) : juce::String();
        const auto catFilter = q.category.trim().toLowerCase();

        std::vector<LocalPreset> out;
        for (const auto& p : presets)
        {
            if (q.folder == "__FAV__" && !p.favorite)
                continue;
            else if (q.folder == "__UNSORTED__" && p.folder.isNotEmpty())
                continue;
            else if (q.folder.isNotEmpty() && !q.folder.startsWith("__") && p.folder != q.folder)
                continue;

            if (catFilter.isNotEmpty() && catFilter != "all" && p.category.toLowerCase() != catFilter)
                continue;

            if (tagFilter.isNotEmpty() && !p.tags.contains(tagFilter))
                continue;

            if (!searchTerms.isEmpty())
            {
                const auto hay = (p.title + " " + p.author + " " + p.description + " " + p.folder + " "
                                  + p.sourceCode + " " + p.category + " " + p.plugins.joinIntoString(" ")
                                  + " " + p.tags.joinIntoString(" "))
                                     .toLowerCase();
                bool match = true;
                for (const auto& term : searchTerms)
                {
                    if (!hay.contains(term))
                    {
                        match = false;
                        break;
                    }
                }
                if (!match)
                    continue;
            }

            out.push_back(p);
        }

        std::sort(out.begin(), out.end(), [&](const LocalPreset& a, const LocalPreset& b) {
            switch (q.sort)
            {
                case LocalSort::nameAsc:
                {
                    const int cmp = a.title.compareIgnoreCase(b.title);
                    return cmp != 0 ? cmp < 0 : a.createdAt > b.createdAt;
                }
                case LocalSort::nameDesc:
                {
                    const int cmp = a.title.compareIgnoreCase(b.title);
                    return cmp != 0 ? cmp > 0 : a.createdAt > b.createdAt;
                }
                case LocalSort::mostUsed:
                    if (a.useCount != b.useCount) return a.useCount > b.useCount;
                    return a.updatedAt > b.updatedAt;
                case LocalSort::mostPlugins:
                    if (a.pluginCount != b.pluginCount) return a.pluginCount > b.pluginCount;
                    return a.title.compareIgnoreCase(b.title) < 0;
                case LocalSort::favoritesFirst:
                    if (a.favorite != b.favorite) return a.favorite && !b.favorite;
                    return a.createdAt > b.createdAt;
                case LocalSort::newest:
                default:
                    if (a.createdAt != b.createdAt) return a.createdAt > b.createdAt;
                    return a.id > b.id;
            }
        });

        return out;
    }

private:
    juce::File rootDir;
    juce::StringArray folders;
    std::vector<LocalPreset> presets;

    void seedDefaultFolders()
    {
        for (const char* name : { "Vocals", "Mix Bus & Master", "Drums & 808",
                                  "Synths & Pads", "Guitars & Keys", "FX & Creative", "Downloaded" })
            folders.add(name);
    }

    juce::String makeUniqueId() const
    {
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d%H%M%S");
        auto& rng = juce::Random::getSystemRandom();
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            const auto candidate = stamp + "_" + juce::String::formatted("%04x", rng.nextInt(0x10000));
            if (findById(candidate) == nullptr)
                return candidate;
        }
        return stamp + "_" + juce::String(presets.size() + 1);
    }

    LocalPreset* findByIdMutable(const juce::String& id)
    {
        for (auto& p : presets)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    static LocalPreset presetFromObject(juce::DynamicObject* po)
    {
        LocalPreset p;
        p.id = po->getProperty("id").toString();
        p.title = po->getProperty("title").toString();
        p.folder = po->getProperty("folder").toString();
        p.author = po->getProperty("author").toString();
        p.description = po->getProperty("description").toString();
        p.category = po->getProperty("category").toString();
        p.sourceCode = po->getProperty("sourceCode").toString();
        p.fileName = po->getProperty("fileName").toString();
        p.createdAt = po->getProperty("createdAt").toString();
        p.updatedAt = po->getProperty("updatedAt").toString();
        p.pluginCount = static_cast<int>(po->getProperty("pluginCount"));
        p.useCount = static_cast<int>(po->getProperty("useCount"));
        p.favorite = static_cast<bool>(po->getProperty("favorite"));

        if (auto* tArr = po->getProperty("tags").getArray())
            for (const auto& t : *tArr)
                if (t.toString().isNotEmpty()) p.tags.add(t.toString());

        if (auto* pArr = po->getProperty("plugins").getArray())
            for (const auto& n : *pArr)
                if (n.toString().isNotEmpty()) p.plugins.add(n.toString());

        if (p.fileName.isEmpty() && p.id.isNotEmpty())
            p.fileName = p.id + ".patchlink";
        if (p.pluginCount == 0 && !p.plugins.isEmpty())
            p.pluginCount = p.plugins.size();
        return p;
    }

    static juce::var presetToVar(const LocalPreset& p)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("id", p.id);
        o->setProperty("title", p.title);
        o->setProperty("folder", p.folder);
        o->setProperty("author", p.author);
        o->setProperty("description", p.description);
        o->setProperty("category", p.category);
        o->setProperty("sourceCode", p.sourceCode);
        o->setProperty("fileName", p.fileName);
        o->setProperty("createdAt", p.createdAt);
        o->setProperty("updatedAt", p.updatedAt);
        o->setProperty("pluginCount", p.pluginCount);
        o->setProperty("useCount", p.useCount);
        o->setProperty("favorite", p.favorite);

        juce::Array<juce::var> jTags;
        for (const auto& t : p.tags) jTags.add(t);
        o->setProperty("tags", jTags);

        juce::Array<juce::var> jPlugins;
        for (const auto& pl : p.plugins) jPlugins.add(pl);
        o->setProperty("plugins", jPlugins);
        return juce::var(o.get());
    }
};
} // namespace patchdb
