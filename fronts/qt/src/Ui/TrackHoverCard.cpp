#include "TrackHoverCard.h"

#include <QAbstractItemView>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>

#include "CoverArtCache.h"
#include "CoverPlaceholder.h"
#include "Icons.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "TextLayout.h"
#include "Tokens.h"
#include "TrackListModel.h"
#include "TrackRowDelegate.h"
#include "Typography.h"

namespace Ui {

namespace {

constexpr int kAnimationMs = 150;
constexpr int kPadding = Theme::Spacing::space3;
constexpr int kCoverSide = 256;
constexpr int kContentWidth = kCoverSide + 2 * kPadding;
constexpr int kCursorOffset = 16;
constexpr int kShadowMargin = 12;
constexpr int kShadowOffsetY = 3;
constexpr int kShadowMaxAlpha = 48;
constexpr int kBadgeSize = 14;

struct CardData {
    Track track;
    QString sourceId;
    QString sourceName;
    QVariant liked; // bool, or invalid while unknown
    QVariant disliked;
    QDateTime lastPlayedAt;
};

QString artistNames(const Track& track)
{
    QStringList names;
    for (const Artist& artist : track.artists)
        names.append(artist.name);
    return names.join(QStringLiteral(", "));
}

QString coverUrlOf(const Track& track)
{
    if (track.coverUrl.has_value() && !track.coverUrl->isEmpty())
        return *track.coverUrl;
    if (track.album.has_value() && track.album->coverUrl.has_value())
        return *track.album->coverUrl;
    return QString();
}

// Floating card, a top-level Qt::ToolTip window like ThemedToolTip's
// popup: translucent outside its rounded shape, never takes focus or
// mouse input, fades in and out.
class CardPopup : public QWidget {
public:
    explicit CardPopup(CoverArtCache* coverCache)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint)
        , coverCache_(coverCache)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);

