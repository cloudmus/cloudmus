#include "Settings/PreloadPage.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include "Metrics.h"
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

    reserveSpin_ = new QSpinBox(page);
    reserveSpin_->setFont(Theme::font(Theme::TextStyle::Body));
    reserveSpin_->setRange(0, ViewModel::Preload::kMaxReserveMb);
    reserveSpin_->setSingleStep(256);
    reserveSpin_->setSuffix(tr(" MB"));
    reserveSpin_->setValue(preload_.reserveMb());
    connect(reserveSpin_, &QSpinBox::valueChanged, this, &Page::dirtyChanged);
    auto* reserveHint = new QLabel(tr("When less than this is left free, old tracks and covers are removed first; then "
                                      "tracks play without being downloaded, and nothing new is saved."),
        page);
    reserveHint->setProperty("hint", true);
    reserveHint->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    reserveHint->setWordWrap(true);
    // What there is to keep free, beside how much to keep.
    freeLabel_ = new QLabel(page);
    freeLabel_->setProperty("hint", true);
    freeLabel_->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    connect(&preload_, &ViewModel::Preload::diskFreeChanged, freeLabel_, [this]() { updateFree(); });
    updateFree();
    auto* reserveRow = new QHBoxLayout;
    reserveRow->setSpacing(Theme::Spacing::space2);
    reserveRow->addWidget(reserveSpin_);
    reserveRow->addWidget(freeLabel_, 1);
    auto* reserveField = new QVBoxLayout;
    reserveField->setSpacing(Theme::Spacing::space2);
    reserveField->addLayout(reserveRow);
    reserveField->addWidget(reserveHint);

    usedLabel_ = new QLabel(page);
    usedLabel_->setProperty("hint", true);
    usedLabel_->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    // How full the space set aside is; red when nearly out. Follows the
    // limit as applied, not the one being typed in.
    usedBar_ = new QProgressBar(page);
    usedBar_->setProperty("themed", true); // see StyleSheet.cpp's progressBarBlock()
    usedBar_->setProperty("meter", true);
    usedBar_->setRange(0, 1000);
    usedBar_->setTextVisible(false);
    // Exactly the height its rounded corners are drawn for.
    usedBar_->setFixedHeight(Theme::Metrics::meterHeight);
    connect(&preload_, &ViewModel::Preload::cacheUsedChanged, usedLabel_, [this]() { updateUsed(); });
    connect(&preload_, &ViewModel::Preload::settingsChanged, usedBar_, [this]() { updateUsed(); });
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
    auto* cacheField = new QVBoxLayout;
    cacheField->setSpacing(Theme::Spacing::space2);
    cacheField->addLayout(cacheRow);
    cacheField->addWidget(usedBar_);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(Theme::Spacing::space3);
    form->setVerticalSpacing(Theme::Spacing::space2);
    form->addRow(tr("Tracks ahead in a playlist:"), playlistSpin_);
    form->addRow(tr("Tracks ahead in a radio:"), radioSpin_);
    form->addRow(tr("Disk space for tracks:"), cacheField);
    form->addRow(tr("Keep free on the disk:"), reserveField);

    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(Theme::Spacing::space3);
    pageLayout->addWidget(hint);
    pageLayout->addLayout(form);
    auto* spaceWarning = new QLabel(page);
    spaceWarning->setProperty("error", true);
    spaceWarning->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    spaceWarning->setWordWrap(true);
    const auto showSpaceWarning = [this, spaceWarning]() {
        spaceWarning->setText(preload_.spaceWarning());
        spaceWarning->setVisible(!spaceWarning->text().isEmpty());
    };
    connect(&preload_, &ViewModel::Preload::spaceWarningChanged, spaceWarning, showSpaceWarning);
    showSpaceWarning();
    pageLayout->addWidget(spaceWarning);
    return page;
}

void PreloadPage::updateUsed()
{
    usedLabel_->setText(tr("%1 used").arg(QLocale().formattedDataSize(preload_.cacheUsedBytes())));
    const double fraction = preload_.cacheUsedFraction();
    usedBar_->setValue(qRound(fraction * usedBar_->maximum()));
    const bool nearlyFull = fraction > kNearlyFull;
    if (usedBar_->property("nearlyFull").toBool() != nearlyFull) {
        usedBar_->setProperty("nearlyFull", nearlyFull);
        // A dynamic property in a selector is only re-read on a re-polish.
        usedBar_->style()->unpolish(usedBar_);
        usedBar_->style()->polish(usedBar_);
    }
}

void PreloadPage::updateFree()
{
    const qint64 free = preload_.diskFreeBytes();
    freeLabel_->setText(free < 0 ? QString() : tr("%1 free now").arg(QLocale().formattedDataSize(free)));
}

bool PreloadPage::isDirty() const
{
    if (!playlistSpin_)
        return false;
    return playlistSpin_->value() != preload_.playlistTracks() || radioSpin_->value() != preload_.radioTracks()
        || cacheSpin_->value() != preload_.cacheMaxMb() || reserveSpin_->value() != preload_.reserveMb();
}

Rpc::Task<bool> PreloadPage::apply()
{
    if (!playlistSpin_)
        co_return true;
    preload_.setPlaylistTracks(playlistSpin_->value());
    preload_.setRadioTracks(radioSpin_->value());
    preload_.setCacheMaxMb(cacheSpin_->value());
    preload_.setReserveMb(reserveSpin_->value());
    co_return true;
}

} // namespace Ui::Settings
