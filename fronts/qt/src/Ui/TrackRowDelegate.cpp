#include "TrackRowDelegate.h"

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
    const QRect thumbRect(rect.left() + margin, rect.top() + margin, kThumbSize, kThumbSize);

    QPixmap thumb = track.coverUrl ? coverCache_->pixmap(*track.coverUrl, QSize(kThumbSize, kThumbSize)) : QPixmap();
    if (!thumb.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(thumbRect, 4, 4);
        painter->setClipPath(clip);
        painter->drawPixmap(thumbRect, thumb);
        painter->setClipping(false);
    } else {
        painter->fillRect(thumbRect, option.palette.alternateBase());
    }

    const QColor textColor = option.state & QStyle::State_Selected ? option.palette.color(QPalette::HighlightedText)
                                                                   : option.palette.color(QPalette::Text);
    QColor secondaryColor = textColor;
    secondaryColor.setAlpha(160);

    const QString durationText = formatDuration(track.durationMs);
    const QFontMetrics metrics(option.font);
    const int durationWidth = metrics.horizontalAdvance(durationText);

    const int textLeft = thumbRect.right() + margin;
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

} // namespace Ui
