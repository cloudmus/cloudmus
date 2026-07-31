#include "TrackRowDelegate.h"

#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "CoverArtCache.h"
#include "TrackListModel.h"

namespace Ui {

namespace {
QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}
} // namespace

TrackRowDelegate::TrackRowDelegate(CoverArtCache* coverCache, QObject* parent)
    : QStyledItemDelegate(parent)
    , coverCache_(coverCache)
{
}

QRect TrackRowDelegate::thumbRect(const QRect& rowRect) const
{
    const int margin = (rowRect.height() - kThumbSize) / 2;
    return QRect(rowRect.left() + margin, rowRect.top() + margin, kThumbSize, kThumbSize);
}

void TrackRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const Track track = index.data(TrackListModel::TrackRole).value<Track>();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    const QRect rect = option.rect;
    const int margin = (rect.height() - kThumbSize) / 2;
    const QRect thumb = thumbRect(rect);

    QPixmap thumbPixmap
        = track.coverUrl ? coverCache_->pixmap(*track.coverUrl, QSize(kThumbSize, kThumbSize)) : QPixmap();
    if (!thumbPixmap.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(thumb, 4, 4);
        painter->setClipPath(clip);
        painter->drawPixmap(thumb, thumbPixmap);
        painter->setClipping(false);
    } else {
        painter->fillRect(thumb, option.palette.alternateBase());
    }

    // Hover-only play button, drawn over the cover thumbnail (the
    // Spotify/YouTube Music convention) rather than as a separate widget —
    // requires the view to have mouse tracking on for State_MouseOver to be
    // set at all (see MainWindow.cpp). editorEvent() below hit-tests clicks
    // against the same thumbRect().
    if (option.state & QStyle::State_MouseOver) {
        QPainterPath clip;
        clip.addRoundedRect(thumb, 4, 4);
        painter->setClipPath(clip);
        painter->fillRect(thumb, QColor(0, 0, 0, 140));
        painter->setClipping(false);

        const QIcon playIcon = QIcon::fromTheme(QStringLiteral("media-playback-start"));
        const QSize iconSize(18, 18);
        const QRect iconRect(thumb.center().x() - iconSize.width() / 2, thumb.center().y() - iconSize.height() / 2,
                             iconSize.width(), iconSize.height());
        playIcon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
    }

    const QColor textColor = option.state & QStyle::State_Selected ? option.palette.color(QPalette::HighlightedText)
                                                                   : option.palette.color(QPalette::Text);
    QColor secondaryColor = textColor;
    secondaryColor.setAlpha(160);

    const QString durationText = formatDuration(track.durationMs);
    const QFontMetrics metrics(option.font);
    const int durationWidth = metrics.horizontalAdvance(durationText);

    const int textLeft = thumb.right() + margin;
    const int textRight = rect.right() - margin - durationWidth - margin;
    const QRect titleRect(textLeft, rect.top() + margin - 2, textRight - textLeft, metrics.height());
    QRect artistRect(textLeft, titleRect.bottom(), textRight - textLeft, metrics.height());

    QString artistNames;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artistNames += QStringLiteral(", ");
        artistNames += track.artists[i].name;
    }

    painter->setPen(textColor);
    painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(track.title, Qt::ElideRight, titleRect.width()));
    painter->setPen(secondaryColor);
    painter->drawText(artistRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(artistNames, Qt::ElideRight, artistRect.width()));

    const QRect durationRect(rect.right() - margin - durationWidth, rect.top(), durationWidth, rect.height());
    painter->setPen(secondaryColor);
    painter->drawText(durationRect, Qt::AlignVCenter | Qt::AlignRight, durationText);

    painter->restore();
}

QSize TrackRowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return QSize(0, kRowHeight); }

bool TrackRowDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                                   const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && thumbRect(option.rect).contains(mouseEvent->pos())) {
            emit playRequested(index);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace Ui
