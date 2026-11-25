#include "PresetHubView.h"
#include "../Core/I18n.h"
#include <algorithm>
#include <cmath>
#include <thread>

namespace
{
juce::String normalizeLookupKey(const juce::String& raw)
{
    return raw.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

juce::String formatShortDate(const juce::String& iso)
{
    if (iso.length() >= 10)
        return iso.substring(0, 10);
    return iso;
}

const char* const kCategoryOrder[] = { "vocals", "master", "drums", "synths", "inst", "fx" };

// Studio Save / Edit Preset modal (displayed inside PatchLinkEditor::overlay).
class SavePresetModal final : public ModalOverlay
{
public:
    SavePresetModal(bool isEditMode,
                    const patchdb::LocalPreset& initial,
                    const juce::StringArray& folders,
                    const juce::StringArray& pluginsList)
        : isEdit(isEditMode), presetId(initial.id), existingCreated(initial.createdAt), existingCode(initial.sourceCode)
    {
        titleLabel.setText(isEdit ? i18n::tr("Edit preset details", "Modifier le preset")
                                  : i18n::tr("Save chain to Local Library", "Enregistrer dans la bibliothèque"),
                           juce::dontSendNotification);
        titleLabel.setFont(theme::font(20.0f, true));
        addAndMakeVisible(titleLabel);

        subtitleLabel.setText(juce::String(pluginsList.size())
                                  + i18n::tr(" plugin(s): ", " plugin(s) : ")
                                  + (pluginsList.isEmpty() ? i18n::tr("Empty chain", "Chaîne vide")
                                                           : pluginsList.joinIntoString(", ")),
                              juce::dontSendNotification);
        subtitleLabel.setFont(theme::font(12.0f));
        subtitleLabel.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(subtitleLabel);

        closeBtn.setTooltip(i18n::tr("Cancel (Esc)", "Annuler (Échap)"));
        closeBtn.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(closeBtn);

        auto setupCap = [this](juce::Label& l, const juce::String& txt) {
            l.setText(txt, juce::dontSendNotification);
            l.setFont(theme::font(11.0f, true));
            l.setColour(juce::Label::textColourId, theme::muted);
            addAndMakeVisible(l);
        };
        setupCap(capTitle, i18n::tr("PRESET NAME", "NOM DU PRESET"));
        setupCap(capFolder, i18n::tr("FOLDER", "DOSSIER"));
        setupCap(capCategory, i18n::tr("CATEGORY", "CATÉGORIE"));
        setupCap(capAuthor, i18n::tr("AUTHOR", "AUTEUR"));
        setupCap(capTags, i18n::tr("TAGS (#vocals #warm)", "TAGS (#voix #chaud)"));
        setupCap(capDesc, i18n::tr("NOTES / DESCRIPTION", "NOTES / DESCRIPTION"));

        auto setupEd = [this](juce::TextEditor& ed, const juce::String& val, const juce::String& placeholder) {
            ed.setFont(theme::font(13.0f));
            ed.setIndents(10, 0);
            ed.setText(val, false);
            ed.setTextToShowWhenEmpty(placeholder, theme::faint);
            addAndMakeVisible(ed);
        };
        setupEd(titleEd, initial.title, i18n::tr("Lead Vocal Rack", "Chaîne Voix Lead"));
        setupEd(authorEd, initial.author, i18n::tr("Optional", "Optionnel"));
        setupEd(tagsEd, initial.tags.joinIntoString(" "), "#vocals #mix");
        setupEd(descEd, initial.description, i18n::tr("Signal notes or sweet spots...", "Notes de réglages ou d'utilisation..."));
        descEd.setMultiLine(true);
        descEd.setReturnKeyStartsNewLine(true);
        descEd.setIndents(10, 8);

        folderBox.setEditableText(true);
        folderBox.addItem(i18n::tr("(Unsorted)", "(Non classé)"), 1);
        int fid = 2;
        int selectedFolderId = 1;
        for (const auto& f : folders)
        {
            folderBox.addItem(f, fid);
            if (initial.folder == f)
                selectedFolderId = fid;
            ++fid;
        }
        if (initial.folder.isNotEmpty() && selectedFolderId == 1)
            folderBox.setText(initial.folder, juce::dontSendNotification);
        else
            folderBox.setSelectedId(selectedFolderId, juce::dontSendNotification);
        addAndMakeVisible(folderBox);

        int cid = 1;
        int selectedCatId = 6;
        for (const char* cat : kCategoryOrder)
        {
            categoryBox.addItem(patchdb::categoryLabel(cat, i18n::isFrench()), cid);
            if (initial.category.equalsIgnoreCase(cat))
                selectedCatId = cid;
            ++cid;
        }
        categoryBox.setSelectedId(selectedCatId, juce::dontSendNotification);
        addAndMakeVisible(categoryBox);

        favToggle.setButtonText(i18n::tr("Mark as Favorite ★", "Ajouter aux Favoris ★"));
        favToggle.setToggleState(initial.favorite, juce::dontSendNotification);
        addAndMakeVisible(favToggle);

        cancelBtn.setButtonText(i18n::tr("Cancel", "Annuler"));
        cancelBtn.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(cancelBtn);

        saveBtn.setButtonText(isEdit ? i18n::tr("Save changes", "Mettre à jour")
                                     : i18n::tr("Save preset", "Enregistrer"));
        saveBtn.onClick = [this] { commit(); };
        addAndMakeVisible(saveBtn);

        titleEd.onReturnKey = [this] { commit(); };
    }

    juce::Rectangle<int> getCardBounds() const override
    {
        const int w = juce::jmin(620, getWidth() - 48);
        const int h = juce::jmin(520, getHeight() - 48);
        return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
    }

    std::function<void(patchdb::LocalPreset meta)> onSubmit;

private:
    bool isEdit = false;
    juce::String presetId, existingCreated, existingCode;

    juce::Label titleLabel, subtitleLabel;
    juce::Label capTitle, capFolder, capCategory, capAuthor, capTags, capDesc;
    juce::TextEditor titleEd, authorEd, tagsEd, descEd;
    juce::ComboBox folderBox, categoryBox;
    juce::ToggleButton favToggle;
    PlButton closeBtn { "", Icon::close, PlButton::Style::ghost };
    PlButton cancelBtn { "Cancel", Icon::none, PlButton::Style::ghost };
    PlButton saveBtn { "Save preset", Icon::save, PlButton::Style::primary };

    void commit()
    {
        patchdb::LocalPreset meta;
        meta.id = presetId;
        meta.createdAt = existingCreated;
        meta.sourceCode = existingCode;
        meta.title = titleEd.getText().trim();
        if (meta.title.isEmpty())
            meta.title = i18n::tr("Untitled Preset", "Preset sans nom");

        auto rawFolder = folderBox.getText().trim();
        if (rawFolder == i18n::tr("(Unsorted)", "(Non classé)") || rawFolder == "(Unsorted)" || rawFolder == "(Non classé)")
            rawFolder.clear();
        meta.folder = rawFolder;

        const int catIdx = juce::jlimit(0, 5, categoryBox.getSelectedId() - 1);
        meta.category = kCategoryOrder[catIdx];
        meta.author = authorEd.getText().trim();
        meta.tags = patchdb::parseTagsString(tagsEd.getText());
        meta.description = descEd.getText().trim();
        meta.favorite = favToggle.getToggleState();
        if (onSubmit)
            onSubmit(std::move(meta));
    }

    void layoutCard(juce::Rectangle<int> card) override
    {
        auto r = card.reduced(24, 20);
        closeBtn.setBounds(r.getRight() - 32, r.getY(), 32, 32);
        titleLabel.setBounds(r.removeFromTop(30).withTrimmedRight(40));
        subtitleLabel.setBounds(r.removeFromTop(20));
        r.removeFromTop(12);

        auto footer = r.removeFromBottom(40);
        favToggle.setBounds(footer.removeFromLeft(220));
        saveBtn.setBounds(footer.removeFromRight(146).reduced(0, 2));
        footer.removeFromRight(8);
        cancelBtn.setBounds(footer.removeFromRight(94).reduced(0, 2));
        r.removeFromBottom(12);

        capTitle.setBounds(r.removeFromTop(18));
        titleEd.setBounds(r.removeFromTop(34));
        r.removeFromTop(10);

        auto row1 = r.removeFromTop(54);
        auto left1 = row1.removeFromLeft(row1.getWidth() / 2 - 6);
        auto right1 = row1.withTrimmedLeft(12);
        capFolder.setBounds(left1.removeFromTop(18));
        folderBox.setBounds(left1.removeFromTop(34));
        capCategory.setBounds(right1.removeFromTop(18));
        categoryBox.setBounds(right1.removeFromTop(34));
        r.removeFromTop(10);

        auto row2 = r.removeFromTop(54);
        auto left2 = row2.removeFromLeft(row2.getWidth() / 2 - 6);
        auto right2 = row2.withTrimmedLeft(12);
        capAuthor.setBounds(left2.removeFromTop(18));
        authorEd.setBounds(left2.removeFromTop(34));
        capTags.setBounds(right2.removeFromTop(18));
        tagsEd.setBounds(right2.removeFromTop(34));
        r.removeFromTop(10);

        capDesc.setBounds(r.removeFromTop(18));
        r.removeFromTop(2);
        descEd.setBounds(r);
    }
};
} // namespace

//==============================================================================
class PresetHubView::SidebarContent final : public juce::Component
{
public:
    explicit SidebarContent(PresetHubView& ownerRef) : owner(ownerRef) {}

