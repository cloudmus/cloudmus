#include "Settings/PreloadPage.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "Preload.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui::Settings {

PreloadPage::PreloadPage(ViewModel::Preload& preload, QObject* parent)
    : Page(parent)
    , preload_(preload)
{
}

QString PreloadPage::title() const { return tr("Preloading"); }

QWidget* PreloadPage::createWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);

    const auto tracksSpin = [this, page](int value) {
        auto* spin = new QSpinBox(page);
        spin->setFont(Theme::font(Theme::TextStyle::Body));
        spin->setRange(0, ViewModel::Preload::kMaxTracks);
        spin->setValue(value);
        connect(spin, &QSpinBox::valueChanged, this, &Page::dirtyChanged);
        return spin;
    };
    playlistSpin_ = tracksSpin(preload_.playlistTracks());
    radioSpin_ = tracksSpin(preload_.radioTracks());

    auto* hint = new QLabel(
        tr("How many tracks after the playing one are downloaded in advance, one at a time, so they start at once "
           "and keep playing through a bad connection. With 0 only the playing track is downloaded."),
        page);
    hint->setProperty("hint", true);
    hint->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    hint->setWordWrap(true);

    cacheSpin_ = new QSpinBox(page);
    cacheSpin_->setFont(Theme::font(Theme::TextStyle::Body));
    cacheSpin_->setRange(0, 1024 * 1024);
    cacheSpin_->setSingleStep(256);
    //: Megabytes, after a number: "1024 MB"
    cacheSpin_->setSuffix(tr(" MB"));
    cacheSpin_->setValue(preload_.cacheMaxMb());
    connect(cacheSpin_, &QSpinBox::valueChanged, this, &Page::dirtyChanged);

    usedLabel_ = new QLabel(page);
    usedLabel_->setProperty("hint", true);
    usedLabel_->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    connect(&preload_, &ViewModel::Preload::cacheUsedChanged, usedLabel_, [this]() { updateUsed(); });
    updateUsed();

    // Takes effect at once, like any action that isn't a setting.
    auto* clearButton = new QPushButton(tr("Clear"), page);
    clearButton->setProperty("variant", "secondary");
    clearButton->setFont(Theme::font(Theme::TextStyle::Button));
    connect(clearButton, &QPushButton::clicked, &preload_, &ViewModel::Preload::clearCache);

    auto* cacheRow = new QHBoxLayout;
    cacheRow->setSpacing(Theme::Spacing::space2);
    cacheRow->addWidget(cacheSpin_);
    cacheRow->addWidget(usedLabel_, 1);
    cacheRow->addWidget(clearButton);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(Theme::Spacing::space3);
    form->setVerticalSpacing(Theme::Spacing::space2);
    form->addRow(tr("Tracks ahead in a playlist:"), playlistSpin_);
    form->addRow(tr("Tracks ahead in a radio:"), radioSpin_);
    form->addRow(tr("Disk space for tracks:"), cacheRow);

    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(Theme::Spacing::space3);
    pageLayout->addWidget(hint);
    pageLayout->addLayout(form);
    return page;
}

void PreloadPage::updateUsed()
{
    usedLabel_->setText(tr("%1 used").arg(QLocale().formattedDataSize(preload_.cacheUsedBytes())));
}

bool PreloadPage::isDirty() const
{
    if (!playlistSpin_)
        return false;
    return playlistSpin_->value() != preload_.playlistTracks() || radioSpin_->value() != preload_.radioTracks()
        || cacheSpin_->value() != preload_.cacheMaxMb();
}

Rpc::Task<bool> PreloadPage::apply()
{
    if (!playlistSpin_)
        co_return true;
    preload_.setPlaylistTracks(playlistSpin_->value());
    preload_.setRadioTracks(radioSpin_->value());
    preload_.setCacheMaxMb(cacheSpin_->value());
    co_return true;
}

} // namespace Ui::Settings
