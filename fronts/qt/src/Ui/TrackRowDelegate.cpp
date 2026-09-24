#include "TrackRowDelegate.h"

#include <QEasingCurve>
#include <QFont>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include "CoverArtCache.h"
#include "CoverPlaceholder.h"
#include "Icons.h"
#include "Radius.h"
#include "Spacing.h"
#include "Tokens.h"
#include "TrackListModel.h"
#include "Typography.h"

namespace Ui {

namespace {
void fillRoundedRect(QPainter* painter, const QRect& rect, const QColor& color, int radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    painter->fillPath(path, color);
}

constexpr int kEqualizerWidth = 12;
constexpr int kEqualizerGap = Theme::Spacing::space2;

// 3 uneven, fixed-height bars — not animated. Drawn immediately left of the
// duration column on the currently-playing row (see paint()).
void paintEqualizerGlyph(QPainter* painter, const QRect& rect, const QColor& color)
{
    static const int kBarHeights[3] = { 5, 12, 8 };
    constexpr int kBarWidth = 2;
    constexpr int kBarGap = 2;
    const int totalWidth = 3 * kBarWidth + 2 * kBarGap;
    int maxBarHeight = kBarHeights[0];
    for (int h : kBarHeights)
        maxBarHeight = qMax(maxBarHeight, h);
    // Bars share a baseline (classic equalizer look), but that baseline
    // itself is centered in `rect` via the tallest bar's own extent — not
    // pinned to rect.bottom(), which glued the whole glyph to the bottom
    // of the (full row-height) rect it's given instead of centering it.
    const int baseline = rect.center().y() + maxBarHeight / 2;
    int x = rect.center().x() - totalWidth / 2;
    for (int barHeight : kBarHeights) {
        const QRect bar(x, baseline - barHeight, kBarWidth, barHeight);
        painter->fillRect(bar, color);
        x += kBarWidth + kBarGap;
    }
}
} // namespace

namespace {
constexpr int kCoverFadeMs = 250;
} // namespace

TrackRowDelegate::TrackRowDelegate(CoverArtCache* coverCache, QObject* parent)
    : QStyledItemDelegate(parent)
    , coverCache_(coverCache)
{
    clock_.start();
    fadeTimer_ = new QTimer(this);
    fadeTimer_->setInterval(16);
    connect(fadeTimer_, &QTimer::timeout, this, &TrackRowDelegate::tickFades);
}

void TrackRowDelegate::tickFades()
{
    const qint64 now = clock_.elapsed();
    for (auto it = fadeStartMs_.begin(); it != fadeStartMs_.end();) {
        // A frame past the end, so the final fully-opaque paint happens.
        if (now - it.value() > kCoverFadeMs + 32)
            it = fadeStartMs_.erase(it);
        else
            ++it;
    }
    for (const QPointer<QWidget>& view : std::as_const(fadingViews_)) {
        if (view)
            view->update();
    }
    if (fadeStartMs_.isEmpty()) {
        fadeTimer_->stop();
        fadingViews_.clear();
    }
}

void TrackRowDelegate::paintThumb(
    QPainter* painter, const QRect& thumb, const QString& coverUrl, const QString& stableId, const QWidget* view) const
{
    const QPixmap pixmap = coverUrl.isEmpty() ? QPixmap() : coverCache_->pixmap(coverUrl, thumb.size());
    if (pixmap.isNull()) {
        Theme::paintCoverPlaceholder(painter, thumb, stableId);
        if (!coverUrl.isEmpty())
            placeholderShown_.insert(coverUrl);
        return;
    }

    // Arrived since this URL was last painted as a placeholder: start its
    // fade-in. Covers already cached (scrolling back, reopening a list)
    // never showed the placeholder and just appear.
    if (placeholderShown_.remove(coverUrl)) {
        fadeStartMs_.insert(coverUrl, clock_.elapsed());
        if (!fadeTimer_->isActive())
            fadeTimer_->start();
    }
    qreal opacity = 1.0;
    if (auto it = fadeStartMs_.constFind(coverUrl); it != fadeStartMs_.constEnd()) {
        const qreal t = qBound(0.0, qreal(clock_.elapsed() - it.value()) / kCoverFadeMs, 1.0);
        opacity = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);
        if (opacity < 1.0) {
            Theme::paintCoverPlaceholder(painter, thumb, stableId);
            if (view != nullptr)
                fadingViews_.insert(const_cast<QWidget*>(view), const_cast<QWidget*>(view));
        }
    }