    struct RowItem
    {
        enum class Type { sectionHeader, smartOrFolder, category, tagChip };
        Type type = Type::smartOrFolder;
        juce::String key;
        juce::String label;
        int count = 0;
        bool active = false;
        bool hasStar = false;
        bool hasFolderIcon = false;
        bool canModifyFolder = false;
        juce::Rectangle<int> bounds;
    };

    void rebuild()
    {
        rows.clear();
        newFolderRect = {};

        int y = 8;
        const int w = juce::jmax(160, getWidth());
        const int pad = 10;
        const int rowW = w - pad * 2;

        if (owner.scope == Scope::local)
        {
            auto addSmart = [&](const juce::String& key, const juce::String& label, int count, bool star, bool folderIc) {
                RowItem item;
                item.type = RowItem::Type::smartOrFolder;
                item.key = key;
                item.label = label;
                item.count = count;
                item.active = (owner.selectedLocalFolder == key && owner.selectedLocalTag.isEmpty());
                item.hasStar = star;
                item.hasFolderIcon = folderIc;
                item.bounds = { pad, y, rowW, 30 };
                y += 32;
                rows.push_back(std::move(item));
            };

            addSmart("__ALL__", i18n::tr("All Presets", "Tous les presets"), owner.db.countInFolder("__ALL__"), false, false);
            addSmart("__FAV__", i18n::tr("Favorites", "Favoris"), owner.db.countInFolder("__FAV__"), true, false);
            addSmart("__UNSORTED__", i18n::tr("Unsorted", "Non classés"), owner.db.countInFolder("__UNSORTED__"), false, false);

            y += 10;
            RowItem hdr;
            hdr.type = RowItem::Type::sectionHeader;
            hdr.label = i18n::tr("FOLDERS", "DOSSIERS");
            hdr.bounds = { pad, y, rowW - 26, 22 };
            newFolderRect = { w - pad - 22, y + 1, 20, 20 };
            y += 26;
            rows.push_back(std::move(hdr));

            for (const auto& f : owner.db.getFolders())
            {
                RowItem item;
                item.type = RowItem::Type::smartOrFolder;
                item.key = f;
                item.label = f;
                item.count = owner.db.countInFolder(f);
                item.active = (owner.selectedLocalFolder == f && owner.selectedLocalTag.isEmpty());
                item.hasFolderIcon = true;
                item.canModifyFolder = true;
                item.bounds = { pad, y, rowW, 30 };
                y += 32;
                rows.push_back(std::move(item));
            }

            const auto tags = owner.db.getPopularTags(16);
            if (!tags.empty())
            {
                y += 10;
                RowItem thdr;
                thdr.type = RowItem::Type::sectionHeader;
                thdr.label = i18n::tr("TAGS", "TAGS");
                thdr.bounds = { pad, y, rowW, 22 };
                y += 26;
                rows.push_back(std::move(thdr));

                int cx = pad;
                const auto fnt = theme::mono(10.5f);
                for (const auto& [tag, cnt] : tags)
                {
                    const auto chipText = tag + " " + juce::String(cnt);
                    const int cw = juce::jmin(rowW, juce::roundToInt(fnt.getStringWidthFloat(chipText)) + 16);
                    if (cx + cw > w - pad && cx > pad)
                    {
                        cx = pad;
                        y += 26;
                    }
                    RowItem chip;
                    chip.type = RowItem::Type::tagChip;
                    chip.key = tag;
                    chip.label = chipText;
                    chip.count = cnt;
                    chip.active = (owner.selectedLocalTag == tag);
                    chip.bounds = { cx, y, cw, 22 };
                    cx += cw + 5;
                    rows.push_back(std::move(chip));
                }
                y += 30;
            }
        }
        else
        {
            RowItem chdr;
            chdr.type = RowItem::Type::sectionHeader;
            chdr.label = i18n::tr("CATEGORIES", "CATÉGORIES");
            chdr.bounds = { pad, y, rowW, 22 };
            y += 26;
            rows.push_back(std::move(chdr));

            auto addCat = [&](const juce::String& catKey, const juce::String& label, int cnt) {
                RowItem item;
                item.type = RowItem::Type::category;
                item.key = catKey;
                item.label = label;
                item.count = cnt;
                item.active = (owner.selectedOnlineCategory == catKey && owner.selectedOnlineTag.isEmpty());
                item.bounds = { pad, y, rowW, 30 };
                y += 32;
                rows.push_back(std::move(item));
            };

            addCat("all", i18n::tr("All Categories", "Toutes catégories"), owner.onlineCatalog.allTotal);
            for (const char* cat : kCategoryOrder)
            {
                const auto it = owner.onlineCatalog.categoryCounts.find(cat);
                const int cnt = (it != owner.onlineCatalog.categoryCounts.end()) ? it->second : 0;
                addCat(cat, patchdb::categoryLabel(cat, i18n::isFrench()), cnt);
            }

            if (!owner.onlineCatalog.popularTags.empty())
            {
                y += 10;
                RowItem thdr;
                thdr.type = RowItem::Type::sectionHeader;
                thdr.label = i18n::tr("POPULAR TAGS", "TAGS POPULAIRES");
                thdr.bounds = { pad, y, rowW, 22 };
                y += 26;
                rows.push_back(std::move(thdr));

                int cx = pad;
                const auto fnt = theme::mono(10.5f);
                for (const auto& [tag, cnt] : owner.onlineCatalog.popularTags)
                {
                    const auto chipText = tag + " " + juce::String(cnt);
                    const int cw = juce::jmin(rowW, juce::roundToInt(fnt.getStringWidthFloat(chipText)) + 16);
                    if (cx + cw > w - pad && cx > pad)
                    {
                        cx = pad;
                        y += 26;
                    }
                    RowItem chip;
                    chip.type = RowItem::Type::tagChip;
                    chip.key = tag;
                    chip.label = chipText;
                    chip.count = cnt;
                    chip.active = (owner.selectedOnlineTag == tag);
                    chip.bounds = { cx, y, cw, 22 };
                    cx += cw + 5;
                    rows.push_back(std::move(chip));
                }
                y += 30;
            }
        }

        setSize(w, juce::jmax(y + 12, getParentHeight()));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::surface);

        for (const auto& row : rows)
        {
            const bool isHover = (&row - rows.data()) == hoveredIndex;
            if (row.type == RowItem::Type::sectionHeader)
            {
                g.setColour(theme::faint);
                g.setFont(theme::mono(10.0f));
                g.drawText(row.label, row.bounds, juce::Justification::centredLeft);
                continue;
            }

            if (row.type == RowItem::Type::tagChip)
            {
                auto r = row.bounds.toFloat();
                g.setColour(row.active ? theme::accent.withAlpha(0.18f)
                                       : (isHover ? theme::raised : theme::card));
                g.fillRoundedRectangle(r, 11.0f);
                g.setColour(row.active ? theme::accent.withAlpha(0.7f) : theme::lineSoft);
                g.drawRoundedRectangle(r.reduced(0.5f), 11.0f, 1.0f);
                g.setColour(row.active ? theme::accent : theme::muted);
                g.setFont(theme::mono(10.5f));
                g.drawText(row.label, row.bounds, juce::Justification::centred, true);
                continue;
            }

            auto r = row.bounds.toFloat();
            if (row.active)
            {
                g.setColour(theme::accent.withAlpha(0.14f));
                g.fillRoundedRectangle(r, 6.0f);
                g.setColour(theme::accent.withAlpha(0.45f));
                g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
            }
            else if (isHover)
            {
                g.setColour(theme::card);
                g.fillRoundedRectangle(r, 6.0f);
            }

            auto content = row.bounds.reduced(10, 0);
            if (row.hasStar || row.hasFolderIcon)
            {
                auto ir = content.removeFromLeft(16).withSizeKeepingCentre(13, 13).toFloat();
                drawIcon(g, row.hasStar ? Icon::starFilled : Icon::folder, ir,
                         row.active ? theme::accent : (row.hasStar ? theme::warning : theme::muted), 1.4f);
                content.removeFromLeft(7);
            }

            auto countArea = content.removeFromRight(30);
            g.setColour(row.active ? theme::accent : theme::faint);
            g.setFont(theme::mono(10.5f));
            g.drawText(juce::String(row.count), countArea, juce::Justification::centredRight);

            g.setColour(row.active ? theme::text : (isHover ? theme::text : theme::muted));
            g.setFont(theme::font(12.5f, row.active));
            g.drawText(row.label, content, juce::Justification::centredLeft, true);
        }

