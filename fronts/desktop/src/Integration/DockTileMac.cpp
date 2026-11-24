#include "DockTile.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QPixmap>
#include <QSize>

#include "CoverArtCache.h"
#include "DockIcon.h"
#include "NowPlaying.h"

namespace Integration {

using ViewModel::NowPlaying;

namespace {

// See DockTile::setShowsCover().
bool showsCover = false;
DockTile* liveTile = nullptr;

// The Dock draws its icons at up to 256px (128pt at 2x); the cover is
// drawn into DockIcon's 1024px canvas, so a bit more keeps it sharp.
constexpr QSize kCoverSize { 824, 824 };

QString coverUrlOf(const Track& track)
{
    QString url = track.coverUrl.value_or(QString());
    if (url.isEmpty() && track.album.has_value())
        url = track.album->coverUrl.value_or(QString());
    return url;
}

} // namespace

DockTile::DockTile(NowPlaying& nowPlaying, Covers::CoverArtCache& covers, QObject* parent)
    : QObject(parent)
    , nowPlaying_(nowPlaying)
    , covers_(covers)
{
    // The native Dock menu: no styling, no icons — macOS draws it like the
    // Dock menus of other apps. The track's rows are text, not actions.
    menu_ = new QMenu();
    titleAction_ = menu_->addAction(QString());
    titleAction_->setEnabled(false);
    detailsAction_ = menu_->addAction(QString());
    detailsAction_->setEnabled(false);
    trackSeparator_ = menu_->addSeparator();
    playPauseAction_ = menu_->addAction(QString());
    nextAction_ = menu_->addAction(QString());
    previousAction_ = menu_->addAction(QString());
    feedbackSeparator_ = menu_->addSeparator();
    likeAction_ = menu_->addAction(QString());
    dislikeAction_ = menu_->addAction(QString());

    connect(playPauseAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::togglePause);
    connect(nextAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::next);
    connect(previousAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::previous);
    connect(likeAction_, &QAction::triggered, this, [this]() { nowPlaying_.setLiked(!nowPlaying_.feedback().liked); });
    connect(dislikeAction_, &QAction::triggered, this,
        [this]() { nowPlaying_.setDisliked(!nowPlaying_.feedback().disliked); });

    connect(&nowPlaying_, &NowPlaying::trackChanged, this, &DockTile::refreshTrack);
    connect(&nowPlaying_, &NowPlaying::playingChanged, this, &DockTile::refreshPlaying);
    connect(&nowPlaying_, &NowPlaying::feedbackChanged, this, &DockTile::refreshFeedbackActions);
    connect(&covers_, &Covers::CoverArtCache::pixmapReady, this, &DockTile::onCoverReady);

    menu_->setAsDockMenu();
    retranslate();
    refreshTrack();

    liveTile = this;
    // QCoreApplication sends LanguageChange to itself when a translator is installed.
    qApp->installEventFilter(this);
}

DockTile::~DockTile()
{
    if (liveTile == this)
        liveTile = nullptr;
    DockIcon::setCover(QPixmap());
    delete menu_;
}

void DockTile::setShowsCover(bool value)
{
    if (showsCover == value)
        return;
    showsCover = value;
    if (liveTile)
        liveTile->refreshCover();
}

bool DockTile::eventFilter(QObject* watched, QEvent* event)
{
    // Relabeled on the next turn, as TrayIcon::eventFilter() explains.
    if (watched == qApp && event->type() == QEvent::LanguageChange && !retranslatePending_) {
        retranslatePending_ = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                retranslatePending_ = false;
                retranslate();
            },
            Qt::QueuedConnection);
    }
    return QObject::eventFilter(watched, event);
}

void DockTile::retranslate()
{
    nextAction_->setText(tr("Next"));
    previousAction_->setText(tr("Previous"));
    refreshPlaying();
    refreshFeedbackActions();
}

void DockTile::refreshTrack()
{
    const bool hasTrack = nowPlaying_.hasTrack();
    titleAction_->setVisible(hasTrack);
    detailsAction_->setVisible(hasTrack);
    trackSeparator_->setVisible(hasTrack);
    if (hasTrack) {
        const Track& track = nowPlaying_.track();
        QString artists;
        for (int i = 0; i < track.artists.size(); ++i) {
            if (i > 0)
                artists += QStringLiteral(", ");
            artists += track.artists[i].name;
        }
        const QString album = track.album.has_value() ? track.album->title : QString();
        titleAction_->setText(track.title);
        detailsAction_->setText(
            artists.isEmpty() || album.isEmpty() ? artists + album : QStringLiteral("%1 — %2").arg(artists, album));
        detailsAction_->setVisible(!detailsAction_->text().isEmpty());
    }
    refreshCover();
}

void DockTile::refreshPlaying() { playPauseAction_->setText(nowPlaying_.playing() ? tr("Pause") : tr("Play")); }

void DockTile::refreshFeedbackActions()
{
    const NowPlaying::Feedback feedback = nowPlaying_.feedback();
    likeAction_->setVisible(feedback.likeSupported);
    likeAction_->setText(feedback.liked ? tr("Unlike") : tr("Like"));
    dislikeAction_->setVisible(feedback.dislikeSupported);
    dislikeAction_->setText(feedback.disliked ? tr("Remove Dislike") : tr("Dislike"));
    feedbackSeparator_->setVisible(feedback.likeSupported || feedback.dislikeSupported);
}

void DockTile::refreshCover()
{
    pendingCoverUrl_.clear();
    const QString url = showsCover && nowPlaying_.hasTrack() ? coverUrlOf(nowPlaying_.track()) : QString();
    if (url.isEmpty()) {
        DockIcon::setCover(QPixmap());
        return;
    }
    const QPixmap cover = covers_.pixmap(url, kCoverSize);
    if (cover.isNull()) {
        // The previous track's cover stays until this one arrives, rather
        // than the logo flashing in between.
        pendingCoverUrl_ = url;
        return;
    }
    DockIcon::setCover(cover);
}

void DockTile::onCoverReady(const QString& url)
{
    if (pendingCoverUrl_.isEmpty() || url != pendingCoverUrl_)
        return;
    const QPixmap cover = covers_.pixmap(url, kCoverSize);
    if (cover.isNull())
        return; // another size of the same url landed; ours is still coming
    pendingCoverUrl_.clear();
    DockIcon::setCover(cover);
}

} // namespace Integration
