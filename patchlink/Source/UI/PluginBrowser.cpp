#include "PluginBrowser.h"
#include "../Core/I18n.h"

class PluginBrowser::ResultsModel final : public juce::ListBoxModel
{
public:
    explicit ResultsModel(PluginBrowser& browser) : owner(browser) {}

    int getNumRows() override { return (int)owner.filtered.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (!juce::isPositiveAndBelow(row, (int)owner.filtered.size())) return;
        const auto& type = owner.filtered[(size_t)row];
        auto r = juce::Rectangle<int>(0, 0, width, height).reduced(8, 3);
        if (selected)
        {
            g.setColour(theme::accent.withAlpha(0.10f));
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            g.setColour(theme::accent);
            g.fillRoundedRectangle((float)r.getX(), (float)r.getY() + 8, 2.0f, (float)r.getHeight() - 16, 1.0f);
        }
        else if (row % 2 != 0)
        {
            g.setColour(theme::card.withAlpha(0.34f));
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
        }

        auto textArea = r.reduced(12, 0);
        auto badge = textArea.removeFromRight(54);
        textArea.removeFromRight(12);
        g.setColour(selected ? theme::text : theme::text.withAlpha(0.94f));
        g.setFont(theme::font(14.0f, true));
        g.drawText(type.name, textArea.removeFromTop(25).translated(0, 6), juce::Justification::centredLeft, true);
        g.setColour(theme::muted);
        g.setFont(theme::font(11.5f));
        auto details = type.manufacturerName.isNotEmpty()
                           ? type.manufacturerName
                           : i18n::tr("Unknown developer", "Développeur inconnu");
        if (type.version.isNotEmpty()) details += "  /  " + type.version;
        g.drawText(details, textArea.removeFromTop(21), juce::Justification::centredLeft, true);

        auto br = badge.withSizeKeepingCentre(52, 22).toFloat();
        g.setColour(theme::raised.withAlpha(0.75f));
        g.fillRoundedRectangle(br, 4.0f);
        g.setColour(theme::muted);
        g.setFont(theme::mono(9.5f));
        g.drawText(type.pluginFormatName.toUpperCase(), badge, juce::Justification::centred);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { owner.chooseRow(row); }
    void returnKeyPressed(int row) override { owner.chooseRow(row); }
    void selectedRowsChanged(int) override { owner.countLabel.repaint(); }

private:
    PluginBrowser& owner;
};

PluginBrowser::PluginBrowser(const juce::Array<juce::PluginDescription>& types)
    : allTypes(types)
{
    std::sort(allTypes.begin(), allTypes.end(), [](const auto& a, const auto& b) {
        const auto an = a.name.toLowerCase();
        const auto bn = b.name.toLowerCase();
        return an == bn ? a.manufacturerName.toLowerCase() < b.manufacturerName.toLowerCase() : an < bn;
    });

    title.setText(i18n::tr("Add a plugin", "Ajouter un plugin"), juce::dontSendNotification);
    title.setFont(theme::font(22.0f, true));
    subtitle.setText(i18n::tr("Search your library by plugin, developer, or format.",
                              "Recherchez par nom, éditeur ou format."),
                     juce::dontSendNotification);
    subtitle.setFont(theme::font(12.5f));
    subtitle.setColour(juce::Label::textColourId, theme::muted);
    addAndMakeVisible(title);
    addAndMakeVisible(subtitle);

    search.setTextToShowWhenEmpty(i18n::tr("Search plugins...", "Rechercher un plugin..."), theme::faint);
    search.setFont(theme::font(14.0f));
    search.setIndents(38, 0);
    search.setSelectAllWhenFocused(true);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { chooseSelected(); };
    search.onEscapeKey = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible(search);

    model = std::make_unique<ResultsModel>(*this);
    results.setModel(model.get());
    results.setRowHeight(58);
    results.setMultipleSelectionEnabled(false);
    results.setColour(juce::ListBox::backgroundColourId, theme::background.withAlpha(0.38f));
    results.getViewport()->setScrollBarsShown(true, false, true, false);
    addAndMakeVisible(results);

    closeButton.setTooltip(i18n::tr("Close (Esc)", "Fermer (Échap)"));
    closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible(closeButton);

    scanButton.setButtonText(i18n::tr("Open Scan Manager", "Ouvrir le scanner"));

    scanButton.onClick = [this] { if (onScanRequested) onScanRequested(); };
    addChildComponent(scanButton);

    emptyLabel.setFont(theme::font(13.0f));
    emptyLabel.setColour(juce::Label::textColourId, theme::muted);
    emptyLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyLabel);