        if (!newFolderRect.isEmpty())
        {
            auto fr = newFolderRect.toFloat();
            g.setColour(hoverNewFolder ? theme::raised : theme::card);
            g.fillRoundedRectangle(fr, 4.0f);
            g.setColour(hoverNewFolder ? theme::accent : theme::lineSoft);
            g.drawRoundedRectangle(fr.reduced(0.5f), 4.0f, 1.0f);
            drawIcon(g, Icon::plus, fr.reduced(4.0f), hoverNewFolder ? theme::accent : theme::muted, 1.6f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool nh = newFolderRect.contains(e.getPosition());
        int next = -1;
        if (!nh)
        {
            for (int i = 0; i < (int)rows.size(); ++i)
            {
                if (rows[(size_t)i].type != RowItem::Type::sectionHeader
                    && rows[(size_t)i].bounds.contains(e.getPosition()))
                {
                    next = i;
                    break;
                }
            }
        }
        if (nh != hoverNewFolder || next != hoveredIndex)
        {
            hoverNewFolder = nh;
            hoveredIndex = next;
            setMouseCursor((nh || next >= 0) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredIndex = -1;
        hoverNewFolder = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (newFolderRect.contains(e.getPosition()))
        {
            owner.promptNewFolder();
            return;
        }

        for (const auto& row : rows)
        {
            if (row.type == RowItem::Type::sectionHeader || !row.bounds.contains(e.getPosition()))
                continue;

            if (e.mods.isPopupMenu() && row.canModifyFolder)
            {
                juce::PopupMenu m;
                m.addItem(1, i18n::tr("Rename folder...", "Renommer le dossier..."));
                m.addItem(2, i18n::tr("Delete folder", "Supprimer le dossier"));
                m.addSeparator();
                m.addItem(3, i18n::tr("New folder...", "Nouveau dossier..."));
                const auto fName = row.key;
                m.showMenuAsync(juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PresetHubView>(&owner), fName](int res) {
                    if (!safe) return;
                    if (res == 1) safe->promptRenameFolder(fName);
                    else if (res == 2) safe->confirmDeleteFolder(fName);
                    else if (res == 3) safe->promptNewFolder();
                });
                return;
            }

            if (owner.scope == Scope::local)
            {
                if (row.type == RowItem::Type::tagChip)
                {
                    owner.selectedLocalTag = (owner.selectedLocalTag == row.key) ? juce::String() : row.key;
                }
                else
                {
                    owner.selectedLocalFolder = row.key;
                    owner.selectedLocalTag.clear();
                }
                owner.runLocalQuery(true);
            }
            else
            {
                if (row.type == RowItem::Type::tagChip)
                {
                    owner.selectedOnlineTag = (owner.selectedOnlineTag == row.key) ? juce::String() : row.key;
                }
                else
                {
                    owner.selectedOnlineCategory = row.key;
                    owner.selectedOnlineTag.clear();
                }
                owner.refreshOnlineCatalog();
            }
            return;
        }
    }

private:
    PresetHubView& owner;
    std::vector<RowItem> rows;
    juce::Rectangle<int> newFolderRect;
    int hoveredIndex = -1;
    bool hoverNewFolder = false;
};

//==============================================================================
class PresetHubView::GridContent final : public juce::Component
{
public:
    explicit GridContent(PresetHubView& ownerRef) : owner(ownerRef) {}

    struct CardBox
    {
        juce::String idOrCode;
        juce::String title, badgeText, subline, footerText;
        juce::StringArray plugins;
        bool selected = false;
        bool favorite = false;
        bool savedLocally = false;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> starRect;
    };

    void rebuild()
    {
        cards.clear();
        const int w = juce::jmax(300, getWidth());
        const int pad = 16;
        const int gap = 12;
        const int cols = w >= 860 ? 3 : (w >= 460 ? 2 : 1);
        const int cardW = (w - pad * 2 - gap * (cols - 1)) / cols;
        const int cardH = 126;

        if (owner.scope == Scope::local)
        {
            for (int i = 0; i < (int)owner.filteredLocal.size(); ++i)
            {
                const auto& p = owner.filteredLocal[(size_t)i];
                CardBox c;
                c.idOrCode = p.id;
                c.title = p.title;
                c.favorite = p.favorite;
                c.selected = (owner.selectedLocalId == p.id);
                c.plugins = p.plugins;

                const auto folderTxt = p.folder.isNotEmpty() ? p.folder : i18n::tr("Unsorted", "Non classé");
                const auto catTxt = patchdb::categoryLabel(p.category, i18n::isFrench());
                c.subline = folderTxt + "  ·  " + catTxt;
                if (p.sourceCode.isNotEmpty())
                    c.subline += "  ·  " + p.sourceCode;

                const auto dateStr = formatShortDate(p.updatedAt.isNotEmpty() ? p.updatedAt : p.createdAt);
                juce::String leftMeta = p.author.isNotEmpty() ? p.author : i18n::tr("Local", "Local");
                if (dateStr.isNotEmpty()) leftMeta += "  ·  " + dateStr;
                if (p.useCount > 0) leftMeta += "  ·  " + juce::String(p.useCount) + i18n::tr(" uses", " util.");
                c.footerText = leftMeta;

                const int col = i % cols;
                const int row = i / cols;
                c.bounds = { pad + col * (cardW + gap), pad + row * (cardH + gap), cardW, cardH };
                c.starRect = { c.bounds.getRight() - 34, c.bounds.getY() + 10, 26, 26 };
                cards.push_back(std::move(c));
            }
        }
        else
        {
            for (int i = 0; i < (int)owner.onlineCatalog.presets.size(); ++i)
            {
                const auto& p = owner.onlineCatalog.presets[(size_t)i];
                CardBox c;
                c.idOrCode = p.code;
                c.title = p.title;
                c.badgeText = p.code;
                c.selected = (owner.selectedOnlineCode == p.code);
                c.savedLocally = owner.db.hasSourceCode(p.code);
                c.plugins = p.plugins;

                const auto catTxt = p.category.isNotEmpty() ? patchdb::categoryLabel(p.category, i18n::isFrench())
                                                          : p.categoryLabel;
                c.subline = catTxt + (c.savedLocally ? i18n::tr("  ·  Saved ✓", "  ·  Enregistré ✓") : juce::String());

                const auto dateStr = formatShortDate(p.createdAt);
                c.footerText = (p.author.isNotEmpty() ? p.author : i18n::tr("Anonymous", "Anonyme"))
                             + (dateStr.isNotEmpty() ? ("  ·  " + dateStr) : "")
                             + "  ·  " + juce::String(p.downloads) + " dl";

                const int col = i % cols;
                const int row = i / cols;
                c.bounds = { pad + col * (cardW + gap), pad + row * (cardH + gap), cardW, cardH };
                cards.push_back(std::move(c));
            }
        }

        const int totalRows = cards.empty() ? 0 : (((int)cards.size() + cols - 1) / cols);
        const int totalH = totalRows > 0 ? pad * 2 + totalRows * cardH + (totalRows - 1) * gap : 260;
        setSize(w, juce::jmax(totalH, getParentHeight()));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::background);

        if (owner.scope == Scope::online && owner.onlineLoading && cards.empty())
        {
            g.setColour(theme::muted);
            g.setFont(theme::font(14.0f));
            g.drawText(i18n::tr("Loading online presets from PatchDrop...", "Chargement des presets depuis PatchDrop..."),
                       getLocalBounds().reduced(32), juce::Justification::centred);
            return;
        }

        if (owner.scope == Scope::online && !owner.onlineCatalog.ok && owner.onlineCatalog.error.isNotEmpty() && cards.empty())
        {
            g.setColour(theme::muted);
            g.setFont(theme::font(13.5f));
            g.drawText(i18n::tr("Could not load online presets: ", "Impossible de charger les presets en ligne : ")
                           + owner.onlineCatalog.error,
                       getLocalBounds().reduced(32), juce::Justification::centred);
            return;
        }

        if (cards.empty())
        {
            g.setColour(theme::muted);
            g.setFont(theme::font(13.5f));
            const auto msg = owner.scope == Scope::local
                ? i18n::tr("No local presets match this filter.\nClick \"Save current\" or drop .patchlink / .fst / .RfxChain / .adg files here.",
                           "Aucun preset local ne correspond.\nCliquez sur « Sauver chaîne » ou déposez des fichiers .patchlink / .fst / .RfxChain / .adg ici.")
                : i18n::tr("No online presets match this search.", "Aucun preset en ligne ne correspond à cette recherche.");
            g.drawFittedText(msg, getLocalBounds().reduced(40), juce::Justification::centred, 3);
            return;
        }

        for (int i = 0; i < (int)cards.size(); ++i)
        {
            const auto& c = cards[(size_t)i];
            const bool isHover = (i == hoveredIndex);
            auto r = c.bounds.toFloat();

            g.setColour(c.selected ? theme::cardHover : (isHover ? theme::cardHover : theme::card));
            g.fillRoundedRectangle(r, theme::radius);
            g.setColour(c.selected ? theme::accent : (isHover ? theme::line : theme::lineSoft));
            g.drawRoundedRectangle(r.reduced(0.5f), theme::radius, c.selected ? 1.5f : 1.0f);

            auto area = c.bounds.reduced(14, 10);

            // Header line: title + star / code pill
            auto topRow = area.removeFromTop(24);
            if (owner.scope == Scope::local)
            {
                topRow.removeFromRight(28);
                auto sr = c.starRect.toFloat().withSizeKeepingCentre(15, 15);
                drawIcon(g, c.favorite ? Icon::starFilled : Icon::star, sr,
                         c.favorite ? theme::warning : (isHover ? theme::muted : theme::faint), 1.4f);
            }
            else
            {
                auto pill = topRow.removeFromRight(66).reduced(0, 2).toFloat();
                g.setColour(theme::accent.withAlpha(0.14f));
                g.fillRoundedRectangle(pill, 4.0f);
                g.setColour(theme::accent);
                g.setFont(theme::mono(11.0f));
                g.drawText(c.badgeText, pill, juce::Justification::centred);
                topRow.removeFromRight(6);
            }

            g.setColour(theme::text);
            g.setFont(theme::font(14.0f, true));
            g.drawText(c.title, topRow, juce::Justification::centredLeft, true);

            // Subline: folder / category / saved badge
            auto subRow = area.removeFromTop(18);
            g.setColour(c.savedLocally ? theme::accent : theme::muted);
            g.setFont(theme::mono(10.0f));
            g.drawText(c.subline, subRow, juce::Justification::centredLeft, true);
            area.removeFromTop(6);

            // Plugin chips (up to 4 + overflow)
            auto footerRow = area.removeFromBottom(16);
            auto chipsArea = area.withTrimmedBottom(4);
            int cx = chipsArea.getX();
            int cy = chipsArea.getY();
            const auto chipFont = theme::font(11.0f);
            const int maxShow = 4;
            const int shown = juce::jmin(maxShow, c.plugins.size());
            for (int pi = 0; pi < shown; ++pi)
            {
                const auto& pname = c.plugins[pi];
                const bool installed = owner.isPluginInstalledLocally(pname);
                const int textW = juce::jmin(140, juce::roundToInt(chipFont.getStringWidthFloat(pname)));
                const int chipW = 16 + textW + 8;
                if (cx + chipW > chipsArea.getRight() && cx > chipsArea.getX())
                {
                    cx = chipsArea.getX();
                    cy += 22;
                    if (cy + 18 > chipsArea.getBottom())
                        break;
                }
                auto cr = juce::Rectangle<float>((float)cx, (float)cy, (float)chipW, 18.0f);
                g.setColour(theme::panel);
                g.fillRoundedRectangle(cr, 4.0f);
                g.setColour(theme::lineSoft);
                g.drawRoundedRectangle(cr.reduced(0.5f), 4.0f, 1.0f);

                // Availability dot
                g.setColour(installed ? theme::accent : theme::warning.withAlpha(0.75f));
                g.fillEllipse(cr.getX() + 6.0f, cr.getCentreY() - 2.5f, 5.0f, 5.0f);

                g.setColour(installed ? theme::text : theme::muted);
                g.setFont(chipFont);
                g.drawText(pname, cr.withTrimmedLeft(14).withTrimmedRight(5), juce::Justification::centredLeft, true);
                cx += chipW + 5;
            }
            if (c.plugins.size() > shown)
            {
                const auto extra = "+" + juce::String(c.plugins.size() - shown);
                g.setColour(theme::faint);
                g.setFont(theme::mono(10.0f));
                g.drawText(extra, cx, cy, 34, 18, juce::Justification::centredLeft);
            }
            else if (c.plugins.isEmpty())
            {
                g.setColour(theme::faint);
                g.setFont(theme::mono(10.0f));
                g.drawText(i18n::tr("Empty chain", "Chaîne vide"), chipsArea, juce::Justification::centredLeft);
            }

            // Footer line
            g.setColour(theme::faint);
            g.setFont(theme::mono(10.0f));
            g.drawText(c.footerText, footerRow, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        int next = -1;
        for (int i = 0; i < (int)cards.size(); ++i)
        {
            if (cards[(size_t)i].bounds.contains(e.getPosition()))
            {
                next = i;
                break;
            }
        }
        if (next != hoveredIndex)
        {
            hoveredIndex = next;
            setMouseCursor(next >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredIndex = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (const auto& c : cards)
        {
            if (!c.bounds.contains(e.getPosition()))
                continue;

            if (owner.scope == Scope::local && c.starRect.contains(e.getPosition()) && !e.mods.isPopupMenu())
            {
                owner.db.toggleFavorite(c.idOrCode);
                owner.runLocalQuery(true);
                return;
            }

            if (owner.scope == Scope::local)
                owner.selectLocalPreset(c.idOrCode);
            else
                owner.selectOnlinePreset(c.idOrCode);

            if (e.mods.isPopupMenu() && owner.scope == Scope::local)
            {
                if (const auto* p = owner.db.findById(c.idOrCode))
                    owner.showLocalCardContextMenu(*p);
            }
            return;
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        for (const auto& c : cards)
        {
            if (!c.bounds.contains(e.getPosition()))
                continue;
            if (owner.scope == Scope::local)
            {
                owner.selectLocalPreset(c.idOrCode);
                owner.loadSelectedLocal();
            }
            else
            {
                owner.selectOnlinePreset(c.idOrCode);
                for (const auto& item : owner.onlineCatalog.presets)
                    if (item.code == c.idOrCode)
                    {
                        owner.downloadAndSaveOnline(item, true);
                        break;
                    }
            }
            return;
        }
    }

private:
    PresetHubView& owner;
    std::vector<CardBox> cards;
    int hoveredIndex = -1;
};

//==============================================================================
class PresetHubView::InspectorPanel final : public juce::Component
{
public:
    explicit InspectorPanel(PresetHubView& ownerRef) : owner(ownerRef)
    {
        titleLabel.setFont(theme::font(16.0f, true));
        addAndMakeVisible(titleLabel);

        metaLabel.setFont(theme::mono(10.5f));
        metaLabel.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(metaLabel);

        descView.setMultiLine(true);
        descView.setReadOnly(true);
        descView.setFont(theme::font(12.0f));
        descView.setColour(juce::TextEditor::backgroundColourId, theme::panel);
        descView.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(descView);

        pluginsCaption.setFont(theme::mono(10.0f));
        pluginsCaption.setColour(juce::Label::textColourId, theme::faint);
        addAndMakeVisible(pluginsCaption);

        primaryBtn.onClick = [this] {
            if (owner.scope == Scope::local) owner.loadSelectedLocal();
            else if (hasOnline) owner.downloadAndSaveOnline(onlineItem, false);
        };
        dragDawBtn.onDragStart = [this] {
            if (owner.scope == Scope::local) owner.dragSelectedLocal(true);
            else if (hasOnline) owner.downloadAndSaveOnline(onlineItem, false, true, false);
        };
        dragDawBtn.onClick = [this] {
            if (owner.scope == Scope::local) owner.dragSelectedLocal(false);
            else if (hasOnline) owner.downloadAndSaveOnline(onlineItem, false, false, true);
        };
        secondaryBtn.onClick = [this] {
            if (owner.scope == Scope::local) owner.stageSelectedLocal();
            else if (hasOnline) owner.downloadAndSaveOnline(onlineItem, true);
        };
        favBtn.onClick = [this] {
            if (hasLocal)
            {
                owner.db.toggleFavorite(localItem.id);
                owner.runLocalQuery(true);
            }
        };
        moveBtn.onClick = [this] {
            if (hasLocal) owner.showMoveFolderMenu(localItem.id, &moveBtn);
        };
        editBtn.onClick = [this] {
            if (hasLocal) owner.openEditMetaDialog(localItem);
        };
        shareOrCopyBtn.onClick = [this] {
            if (owner.scope == Scope::local) owner.shareSelectedLocal();
            else if (hasOnline)
            {
                juce::SystemClipboard::copyTextToClipboard(onlineItem.code);
                if (owner.onToast)
                    owner.onToast(i18n::tr("Copied code ", "Code copié : ") + onlineItem.code, false);
            }
        };
        deleteBtn.onClick = [this] {
            if (hasLocal) owner.confirmDeletePreset(localItem);
        };

        for (auto* b : { &primaryBtn, &dragDawBtn, &secondaryBtn, &favBtn, &moveBtn, &editBtn, &shareOrCopyBtn, &deleteBtn })
            addAndMakeVisible(b);

        applyLanguage();
        refreshSelection();
    }

    void applyTheme()
    {
        titleLabel.setColour(juce::Label::textColourId, theme::text);
        metaLabel.setColour(juce::Label::textColourId, theme::muted);
        pluginsCaption.setColour(juce::Label::textColourId, theme::faint);
        descView.setColour(juce::TextEditor::backgroundColourId, theme::panel);
        descView.setColour(juce::TextEditor::textColourId, theme::text);
        repaint();
    }

    void applyLanguage()
    {
        if (owner.scope == Scope::local)
        {
            primaryBtn.setButtonText(i18n::tr("Load in PatchLink", "Charger ici"));
            secondaryBtn.setButtonText(i18n::tr("Stage", "Préparer"));
            dragDawBtn.setButtonText(i18n::tr("Drag DAW", "Vers DAW"));
            favBtn.setButtonText(hasLocal && localItem.favorite ? i18n::tr("Unfav", "Retirer ★") : i18n::tr("Fav", "Favori"));
            moveBtn.setButtonText(i18n::tr("Move", "Dossier"));
            editBtn.setButtonText(i18n::tr("Edit", "Éditer"));
            shareOrCopyBtn.setButtonText(i18n::tr("Share", "Partager"));
            deleteBtn.setButtonText(i18n::tr("Delete", "Supprimer"));
        }
        else
        {
            const bool already = hasOnline && owner.db.hasSourceCode(onlineItem.code);
            primaryBtn.setButtonText(already ? i18n::tr("Saved ✓ (Update)", "Sauvé ✓ (MAJ)")
                                             : i18n::tr("Download & Save", "Télécharger"));
            secondaryBtn.setButtonText(i18n::tr("Load now", "Charger"));
            dragDawBtn.setButtonText(i18n::tr("Drag DAW", "Vers DAW"));
            shareOrCopyBtn.setButtonText(i18n::tr("Copy code", "Copier code"));
        }
    }

    void refreshSelection()
    {
        hasLocal = false;
        hasOnline = false;
        plugins.clear();

        if (owner.scope == Scope::local)
        {
            if (const auto* p = owner.db.findById(owner.selectedLocalId))
            {
                hasLocal = true;
                localItem = *p;
                plugins = p->plugins;
                titleLabel.setText(p->title, juce::dontSendNotification);
                const auto fLabel = p->folder.isNotEmpty() ? p->folder : i18n::tr("Unsorted", "Non classé");
                juce::String m = fLabel + " · " + patchdb::categoryLabel(p->category, i18n::isFrench());
                if (p->author.isNotEmpty()) m += "\n" + i18n::tr("By ", "Par ") + p->author;
                if (p->sourceCode.isNotEmpty()) m += " · " + p->sourceCode;
                if (p->tags.size() > 0) m += "\n" + p->tags.joinIntoString(" ");
                metaLabel.setText(m, juce::dontSendNotification);
                descView.setText(p->description.isNotEmpty() ? p->description
                                                             : i18n::tr("Double-click card to load, or drag \"Vers DAW\" / \"Drag DAW\" onto a mixer track.",
                                                                        "Double-cliquez pour charger, ou glissez « Vers DAW » sur une piste du mixeur."),
                                 false);
                pluginsCaption.setText(i18n::tr("PLUGINS IN CHAIN (", "PLUGINS DANS LA CHAÎNE (") + juce::String(plugins.size()) + ")",
                                       juce::dontSendNotification);
            }
        }
        else
        {
            for (const auto& item : owner.onlineCatalog.presets)
            {
                if (item.code == owner.selectedOnlineCode)
                {
                    hasOnline = true;
                    onlineItem = item;
                    plugins = item.plugins;
                    titleLabel.setText(item.title, juce::dontSendNotification);
                    juce::String m = item.code + " · " + patchdb::categoryLabel(item.category, i18n::isFrench())
                                   + " · " + juce::String(item.downloads) + " dl";
                    if (item.author.isNotEmpty()) m += "\n" + i18n::tr("By ", "Par ") + item.author;
                    if (item.tags.size() > 0) m += "\n" + item.tags.joinIntoString(" ");
                    metaLabel.setText(m, juce::dontSendNotification);
                    descView.setText(item.description.isNotEmpty() ? item.description
                                                                   : i18n::tr("Community chain from PatchDrop.", "Chaîne partagée sur PatchDrop."),
                                     false);
                    pluginsCaption.setText(i18n::tr("PLUGINS IN CHAIN (", "PLUGINS DANS LA CHAÎNE (") + juce::String(plugins.size()) + ")",
                                           juce::dontSendNotification);
                    break;
                }
            }
        }

        const bool hasAny = hasLocal || hasOnline;
        titleLabel.setVisible(hasAny);
        metaLabel.setVisible(hasAny);
        descView.setVisible(hasAny);
        pluginsCaption.setVisible(hasAny);

        primaryBtn.setVisible(hasAny);
        dragDawBtn.setVisible(hasAny);
        secondaryBtn.setVisible(hasAny);
        shareOrCopyBtn.setVisible(hasAny);
        favBtn.setVisible(hasLocal);
        moveBtn.setVisible(hasLocal);
        editBtn.setVisible(hasLocal);
        deleteBtn.setVisible(hasLocal);

        const bool busy = owner.onlineDownloadBusy;
        primaryBtn.setEnabled(hasAny && !busy);
        dragDawBtn.setEnabled(hasAny && !busy);
        secondaryBtn.setEnabled(hasAny && !busy);

        applyLanguage();
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::panel);
        g.setColour(theme::lineSoft);
        g.drawVerticalLine(0, 0.0f, (float)getHeight());

        if (!hasLocal && !hasOnline)
        {
            g.setColour(theme::faint);
            g.setFont(theme::font(12.5f));
            g.drawFittedText(i18n::tr("Select a preset to inspect its plugins, installed status, and export options.",
                                      "Sélectionnez un preset pour voir ses plugins, leur disponibilité et les options d'export."),
                             getLocalBounds().reduced(24), juce::Justification::centred, 4);
            return;
        }

        if (!pluginsListArea.isEmpty())
        {
            g.setColour(theme::surface);
            g.fillRoundedRectangle(pluginsListArea.toFloat(), 6.0f);
            g.setColour(theme::lineSoft);
            g.drawRoundedRectangle(pluginsListArea.toFloat().reduced(0.5f), 6.0f, 1.0f);

            int py = pluginsListArea.getY() + 8;
            const auto rowFont = theme::font(12.0f);
            const auto statusFont = theme::mono(9.5f);
            for (int i = 0; i < plugins.size(); ++i)
            {
                if (py + 22 > pluginsListArea.getBottom() - 4)
                {
                    g.setColour(theme::faint);
                    g.setFont(statusFont);
                    g.drawText("+" + juce::String(plugins.size() - i) + i18n::tr(" more", " autres"),
                               pluginsListArea.getX() + 10, py, pluginsListArea.getWidth() - 20, 18,
                               juce::Justification::centredLeft);
                    break;
                }
                const auto& pn = plugins[i];
                const bool ok = owner.isPluginInstalledLocally(pn);
                auto rowR = juce::Rectangle<int>(pluginsListArea.getX() + 10, py, pluginsListArea.getWidth() - 20, 20);
                g.setColour(ok ? theme::accent : theme::warning);
                g.fillEllipse((float)rowR.getX(), (float)rowR.getCentreY() - 3.0f, 6.0f, 6.0f);

                auto stArea = rowR.removeFromRight(62);
                g.setColour(ok ? theme::accent : theme::warning);
                g.setFont(statusFont);
                g.drawText(ok ? i18n::tr("READY", "PRÊT") : i18n::tr("MISSING", "ABSENT"),
                           stArea, juce::Justification::centredRight);

                g.setColour(ok ? theme::text : theme::muted);
                g.setFont(rowFont);
                g.drawText(pn, rowR.withTrimmedLeft(12), juce::Justification::centredLeft, true);
                py += 22;
            }
            if (plugins.isEmpty())
            {
                g.setColour(theme::faint);
                g.setFont(theme::font(11.5f));
                g.drawText(i18n::tr("Empty chain", "Chaîne vide"), pluginsListArea, juce::Justification::centred);
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16, 14);
        if (!hasLocal && !hasOnline)
        {
            pluginsListArea = {};
            return;
        }

        titleLabel.setBounds(r.removeFromTop(24));
        r.removeFromTop(4);
        metaLabel.setBounds(r.removeFromTop(44));
        r.removeFromTop(6);
        descView.setBounds(r.removeFromTop(52));
        r.removeFromTop(10);

        // Reserve action buttons at bottom
        if (owner.scope == Scope::local)
        {
            auto row3 = r.removeFromBottom(30);
            const int bw3 = (row3.getWidth() - 12) / 3;
            moveBtn.setBounds(row3.removeFromLeft(bw3));
            row3.removeFromLeft(6);
            editBtn.setBounds(row3.removeFromLeft(bw3));
            row3.removeFromLeft(6);
            shareOrCopyBtn.setBounds(row3);
            r.removeFromBottom(6);

            auto row2b = r.removeFromBottom(30);
            const int bw2b = (row2b.getWidth() - 6) / 2;
            favBtn.setBounds(row2b.removeFromLeft(bw2b));
            row2b.removeFromLeft(6);
            deleteBtn.setBounds(row2b);
            r.removeFromBottom(6);
        }
        else
        {
            auto row3 = r.removeFromBottom(30);
            shareOrCopyBtn.setBounds(row3);
            r.removeFromBottom(6);
        }

        auto row2 = r.removeFromBottom(32);
        const int bw2 = (row2.getWidth() - 6) / 2;
        dragDawBtn.setBounds(row2.removeFromLeft(bw2));
        row2.removeFromLeft(6);
        secondaryBtn.setBounds(row2);
        r.removeFromBottom(6);

        primaryBtn.setBounds(r.removeFromBottom(36));
        r.removeFromBottom(12);

        pluginsCaption.setBounds(r.removeFromTop(18));
        r.removeFromTop(4);
        pluginsListArea = r;
    }

private:
    PresetHubView& owner;
    bool hasLocal = false;
    bool hasOnline = false;
    patchdb::LocalPreset localItem;
    ShareClient::OnlinePresetSummary onlineItem;
    juce::StringArray plugins;
    juce::Rectangle<int> pluginsListArea;

    juce::Label titleLabel, metaLabel, pluginsCaption;
    juce::TextEditor descView;

    PlButton primaryBtn { "Load in PatchLink", Icon::plus, PlButton::Style::primary };
    PlButton dragDawBtn { "Drag DAW", Icon::exportFile, PlButton::Style::secondary };
    PlButton secondaryBtn { "Stage", Icon::none, PlButton::Style::secondary };
    PlButton favBtn { "Fav", Icon::star, PlButton::Style::ghost };
    PlButton moveBtn { "Move", Icon::folder, PlButton::Style::ghost };
    PlButton editBtn { "Edit", Icon::edit, PlButton::Style::ghost };
    PlButton shareOrCopyBtn { "Share", Icon::share, PlButton::Style::ghost };
    PlButton deleteBtn { "Delete", Icon::trash, PlButton::Style::danger };
};

//==============================================================================
PresetHubView::PresetHubView(patchdb::PresetDatabase& database,
                             const juce::KnownPluginList& knownPlugins,
                             const juce::String& apiBaseUrl)
    : db(database), knownList(knownPlugins), apiBase(apiBaseUrl)
{
    sidebarContent = std::make_unique<SidebarContent>(*this);
    gridContent = std::make_unique<GridContent>(*this);
    inspector = std::make_unique<InspectorPanel>(*this);

    sidebarViewport.setViewedComponent(sidebarContent.get(), false);
    sidebarViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(sidebarViewport);

    gridViewport.setViewedComponent(gridContent.get(), false);
    gridViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(gridViewport);

    addAndMakeVisible(inspector.get());

    localScopeBtn.onClick = [this] { setScope(Scope::local); };
    onlineScopeBtn.onClick = [this] { setScope(Scope::online); };
    addAndMakeVisible(localScopeBtn);
    addAndMakeVisible(onlineScopeBtn);

    searchBox.setFont(theme::font(13.0f));
    searchBox.setIndents(10, 0);
    searchBox.onTextChange = [this] {
        clearSearchBtn.setVisible(searchBox.getText().isNotEmpty());
        scheduleSearchRefresh();
    };
    searchBox.onReturnKey = [this] {
        stopTimer();
        if (scope == Scope::local) runLocalQuery(true);
        else refreshOnlineCatalog();
    };
    addAndMakeVisible(searchBox);

    clearSearchBtn.onClick = [this] {
        searchBox.clear();
        clearSearchBtn.setVisible(false);
        stopTimer();
        if (scope == Scope::local) runLocalQuery(true);
        else refreshOnlineCatalog();
    };
    addChildComponent(clearSearchBtn);

    sortBox.onChange = [this] {
        if (scope == Scope::local) runLocalQuery(true);
        else refreshOnlineCatalog();
    };
    addAndMakeVisible(sortBox);

    saveCurrentBtn.onClick = [this] { openSaveCurrentDialog(); };
    importBtn.onClick = [this] {
        if (onImportFilesRequested)
            onImportFilesRequested(getActiveLocalFolderForDrop());
    };
    refreshOnlineBtn.onClick = [this] { refreshOnlineCatalog(); };
    addAndMakeVisible(saveCurrentBtn);
    addAndMakeVisible(importBtn);
    addChildComponent(refreshOnlineBtn);

    refreshPluginAvailability();
    applyLanguage();
    setScope(Scope::local);
}

PresetHubView::~PresetHubView()
{
    stopTimer();
}

void PresetHubView::applyTheme()
{
    searchBox.setColour(juce::TextEditor::backgroundColourId, theme::background);
    searchBox.setColour(juce::TextEditor::textColourId, theme::text);
    searchBox.setColour(juce::TextEditor::outlineColourId, theme::line);
    searchBox.setTextToShowWhenEmpty(searchBox.getTextToShowWhenEmpty(), theme::faint);

    sortBox.setColour(juce::ComboBox::backgroundColourId, theme::card);
    sortBox.setColour(juce::ComboBox::textColourId, theme::text);
    sortBox.setColour(juce::ComboBox::outlineColourId, theme::line);
    sortBox.setColour(juce::ComboBox::buttonColourId, theme::card);
    sortBox.setColour(juce::ComboBox::arrowColourId, theme::muted);

    inspector->applyTheme();
    sidebarContent->repaint();
    gridContent->repaint();
    repaint();
}

void PresetHubView::applyLanguage()
{
    searchBox.setTextToShowWhenEmpty(
        i18n::tr("Search title, plugin, author, #tag, or code...",
                 "Chercher titre, plugin, auteur, #tag ou code..."),
        theme::faint);
    saveCurrentBtn.setButtonText(i18n::tr("Save current", "Sauver chaîne"));
    importBtn.setButtonText(i18n::tr("Import", "Importer"));
    refreshOnlineBtn.setButtonText(i18n::tr("Refresh", "Actualiser"));

    rebuildSortOptions();
    updateTopBarCountsAndVisibility();
    sidebarContent->rebuild();
    gridContent->rebuild();
    inspector->applyLanguage();
    inspector->refreshSelection();
}

void PresetHubView::rebuildSortOptions()
{
    const int prev = sortBox.getSelectedId();
    sortBox.clear(juce::dontSendNotification);
    if (scope == Scope::local)
    {
        sortBox.addItem(i18n::tr("Sort: Newest", "Tri : Plus récents"), 1);
        sortBox.addItem(i18n::tr("Sort: Name A-Z", "Tri : Nom A-Z"), 2);
        sortBox.addItem(i18n::tr("Sort: Name Z-A", "Tri : Nom Z-A"), 3);
        sortBox.addItem(i18n::tr("Sort: Most used", "Tri : Plus utilisés"), 4);
        sortBox.addItem(i18n::tr("Sort: Most plugins", "Tri : Plus de plugins"), 5);
        sortBox.addItem(i18n::tr("Sort: Favorites first", "Tri : Favoris d'abord"), 6);
        sortBox.setSelectedId((prev >= 1 && prev <= 6) ? prev : 1, juce::dontSendNotification);
    }
    else
    {
        sortBox.addItem(i18n::tr("Sort: Newest", "Tri : Plus récents"), 1);
        sortBox.addItem(i18n::tr("Sort: Most downloaded", "Tri : Plus téléchargés"), 2);
        sortBox.addItem(i18n::tr("Sort: Name A-Z", "Tri : Nom A-Z"), 3);
        sortBox.addItem(i18n::tr("Sort: Most plugins", "Tri : Plus de plugins"), 4);
        sortBox.setSelectedId((prev >= 1 && prev <= 4) ? prev : 1, juce::dontSendNotification);
    }
}

void PresetHubView::updateTopBarCountsAndVisibility()
{
    localScopeBtn.setSelected(scope == Scope::local);
    onlineScopeBtn.setSelected(scope == Scope::online);

    localScopeBtn.setButtonText(i18n::tr("Local", "Local") + " (" + juce::String((int)db.getAllPresets().size()) + ")");
    const juce::String onlineCount = onlineCatalog.ok ? (" (" + juce::String(onlineCatalog.allTotal) + ")") : "";
    onlineScopeBtn.setButtonText(i18n::tr("Online", "En ligne") + onlineCount);

    const bool local = (scope == Scope::local);
    saveCurrentBtn.setVisible(local);
    importBtn.setVisible(local);
    refreshOnlineBtn.setVisible(!local);
}

void PresetHubView::setScope(Scope newScope)
{
    const bool changed = (scope != newScope);
    scope = newScope;
    if (changed)
        rebuildSortOptions();
    updateTopBarCountsAndVisibility();
    resized();

    if (scope == Scope::local)
    {
        runLocalQuery(true);
    }
    else
    {
        if (!onlineLoadedOnce && !onlineLoading)
            refreshOnlineCatalog();
        else
        {
            sidebarContent->rebuild();
            gridContent->rebuild();
            inspector->refreshSelection();
        }
    }
}

juce::String PresetHubView::getActiveLocalFolderForDrop() const
{
    if (!selectedLocalFolder.isEmpty() && !selectedLocalFolder.startsWith("__"))
        return selectedLocalFolder;
    return {};
}

void PresetHubView::refreshPluginAvailability()
{
    normalizedInstalledNames.clear();
    for (const auto& t : knownList.getTypes())
    {
        const auto k = normalizeLookupKey(t.name);
        if (k.isNotEmpty() && !normalizedInstalledNames.contains(k))
            normalizedInstalledNames.add(k);
        const auto kn = normalizeLookupKey(t.manufacturerName + t.name);
        if (kn.isNotEmpty() && !normalizedInstalledNames.contains(kn))
            normalizedInstalledNames.add(kn);
    }
    if (gridContent) gridContent->repaint();
    if (inspector) inspector->repaint();
}

bool PresetHubView::isPluginInstalledLocally(const juce::String& pluginName) const
{
    const auto key = normalizeLookupKey(pluginName);
    if (key.isEmpty())
        return false;
    if (normalizedInstalledNames.contains(key))
        return true;
    for (const auto& k : normalizedInstalledNames)
        if (k.contains(key) || key.contains(k))
            return true;
    return false;
}

void PresetHubView::refreshLocal(bool preserveSelection)
{
    updateTopBarCountsAndVisibility();
    if (scope == Scope::local)
        runLocalQuery(preserveSelection);
    else
        gridContent->rebuild();
}

void PresetHubView::runLocalQuery(bool preserveSelection)
{
    patchdb::LocalQuery q;
    q.search = searchBox.getText();
    q.folder = selectedLocalFolder;
    q.tag = selectedLocalTag;
    const int sid = sortBox.getSelectedId();
    q.sort = (sid >= 1 && sid <= 6) ? static_cast<patchdb::LocalSort>(sid - 1) : patchdb::LocalSort::newest;

    filteredLocal = db.query(q);

    if (!preserveSelection || selectedLocalId.isEmpty()
        || std::none_of(filteredLocal.begin(), filteredLocal.end(),
                        [&](const patchdb::LocalPreset& p) { return p.id == selectedLocalId; }))
    {
        selectedLocalId = filteredLocal.empty() ? juce::String() : filteredLocal.front().id;
    }

    updateTopBarCountsAndVisibility();
    sidebarContent->rebuild();
    gridContent->rebuild();
    inspector->refreshSelection();
}

void PresetHubView::refreshOnlineCatalog()
{
    const auto q = searchBox.getText().trim();
    const auto cat = selectedOnlineCategory;
    const auto tag = selectedOnlineTag;
    const int sortId = sortBox.getSelectedId();
    const auto base = apiBase;
    const uint64_t gen = ++onlineRequestGen;
    const juce::String sortParam = (sortId == 2) ? "popular" : "newest";

    onlineLoading = true;
    gridContent->rebuild();

    auto safe = juce::Component::SafePointer<PresetHubView>(this);
    std::thread([safe, gen, base, q, cat, tag, sortParam, sortId]() mutable {
        auto result = ShareClient::fetchCatalog(base, q, cat, tag, sortParam, 100);
        if (result.ok)
        {
            if (sortId == 3)
            {
                std::sort(result.presets.begin(), result.presets.end(),
                          [](const ShareClient::OnlinePresetSummary& a, const ShareClient::OnlinePresetSummary& b) {
                              return a.title.compareIgnoreCase(b.title) < 0;
                          });
            }
            else if (sortId == 4)
            {
                std::sort(result.presets.begin(), result.presets.end(),
                          [](const ShareClient::OnlinePresetSummary& a, const ShareClient::OnlinePresetSummary& b) {
                              return a.plugins.size() > b.plugins.size();
                          });
            }
        }

        juce::MessageManager::callAsync([safe, gen, result = std::move(result)]() mutable {
            if (!safe || gen != safe->onlineRequestGen)
                return;
            safe->onlineLoading = false;
            safe->onlineLoadedOnce = true;
            safe->onlineCatalog = std::move(result);

            if (safe->onlineCatalog.ok)
            {
                const bool selFound = std::any_of(safe->onlineCatalog.presets.begin(),
                                                  safe->onlineCatalog.presets.end(),
                                                  [&](const ShareClient::OnlinePresetSummary& s) {
                                                      return s.code == safe->selectedOnlineCode;
                                                  });
                if (!selFound)
                {
                    safe->selectedOnlineCode = safe->onlineCatalog.presets.empty()
                        ? juce::String()
                        : safe->onlineCatalog.presets.front().code;
                }
                if (safe->onStatus)
                    safe->onStatus(i18n::tr("Online: ", "En ligne : ")
                                   + juce::String((int)safe->onlineCatalog.presets.size())
                                   + i18n::tr(" presets", " presets"));
            }

            safe->updateTopBarCountsAndVisibility();
            if (safe->scope == Scope::online)
            {
                safe->sidebarContent->rebuild();
                safe->gridContent->rebuild();
                safe->inspector->refreshSelection();
            }
        });
    }).detach();
}

void PresetHubView::scheduleSearchRefresh()
{
    startTimer(260);
}

void PresetHubView::timerCallback()
{
    stopTimer();
    if (scope == Scope::local)
        runLocalQuery(true);
    else
        refreshOnlineCatalog();
}

void PresetHubView::selectLocalPreset(const juce::String& id)
{
    selectedLocalId = id;
    gridContent->rebuild();
    inspector->refreshSelection();
}

void PresetHubView::selectOnlinePreset(const juce::String& code)
{
    selectedOnlineCode = code;
    gridContent->rebuild();
    inspector->refreshSelection();
}

void PresetHubView::promptNewFolder()
{
    auto* aw = new juce::AlertWindow(i18n::tr("New folder", "Nouveau dossier"),
                                     i18n::tr("Create a folder in your local preset library:",
                                              "Créer un dossier dans votre bibliothèque locale :"),
                                     juce::AlertWindow::NoIcon);
    aw->addTextEditor("name", "", i18n::tr("Folder name", "Nom du dossier"));
    aw->addButton(i18n::tr("Create", "Créer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton(i18n::tr("Cancel", "Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<PresetHubView>(this), aw](int res) {
            const auto name = aw->getTextEditorContents("name").trim();
            if (res == 1 && safe && name.isNotEmpty())
            {
                if (safe->db.addFolder(name))
                {
                    safe->selectedLocalFolder = name;
                    safe->selectedLocalTag.clear();
                    safe->runLocalQuery(true);
                }
                else if (safe->onToast)
                {
                    safe->onToast(i18n::tr("Folder already exists or invalid name", "Ce dossier existe déjà ou nom invalide"), true);
                }
            }
        }), true);
}

void PresetHubView::promptRenameFolder(const juce::String& oldName)
{
    auto* aw = new juce::AlertWindow(i18n::tr("Rename folder", "Renommer le dossier"),
                                     i18n::tr("New name for \"", "Nouveau nom pour « ") + oldName + "\":",
                                     juce::AlertWindow::NoIcon);
    aw->addTextEditor("name", oldName, i18n::tr("Folder name", "Nom du dossier"));
    aw->addButton(i18n::tr("Rename", "Renommer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton(i18n::tr("Cancel", "Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<PresetHubView>(this), aw, oldName](int res) {
            const auto next = aw->getTextEditorContents("name").trim();
            if (res == 1 && safe && next.isNotEmpty() && safe->db.renameFolder(oldName, next))
            {
                if (safe->selectedLocalFolder == oldName)
                    safe->selectedLocalFolder = next;
                safe->runLocalQuery(true);
            }
        }), true);
}

void PresetHubView::confirmDeleteFolder(const juce::String& name)
{
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        i18n::tr("Delete folder?", "Supprimer le dossier ?"),
        i18n::tr("Presets inside \"", "Les presets dans « ") + name
            + i18n::tr("\" will be moved to Unsorted (not deleted).", " » seront déplacés dans Non classés (rien n'est supprimé)."),
        i18n::tr("Delete folder", "Supprimer"),
        i18n::tr("Cancel", "Annuler"),
        this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<PresetHubView>(this), name](int res) {
            if (safe && res != 0 && safe->db.deleteFolder(name))
            {
                if (safe->selectedLocalFolder == name)
                    safe->selectedLocalFolder = "__ALL__";
                safe->runLocalQuery(true);
            }
        }));
}

void PresetHubView::openSaveCurrentDialog()
{
    const auto patch = onRequestCurrentPatch ? onRequestCurrentPatch() : juce::var();
    if (!patch.isObject())
    {
        if (onToast)
            onToast(i18n::tr("Load or drop a chain first before saving", "Chargez ou déposez d'abord une chaîne à enregistrer"), true);
        return;
    }

    patchdb::LocalPreset initial;
    initial.title = onRequestCurrentTitle ? onRequestCurrentTitle().trim() : juce::String();
    if (initial.title.isEmpty() || initial.title == "Untitled chain")
    {
        const auto names = patchdb::extractPluginNamesFromPatch(patch);
        initial.title = names.isEmpty() ? i18n::tr("New Chain", "Nouvelle chaîne")
                                        : (names.size() == 1 ? names[0] : (names[0] + " + " + juce::String(names.size() - 1)));
    }
    initial.folder = getActiveLocalFolderForDrop();
    const auto pnames = patchdb::extractPluginNamesFromPatch(patch);
    initial.category = patchdb::inferCategoryFromMeta(initial.title, {}, pnames);
    openSaveModalInternal(false, initial, patch);
}

void PresetHubView::openEditMetaDialog(const patchdb::LocalPreset& preset)
{
    openSaveModalInternal(true, preset, juce::var());
}

void PresetHubView::openSaveModalInternal(bool isEdit,
                                          const patchdb::LocalPreset& initialMeta,
                                          const juce::var& patchVar)
{
    const auto pnames = isEdit ? initialMeta.plugins : patchdb::extractPluginNamesFromPatch(patchVar);
    auto modal = std::make_unique<SavePresetModal>(isEdit, initialMeta, db.getFolders(), pnames);
    modal->onDismiss = [safe = juce::Component::SafePointer<PresetHubView>(this)] {
        if (safe && safe->onDismissModal) safe->onDismissModal();
    };
    modal->onSubmit = [safe = juce::Component::SafePointer<PresetHubView>(this), isEdit, patchVar](patchdb::LocalPreset meta) {
        if (!safe) return;
        if (safe->onDismissModal) safe->onDismissModal();

        if (isEdit)
        {
            if (safe->db.updatePresetMeta(meta.id, meta.title, meta.folder, meta.author,
                                          meta.description, meta.category, meta.tags, meta.favorite))
            {
                safe->runLocalQuery(true);
                if (safe->onToast)
                    safe->onToast(i18n::tr("Updated \"", "Mis à jour : « ") + meta.title + "\"", false);
            }
        }
        else
        {
            const auto newId = safe->db.savePreset(patchVar, std::move(meta), false);
            if (newId.isNotEmpty())
            {
                safe->selectedLocalId = newId;
                if (safe->scope == Scope::local)
                    safe->runLocalQuery(true);
                else
                    safe->updateTopBarCountsAndVisibility();
                if (safe->onToast)
                    safe->onToast(i18n::tr("Saved to Local Library", "Enregistré dans la bibliothèque locale"), false);
            }
            else if (safe->onToast)
            {
                safe->onToast(i18n::tr("Could not save preset", "Impossible d'enregistrer le preset"), true);
            }
        }
    };

    if (onShowModal)
        onShowModal(std::move(modal));
}

void PresetHubView::showMoveFolderMenu(const juce::String& presetId, juce::Component* target)
{
    juce::PopupMenu m;
    m.addItem(1, i18n::tr("(Unsorted)", "(Non classé)"));
    const auto folders = db.getFolders();
    for (int i = 0; i < folders.size(); ++i)
        m.addItem(10 + i, folders[i]);

    auto opts = target != nullptr ? juce::PopupMenu::Options().withTargetComponent(target)
                                  : juce::PopupMenu::Options();
    m.showMenuAsync(opts, [safe = juce::Component::SafePointer<PresetHubView>(this), presetId, folders](int res) {
        if (!safe || res == 0) return;
        const juce::String dest = (res == 1) ? juce::String() : folders[res - 10];
        if (safe->db.movePresetToFolder(presetId, dest))
            safe->runLocalQuery(true);
    });
}

void PresetHubView::showLocalCardContextMenu(const patchdb::LocalPreset& preset)
{
    juce::PopupMenu m;
    m.addItem(1, i18n::tr("Load into PatchLink", "Charger dans PatchLink"));
    m.addItem(2, i18n::tr("Stage (Zero-Load)", "Préparer (Sans chargement)"));
    m.addItem(3, i18n::tr("Export / Drag to DAW...", "Exporter / Glisser vers DAW..."));
    m.addSeparator();
    m.addItem(4, preset.favorite ? i18n::tr("Remove from Favorites", "Retirer des Favoris")
                                 : i18n::tr("Add to Favorites ★", "Ajouter aux Favoris ★"));

    juce::PopupMenu moveMenu;
    moveMenu.addItem(100, i18n::tr("(Unsorted)", "(Non classé)"));
    const auto folders = db.getFolders();
    for (int i = 0; i < folders.size(); ++i)
        moveMenu.addItem(101 + i, folders[i], true, preset.folder == folders[i]);
    m.addSubMenu(i18n::tr("Move to folder", "Déplacer vers"), moveMenu);

    m.addItem(5, i18n::tr("Edit details...", "Modifier les infos..."));
    m.addItem(6, i18n::tr("Share online...", "Partager en ligne..."));
    m.addSeparator();
    m.addItem(7, i18n::tr("Delete preset", "Supprimer le preset"));

    const auto id = preset.id;
    m.showMenuAsync(juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PresetHubView>(this), id, folders](int res) {
        if (!safe || res == 0) return;
        if (res == 1) safe->loadSelectedLocal();
        else if (res == 2) safe->stageSelectedLocal();
        else if (res == 3) safe->dragSelectedLocal(false);
        else if (res == 4) { safe->db.toggleFavorite(id); safe->runLocalQuery(true); }
        else if (res == 5) { if (const auto* p = safe->db.findById(id)) safe->openEditMetaDialog(*p); }
        else if (res == 6) safe->shareSelectedLocal();
        else if (res == 7) { if (const auto* p = safe->db.findById(id)) safe->confirmDeletePreset(*p); }
        else if (res == 100) { safe->db.movePresetToFolder(id, {}); safe->runLocalQuery(true); }
        else if (res >= 101 && res - 101 < folders.size()) { safe->db.movePresetToFolder(id, folders[res - 101]); safe->runLocalQuery(true); }
    });
}

void PresetHubView::confirmDeletePreset(const patchdb::LocalPreset& preset)
{
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        i18n::tr("Delete preset?", "Supprimer le preset ?"),
        i18n::tr("Permanently remove \"", "Supprimer définitivement « ") + preset.title
            + i18n::tr("\" from your Local Library?", " » de votre bibliothèque locale ?"),
        i18n::tr("Delete", "Supprimer"),
        i18n::tr("Cancel", "Annuler"),
        this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<PresetHubView>(this), id = preset.id](int res) {
            if (safe && res != 0 && safe->db.deletePreset(id))
                safe->runLocalQuery(false);
        }));
}

