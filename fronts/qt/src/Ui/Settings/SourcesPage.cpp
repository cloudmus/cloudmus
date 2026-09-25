#include "Settings/SourcesPage.h"

#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

#include "Icons.h"
#include "Settings.h"
#include "SourceManager.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui::Settings {

namespace {
constexpr int kIconSide = 16;
} // namespace

SourcesPage::SourcesPage(Config::Settings& settings, Rpc::SourceManager& sourceManager, QObject* parent)
    : Page(parent)
    , settings_(settings)
    , sourceManager_(sourceManager)
{
}

QString SourcesPage::title() const { return tr("Sources"); }

int SourcesPage::estimatedHeight() const { return 40 + 44 * int(sourceManager_.manifests().size()); }

QWidget* SourcesPage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space1);

    const auto& manifests = sourceManager_.manifests();
    if (manifests.isEmpty()) {
        auto* none = new QLabel(tr("No sources are installed."), widget);
        none->setProperty("hint", true);
        none->setFont(Theme::font(Theme::TextStyle::Body));
        layout->addWidget(none);
        return widget;
    }

    for (const Rpc::BackendManifest& manifest : manifests) {
        auto* check = new QCheckBox(manifest.name, widget);
        check->setFont(Theme::font(Theme::TextStyle::Body));
        if (!manifest.iconPath.isEmpty()) {
            check->setIcon(Theme::iconFromFile(manifest.iconPath, Theme::IconColor::InkSecondary, kIconSide));
            check->setIconSize(QSize(kIconSide, kIconSide));
        }
        check->setChecked(sourceManager_.isEnabled(manifest.id));
        connect(check, &QCheckBox::toggled, this, &Page::dirtyChanged);
        checks_.insert(manifest.id, check);
        layout->addWidget(check);

        // Only a running source has told us what it is.
        const Rpc::RpcClient* client = sourceManager_.client(manifest.id);
        const QString description = client != nullptr ? client->sourceDescription() : QString();
        if (!description.isEmpty()) {
            auto* label = new QLabel(description, widget);
            label->setProperty("hint", true);
            label->setFont(Theme::font(Theme::TextStyle::BodySecondary));
            label->setWordWrap(true);
            // Under the name, past the checkbox indicator and the icon.
            label->setContentsMargins(Theme::Spacing::space6 + Theme::Spacing::space4, 0, 0, Theme::Spacing::space1);
            layout->addWidget(label);
        }
    }

    auto* hint = new QLabel(tr("A switched-off source doesn't start and isn't shown in the sidebar."), widget);
    hint->setProperty("hint", true);
    hint->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    hint->setWordWrap(true);
    layout->addSpacing(Theme::Spacing::space2);
    layout->addWidget(hint);
    return widget;
}

bool SourcesPage::isDirty() const
{
    for (auto it = checks_.cbegin(); it != checks_.cend(); ++it) {
        if (it.value()->isChecked() != sourceManager_.isEnabled(it.key()))
            return true;
    }
    return false;
}

void SourcesPage::apply()
{
    // Stored for every manifest this page knows about, while keeping ids of
    // backends that aren't installed right now: reinstalling one shouldn't
    // silently switch it back on.
    QStringList disabled = settings_.disabledSources();
    for (auto it = checks_.cbegin(); it != checks_.cend(); ++it) {
        const bool enabled = it.value()->isChecked();
        disabled.removeAll(it.key());
        if (!enabled)
            disabled.append(it.key());
        sourceManager_.setEnabled(it.key(), enabled);
    }
    settings_.setDisabledSources(disabled);
}

} // namespace Ui::Settings