        opacityAnim_ = new QPropertyAnimation(this, "windowOpacity", this);
        opacityAnim_->setDuration(kAnimationMs);
        opacityAnim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(opacityAnim_, &QPropertyAnimation::finished, this, [this]() {
            if (opacityAnim_->endValue().toReal() <= 0.0)
                hide();
        });
        connect(coverCache_, &CoverArtCache::pixmapReady, this, [this](const QString& url) {
            if (isVisible() && url == coverUrl_)
                update();
        });
    }

    void showCard(const CardData& data, const QPoint& globalPos)
    {
        data_ = data;
        coverUrl_ = coverUrlOf(data.track);
        buildRows();
        const int contentHeight = layoutHeight();
        resize(kContentWidth + 2 * kShadowMargin, contentHeight + 2 * kShadowMargin);
        reposition(globalPos);
        update();
        animateOpacityTo(1.0);
        show();
        raise();
    }

    void fadeOut()
    {
        if (isVisible())
            animateOpacityTo(0.0);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        // Replace, not blend — see ThemedToolTip's identical comment.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const Theme::Palette& pal = Theme::palette();
        const QRect content = rect().adjusted(kShadowMargin, kShadowMargin, -kShadowMargin, -kShadowMargin);
        Theme::paintSoftShadow(&painter, content, kShadowMargin, kShadowOffsetY, kShadowMaxAlpha, Theme::Radius::md);
        QPainterPath panel;
        panel.addRoundedRect(QRectF(content).adjusted(0.5, 0.5, -0.5, -0.5), Theme::Radius::md, Theme::Radius::md);
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface200);
        painter.drawPath(panel);

        // Cover
        const QRect cover(content.left() + kPadding, content.top() + kPadding, kCoverSide, kCoverSide);
        const QPixmap pixmap
            = coverUrl_.isEmpty() ? QPixmap() : coverCache_->pixmap(coverUrl_, QSize(kCoverSide, kCoverSide));
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(cover, Theme::Radius::md, Theme::Radius::md);
        painter.setClipPath(clip);
        if (pixmap.isNull())
            Theme::paintCoverPlaceholder(&painter, cover, data_.sourceId + data_.track.id);
        else
            painter.drawPixmap(cover, pixmap);
        painter.restore();

        int y = cover.bottom() + 1 + kPadding;
        const int textLeft = content.left() + kPadding;
        const int textWidth = kCoverSide;

        // Title (up to 2 lines) + artists (up to 2 lines)
        y = drawWrapped(
            painter, Theme::font(Theme::TextStyle::Title), pal.ink, data_.track.title, textLeft, y, textWidth, 2);
        y += Theme::Spacing::space1;
        y = drawWrapped(painter, Theme::font(Theme::TextStyle::BodySecondary), pal.inkSecondary,
            artistNames(data_.track), textLeft, y, textWidth, 2);

        // Badges: liked / disliked / explicit
        if (hasBadges()) {
            y += Theme::Spacing::space2;
            int x = textLeft;
            const QFont captionFont = Theme::font(Theme::TextStyle::Caption);
            const QFontMetrics captionMetrics(captionFont);
            painter.setFont(captionFont);
            const auto badge
                = [&](const QString& glyph, Theme::IconColor color, const QColor& textColor, const QString& text) {
                      if (!glyph.isEmpty()) {
                          Theme::icon(glyph, color, kBadgeSize)
                              .paint(&painter,
                                  QRect(x, y + (captionMetrics.height() - kBadgeSize) / 2, kBadgeSize, kBadgeSize));
                          x += kBadgeSize + Theme::Spacing::space1;
                      }
                      painter.setPen(textColor);
                      painter.drawText(
                          QRect(x, y, textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, text);
                      x += captionMetrics.horizontalAdvance(text) + Theme::Spacing::space3;
                  };
            if (data_.liked.toBool())
                badge(QStringLiteral("favorite"), Theme::IconColor::Accent, pal.accent, tr("Liked"));
            else if (data_.disliked.toBool())
                badge(QStringLiteral("heart_broken"), Theme::IconColor::InkTertiary, pal.inkSecondary, tr("Disliked"));
            if (data_.track.explicit_.value_or(false))
                badge(QString(), Theme::IconColor::Ink, pal.inkSecondary, tr("Explicit"));
            y += captionMetrics.height();
        }

        // Label / value rows
        y += Theme::Spacing::space2;
        painter.setPen(QPen(pal.border, 1));
        painter.drawLine(textLeft, y, textLeft + textWidth, y);
        y += Theme::Spacing::space2;
        const QFont labelFont = Theme::font(Theme::TextStyle::Caption);
        const QFontMetrics labelMetrics(labelFont);
        painter.setFont(labelFont);
        int labelWidth = 0;
        for (const auto& row : rows_)
            labelWidth = qMax(labelWidth, labelMetrics.horizontalAdvance(row.first));
        const int valueLeft = textLeft + labelWidth + Theme::Spacing::space3;
        const int valueWidth = textLeft + textWidth - valueLeft;
        for (const auto& row : rows_) {
            painter.setPen(pal.inkTertiary);
            painter.drawText(
                QRect(textLeft, y, labelWidth, labelMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, row.first);
            painter.setPen(pal.ink);
            painter.drawText(QRect(valueLeft, y, valueWidth, labelMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                labelMetrics.elidedText(row.second, Qt::ElideRight, valueWidth));
            y += labelMetrics.height() + Theme::Spacing::space1;
        }
    }

private:
    bool hasBadges() const
    {
        return data_.liked.toBool() || data_.disliked.toBool() || data_.track.explicit_.value_or(false);
    }

    void buildRows()
    {
        rows_.clear();
        if (data_.track.album.has_value() && !data_.track.album->title.isEmpty())
            rows_.append({ tr("Album"), data_.track.album->title });
        if (data_.track.durationMs > 0)
            rows_.append({ tr("Duration"), TrackRowDelegate::formatDuration(data_.track.durationMs) });
        if (!data_.sourceName.isEmpty())
            rows_.append({ tr("Source"), data_.sourceName });
        rows_.append({ tr("Last played"),
            data_.lastPlayedAt.isValid() ? TrackRowDelegate::formatPlayedAt(data_.lastPlayedAt) : tr("Never") });
    }

    // Mirrors paintEvent()'s vertical flow.
    int layoutHeight() const
    {
        int h = kPadding + kCoverSide + kPadding;
        h += wrappedHeight(Theme::font(Theme::TextStyle::Title), data_.track.title, 2);
        h += Theme::Spacing::space1;
        h += wrappedHeight(Theme::font(Theme::TextStyle::BodySecondary), artistNames(data_.track), 2);
        const QFontMetrics captionMetrics(Theme::font(Theme::TextStyle::Caption));
        if (hasBadges())
            h += Theme::Spacing::space2 + captionMetrics.height();
        h += Theme::Spacing::space2 + Theme::Spacing::space2;
        h += int(rows_.size()) * (captionMetrics.height() + Theme::Spacing::space1);
        return h + kPadding - Theme::Spacing::space1;
    }

    static int wrappedHeight(const QFont& font, const QString& text, int maxLines)
    {
        return TextLayout::wrappedHeight(font, text, kCoverSide, maxLines);
    }

    // Draws `text` wrapped into at most `maxLines` lines (the last elided)
    // and returns the y just below it.
    static int drawWrapped(QPainter& painter, const QFont& font, const QColor& color, const QString& text, int x, int y,
        int width, int maxLines)
    {
        const int height = TextLayout::wrappedHeight(font, text, width, maxLines);
        painter.setPen(color);
        TextLayout::drawWrapped(painter, font, QRect(x, y, width, height), Qt::AlignLeft, text, maxLines);
        return y + height;
    }

    void animateOpacityTo(qreal target)
    {
        opacityAnim_->stop();
        opacityAnim_->setStartValue(windowOpacity());
        opacityAnim_->setEndValue(target);
        opacityAnim_->start();
    }

    // Beside the cursor, flipped per axis at the screen's edges — same
    // approach as ThemedToolTip's popup.
    void reposition(const QPoint& globalPos)
    {
        const QSize contentSize = size() - QSize(2 * kShadowMargin, 2 * kShadowMargin);
        QRect content(globalPos + QPoint(kCursorOffset, kCursorOffset), contentSize);
        const QScreen* screen = QGuiApplication::screenAt(globalPos);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect avail = screen->availableGeometry();
        if (content.right() > avail.right())
            content.moveRight(globalPos.x() - kCursorOffset);
        if (content.bottom() > avail.bottom())
            content.moveBottom(qMax(avail.top() + contentSize.height(), globalPos.y() - kCursorOffset));
        if (content.top() < avail.top())
            content.moveTop(avail.top());
        setGeometry(QRect(content.topLeft() - QPoint(kShadowMargin, kShadowMargin), size()));
    }

    CoverArtCache* coverCache_;
    CardData data_;
    QString coverUrl_;
    QList<QPair<QString, QString>> rows_;
    QPropertyAnimation* opacityAnim_;
};

} // namespace