void PresetHubView::loadSelectedLocal()
{
    const auto* p = db.findById(selectedLocalId);
    if (p == nullptr) return;
    juce::var patch;
    if (!db.loadPresetPatch(p->id, patch))
    {
        if (onToast) onToast(i18n::tr("Could not read preset file", "Impossible de lire le fichier preset"), true);
        return;
    }
    const auto title = p->title;
    db.recordUse(p->id);
    runLocalQuery(true);
    if (onLoadPatch)
        onLoadPatch(patch, title);
}

void PresetHubView::stageSelectedLocal()
{
    const auto* p = db.findById(selectedLocalId);
    if (p == nullptr) return;
    juce::var patch;
    if (!db.loadPresetPatch(p->id, patch))
    {
        if (onToast) onToast(i18n::tr("Could not read preset file", "Impossible de lire le fichier preset"), true);
        return;
    }
    const auto title = p->title;
    db.recordUse(p->id);
    if (onStagePatch)
        onStagePatch(patch, title);
}

void PresetHubView::dragSelectedLocal(bool startDrag)
{
    const auto* p = db.findById(selectedLocalId);
    if (p == nullptr) return;
    juce::var patch;
    if (!db.loadPresetPatch(p->id, patch))
    {
        if (onToast) onToast(i18n::tr("Could not read preset file", "Impossible de lire le fichier preset"), true);
        return;
    }
    const auto title = p->title;
    db.recordUse(p->id);
    if (onDragPatchToDaw)
        onDragPatchToDaw(patch, title, startDrag);
}