    painter->save();
    QPainterPath clip;
    clip.addRoundedRect(thumb, Theme::Radius::coverArtSm, Theme::Radius::coverArtSm);
    painter->setClipPath(clip);
    painter->setOpacity(painter->opacity() * opacity);
    painter->drawPixmap(thumb, pixmap);
    painter->restore();
}

void TrackRowDelegate::setCurrentlyPlaying(const QString& sourceId, const QString& trackId)
{
    currentSourceId_ = sourceId;
    currentTrackId_ = trackId;
}

void TrackRowDelegate::setRowInsets(int left, int right)
{
    insetLeft_ = left;
    insetRight_ = right;
}

QRect TrackRowDelegate::thumbRect(const QRect& rowRect) const
{
    const int margin = (rowRect.height() - kThumbSize) / 2;
    return QRect(rowRect.left() + margin, rowRect.top() + margin, kThumbSize, kThumbSize);
}

QString TrackRowDelegate::formatDuration(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString TrackRowDelegate::formatPlayedAt(const QDateTime& utcWhen)
{
    const QDateTime local = utcWhen.toLocalTime();
    const QDate today = QDate::currentDate();
    const QDate date = local.date();
    QString datePart;
    if (date == today)
        datePart = tr("Today");
    else if (date == today.addDays(-1))
        datePart = tr("Yesterday");
    else
        datePart = local.date().toString(QStringLiteral("dd.MM.yyyy"));
    return QStringLiteral("%1, %2").arg(datePart, local.time().toString(QStringLiteral("HH:mm")));
}

void TrackRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const Track track = index.data(TrackListModel::TrackRole).value<Track>();
    const Theme::Palette& pal = Theme::palette();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool isCurrentTrack = !currentTrackId_.isEmpty() && track.id == currentTrackId_
        && index.data(TrackListModel::SourceIdRole).toString() == currentSourceId_;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // Selected and playing render identically per the design system's
    // States table (surface-400 + accent text/icon) — this app has no
    // persistent multi-select browsing UX beyond single-click activation,
    // so the two concepts reading the same way is not a meaningful loss.
    const QRect rect = insetRow(option.rect);
    if (selected || isCurrentTrack)
        fillRoundedRect(painter, rect, pal.surface400, Theme::Radius::md);
    else if (hovered)
        fillRoundedRect(painter, rect, pal.surface300, Theme::Radius::md);

    const int margin = (rect.height() - kThumbSize) / 2;
    const QRect thumb = thumbRect(rect);

    paintThumb(painter, thumb, track.coverUrl.value_or(QString()),
        index.data(TrackListModel::SourceIdRole).toString() + track.id, option.widget);

    // Hover-only play button, drawn over the cover thumbnail (the
    // Spotify/YouTube Music convention) rather than as a separate widget —
    // requires the view to have mouse tracking on for State_MouseOver to be
    // set at all (see MainWindow.cpp). editorEvent() below hit-tests clicks
    // against the same thumbRect().
    if (hovered) {
        QPainterPath clip;
        clip.addRoundedRect(thumb, Theme::Radius::coverArtSm, Theme::Radius::coverArtSm);
        painter->setClipPath(clip);
        painter->fillRect(thumb, QColor(0, 0, 0, 140));
        painter->setClipping(false);

        // Always white: it sits over the fixed dark scrim above, not over a
        // theme surface — a theme token like Ink turns near-black in the
        // light theme and vanishes against the scrim.
        const QIcon playIcon = Theme::iconWithColor(QStringLiteral("play_arrow"), QColor(255, 255, 255), 18);
        const QSize iconSize(18, 18);
        const QRect iconRect(thumb.center().x() - iconSize.width() / 2, thumb.center().y() - iconSize.height() / 2,
            iconSize.width(), iconSize.height());
        playIcon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
    }

    const QColor textColor = (selected || isCurrentTrack) ? pal.accent : pal.ink;
    const QColor secondaryColor = pal.inkSecondary;

    const QFont titleFont = Theme::font(Theme::TextStyle::Title, option.font);
    const QFont secondaryFont = Theme::font(Theme::TextStyle::BodySecondary, option.font);
    const QFont captionFont = Theme::tabularFont(Theme::TextStyle::Caption, option.font);
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics secondaryMetrics(secondaryFont);
    const QFontMetrics captionMetrics(captionFont);

    const QString durationText = formatDuration(track.durationMs);
    const int durationWidth = captionMetrics.horizontalAdvance(durationText);

    // History rows (see TrackListModel::PlayedAtRole) get a second stacked
    // label here, mirroring the title/artist stack on the left — mixing in
    // an invalid QDateTime for a non-history row is exactly what leaves
    // playedAtText empty and this whole block a no-op. (When a track was
    // last played in general is on its hover card — see TrackHoverCard.)
    const QDateTime playedAt = index.data(TrackListModel::PlayedAtRole).toDateTime();
    const QString playedAtText = playedAt.isValid() ? formatPlayedAt(playedAt) : QString();
    const int playedAtWidth = playedAtText.isEmpty() ? 0 : captionMetrics.horizontalAdvance(playedAtText);
    const int rightColumnWidth = qMax(durationWidth, playedAtWidth);
    // The 3-bar equalizer sits immediately left of the duration column on
    // the playing row — it sits *before* the duration rather than replacing
    // it, so remaining/elapsed context stays visible (design system allows
    // either; this app keeps the duration).
    const int equalizerReserve = isCurrentTrack ? kEqualizerWidth + kEqualizerGap : 0;

    // Liked / disliked badge, left of the equalizer and the right column.
    const bool liked = index.data(TrackListModel::LikedRole).toBool();
    const bool disliked = !liked && index.data(TrackListModel::DislikedRole).toBool();
    const int badgeReserve = (liked || disliked) ? kBadgeSize + kEqualizerGap : 0;
    if (liked || disliked) {
        const QRect badgeRect(rect.right() - margin - rightColumnWidth - equalizerReserve - badgeReserve + 1,
            rect.center().y() - kBadgeSize / 2, kBadgeSize, kBadgeSize);
        Theme::icon(liked ? QStringLiteral("favorite") : QStringLiteral("heart_broken"),
            liked ? Theme::IconColor::Accent : Theme::IconColor::InkTertiary, kBadgeSize)
            .paint(painter, badgeRect);
    }

    const int textLeft = thumb.right() + margin;
    const int textRight = rect.right() - margin - rightColumnWidth - equalizerReserve - badgeReserve - margin;
    const QRect titleRect(textLeft, rect.top() + margin - 2, textRight - textLeft, titleMetrics.height());
    const QRect artistRect(textLeft, titleRect.bottom(), textRight - textLeft, secondaryMetrics.height());

    QString artistNames;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artistNames += QStringLiteral(", ");
        artistNames += track.artists[i].name;
    }

    painter->setPen(textColor);
    painter->setFont(titleFont);
    painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
        titleMetrics.elidedText(track.title, Qt::ElideRight, titleRect.width()));

    painter->setPen(secondaryColor);
    painter->setFont(secondaryFont);
    painter->drawText(artistRect, Qt::AlignVCenter | Qt::AlignLeft,
        secondaryMetrics.elidedText(artistNames, Qt::ElideRight, artistRect.width()));

    if (isCurrentTrack) {
        const QRect eqRect(
            rect.right() - margin - rightColumnWidth - equalizerReserve, rect.top(), kEqualizerWidth, rect.height());
        paintEqualizerGlyph(painter, eqRect, pal.accent);
    }

    painter->setFont(captionFont);
    painter->setPen(secondaryColor);
    if (!playedAtText.isEmpty()) {
        const QRect playedAtRect(rect.right() - margin - rightColumnWidth, rect.top() + margin - 2, rightColumnWidth,
            captionMetrics.height());
        const QRect durationRect2(
            rect.right() - margin - rightColumnWidth, playedAtRect.bottom(), rightColumnWidth, captionMetrics.height());
        painter->drawText(playedAtRect, Qt::AlignVCenter | Qt::AlignRight, playedAtText);
        painter->drawText(durationRect2, Qt::AlignVCenter | Qt::AlignRight, durationText);
    } else {
        const QRect durationRect(rect.right() - margin - durationWidth, rect.top(), durationWidth, rect.height());
        painter->drawText(durationRect, Qt::AlignVCenter | Qt::AlignRight, durationText);
    }

    painter->restore();
}

QSize TrackRowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return QSize(0, kRowHeight); }

bool TrackRowDelegate::editorEvent(
    QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && thumbRect(insetRow(option.rect)).contains(mouseEvent->pos())) {
            emit playRequested(index);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace Ui