void TrackHoverCard::attach(QAbstractItemView* view, CoverArtCache* coverCache, SourceNameFn sourceName)
{
    new TrackHoverCard(view, coverCache, std::move(sourceName));
}

TrackHoverCard::TrackHoverCard(QAbstractItemView* view, CoverArtCache* coverCache, SourceNameFn sourceName)
    : QObject(view)
    , view_(view)
    , coverCache_(coverCache)
    , sourceName_(std::move(sourceName))
{
    view_->viewport()->installEventFilter(this);
    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, this, &TrackHoverCard::hideCard);
}

TrackHoverCard::~TrackHoverCard() { delete popup_; }

bool TrackHoverCard::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != view_->viewport())
        return false;
    switch (event->type()) {
        case QEvent::ToolTip: {
            // The viewport's own tooltip slot — consumed so no plain
            // tooltip shows as well.
            const auto* help = static_cast<QHelpEvent*>(event);
            const QModelIndex index = view_->indexAt(help->pos());
            if (index.isValid())
                showFor(index, help->globalPos());
            else
                hideCard();
            return true;
        }
        case QEvent::MouseMove:
            if (shownIndex_.isValid() && view_->indexAt(static_cast<QMouseEvent*>(event)->pos()) != shownIndex_)
                hideCard();
            break;
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::Hide:
            hideCard();
            break;
        default:
            break;
    }
    return false;
}

void TrackHoverCard::showFor(const QModelIndex& index, const QPoint& globalPos)
{
    // Qt resends ToolTip while the cursor rests — keep the card still.
    if (index == shownIndex_ && popup_ != nullptr && popup_->isVisible())
        return;
    CardData data;
    data.track = index.data(TrackListModel::TrackRole).value<Track>();
    data.sourceId = index.data(TrackListModel::SourceIdRole).toString();
    data.sourceName = sourceName_ ? sourceName_(data.sourceId) : QString();
    data.liked = index.data(TrackListModel::LikedRole);
    data.disliked = index.data(TrackListModel::DislikedRole);
    data.lastPlayedAt = index.data(TrackListModel::LastPlayedRole).toDateTime();
    const QDateTime playedAt = index.data(TrackListModel::PlayedAtRole).toDateTime();
    if (playedAt.isValid() && (!data.lastPlayedAt.isValid() || playedAt > data.lastPlayedAt))
        data.lastPlayedAt = playedAt;

    if (popup_ == nullptr)
        popup_ = new CardPopup(coverCache_);
    shownIndex_ = index;
    static_cast<CardPopup*>(popup_)->showCard(data, globalPos);
}

void TrackHoverCard::hideCard()
{
    shownIndex_ = QPersistentModelIndex();
    if (popup_ != nullptr)
        static_cast<CardPopup*>(popup_)->fadeOut();
}

} // namespace Ui