void PresetHubView::shareSelectedLocal()
{
    const auto* p = db.findById(selectedLocalId);
    if (p == nullptr) return;
    juce::var patch;
    if (!db.loadPresetPatch(p->id, patch))
    {
        if (onToast) onToast(i18n::tr("Could not read preset file", "Impossible de lire le fichier preset"), true);
        return;
    }
    if (onSharePatch)
        onSharePatch(patch, p->title);
}

void PresetHubView::downloadAndSaveOnline(const ShareClient::OnlinePresetSummary& item,
                                          bool alsoLoad, bool startDragDaw, bool formatMenu)
{
    if (onlineDownloadBusy)
        return;

    onlineDownloadBusy = true;
    inspector->refreshSelection();
    if (onStatus)
        onStatus(i18n::tr("Downloading ", "Téléchargement de ") + item.code + "...");

    const auto code = item.code;
    const auto base = apiBase;
    auto safe = juce::Component::SafePointer<PresetHubView>(this);
    std::thread([safe, code, base, item, alsoLoad, startDragDaw, formatMenu]() mutable {
        juce::var patch;
        ShareClient::OnlinePresetSummary fullMeta;
        juce::String err;
        const bool ok = ShareClient::fetchWithMeta(code, base, patch, fullMeta, err);
        juce::MessageManager::callAsync([safe, ok, patch, fullMeta, item, err, alsoLoad, startDragDaw, formatMenu]() mutable {
            if (!safe) return;
            safe->onlineDownloadBusy = false;

            if (!ok)
            {
                safe->inspector->refreshSelection();
                if (safe->onToast)
                    safe->onToast(err.isNotEmpty() ? err : i18n::tr("Download failed", "Échec du téléchargement"), true);
                return;
            }

            const auto finalTitle = fullMeta.title.isNotEmpty() ? fullMeta.title : item.title;
            patchdb::LocalPreset meta;
            meta.title = finalTitle;
            meta.author = fullMeta.author.isNotEmpty() ? fullMeta.author : item.author;
            meta.description = fullMeta.description.isNotEmpty() ? fullMeta.description : item.description;
            meta.category = fullMeta.category.isNotEmpty() ? fullMeta.category : item.category;
            meta.tags = fullMeta.tags.isEmpty() ? item.tags : fullMeta.tags;
            meta.sourceCode = item.code;
            meta.folder = "Downloaded";

            const auto savedId = safe->db.savePreset(patch, std::move(meta), false);
            if (savedId.isNotEmpty())
                safe->selectedLocalId = savedId;

            safe->updateTopBarCountsAndVisibility();
            safe->gridContent->rebuild();
            safe->inspector->refreshSelection();

            if (startDragDaw || formatMenu)
            {
                if (safe->onDragPatchToDaw)
                    safe->onDragPatchToDaw(patch, finalTitle, startDragDaw);
            }
            else if (alsoLoad)
            {
                if (safe->onLoadPatch)
                    safe->onLoadPatch(patch, finalTitle);
            }
            else if (safe->onToast)
            {
                safe->onToast(i18n::tr("Saved \"", "Téléchargé dans Dossiers/Downloaded : « ") + finalTitle
                                  + i18n::tr("\" to Local Library (Downloaded)", " »"), false);
            }
        });
    }).detach();
}