    countLabel.setFont(theme::mono(10.5f));
    countLabel.setColour(juce::Label::textColourId, theme::faint);
    addAndMakeVisible(countLabel);

    filter();
    if (!filtered.empty()) results.selectRow(0);
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginBrowser>(this)] {
        if (safe != nullptr) safe->search.grabKeyboardFocus();
    });
}

void PluginBrowser::filter()
{
    const auto previous = results.getSelectedRow() >= 0 && results.getSelectedRow() < (int)filtered.size()
                            ? filtered[(size_t)results.getSelectedRow()].createIdentifierString() : juce::String{};
    const auto query = search.getText().trim().toLowerCase();
    filtered.clear();
    for (const auto& type : allTypes)
    {
        if (query.isNotEmpty()
            && !type.name.toLowerCase().contains(query)
            && !type.manufacturerName.toLowerCase().contains(query)
            && !type.pluginFormatName.toLowerCase().contains(query)
            && !type.category.toLowerCase().contains(query))
            continue;
        filtered.push_back(type);
    }

    results.updateContent();
    int toSelect = filtered.empty() ? -1 : 0;
    if (previous.isNotEmpty())
        for (size_t i = 0; i < filtered.size(); ++i)
            if (filtered[i].createIdentifierString() == previous) { toSelect = (int)i; break; }
    results.selectRow(toSelect);

    const auto hasLibrary = !allTypes.isEmpty();
    emptyLabel.setText(hasLibrary
                           ? i18n::tr("No plugins match this search.", "Aucun plugin ne correspond à la recherche.")
                           : i18n::tr("Your library is empty. Scan your plugin folders to get started.",
                                      "Votre bibliothèque est vide. Scannez vos dossiers VST pour commencer."),
                       juce::dontSendNotification);
    emptyLabel.setVisible(filtered.empty());
    scanButton.setVisible(!hasLibrary);
    const auto noun = filtered.size() == 1 ? i18n::tr(" plugin", " plugin") : i18n::tr(" plugins", " plugins");
    const auto suffix = query.isNotEmpty() ? i18n::tr(" found", " trouvé(s)") : i18n::tr(" indexed", " indexé(s)");
    countLabel.setText(juce::String((int)filtered.size()) + noun + suffix, juce::dontSendNotification);
    results.repaint();
}

void PluginBrowser::chooseSelected()
{
    chooseRow(results.getSelectedRow() >= 0 ? results.getSelectedRow() : 0);
}

void PluginBrowser::chooseRow(int row)
{
    if (juce::isPositiveAndBelow(row, (int)filtered.size()) && onPluginChosen)
        onPluginChosen(filtered[(size_t)row]);
}

void PluginBrowser::layoutCard(juce::Rectangle<int> card)
{
    auto r = card.reduced(24, 20);
    closeButton.setBounds(r.getRight() - 34, r.getY(), 34, 34);
    title.setBounds(r.removeFromTop(31).withTrimmedRight(44));
    subtitle.setBounds(r.removeFromTop(23));
    r.removeFromTop(14);
    search.setBounds(r.removeFromTop(42));
    r.removeFromTop(14);
    countLabel.setBounds(r.removeFromBottom(24));
    results.setBounds(r);
    emptyLabel.setBounds(r.reduced(24).removeFromTop(100).withCentre(r.getCentre()).withHeight(90));
    scanButton.setBounds(emptyLabel.getBounds().removeFromBottom(34).withSizeKeepingCentre(180, 34).translated(0, 50));
}

void PluginBrowser::paintOverChildren(juce::Graphics& g)
{
    drawIcon(g, Icon::search, search.getBounds().toFloat().reduced(12, 12).withWidth(18), theme::faint);
    g.setColour(theme::lineSoft);
    auto card = getCardBounds();
    g.drawLine((float)card.getX() + 24, (float)search.getBottom() + 14,
               (float)card.getRight() - 24, (float)search.getBottom() + 14, 1.0f);
}