void PresetHubView::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::panel);
    g.fillRect(0, 0, getWidth(), 52);
    g.setColour(theme::lineSoft);
    g.drawHorizontalLine(51, 0.0f, (float)getWidth());
    g.drawVerticalLine(216, 52.0f, (float)getHeight());
}

void PresetHubView::resized()
{
    auto r = getLocalBounds();
    auto topBar = r.removeFromTop(52).reduced(14, 9);

    localScopeBtn.setBounds(topBar.removeFromLeft(116));
    topBar.removeFromLeft(4);
    onlineScopeBtn.setBounds(topBar.removeFromLeft(126));
    topBar.removeFromLeft(10);

    if (scope == Scope::local)
    {
        saveCurrentBtn.setBounds(topBar.removeFromRight(124));
        topBar.removeFromRight(6);
        importBtn.setBounds(topBar.removeFromRight(86));
        topBar.removeFromRight(8);
    }
    else
    {
        refreshOnlineBtn.setBounds(topBar.removeFromRight(98));
        topBar.removeFromRight(8);
    }

    sortBox.setBounds(topBar.removeFromRight(176));
    topBar.removeFromRight(8);

    searchBox.setBounds(topBar);
    clearSearchBtn.setBounds(searchBox.getRight() - 28, searchBox.getY() + 3, 24, searchBox.getHeight() - 6);

    auto left = r.removeFromLeft(216);
    sidebarViewport.setBounds(left);
    sidebarContent->setSize(left.getWidth() - (sidebarViewport.isVerticalScrollBarShown() ? 10 : 0),
                            juce::jmax(left.getHeight(), sidebarContent->getHeight()));
    sidebarContent->rebuild();

    auto right = r.removeFromRight(juce::jmin(272, juce::jmax(230, getWidth() / 4)));
    inspector->setBounds(right);

    gridViewport.setBounds(r);
    gridContent->setSize(r.getWidth() - 10, juce::jmax(r.getHeight(), gridContent->getHeight()));
    gridContent->rebuild();
}
