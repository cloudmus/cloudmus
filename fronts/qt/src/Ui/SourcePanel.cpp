#include "SourcePanel.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "AuthCard.h"
#include "CoverArtCache.h"
#include "GeneratedCoverArt.h"
#include "Icons.h"
#include "Models.h"
#include "OverlayScrollBar.h"
#include "Radius.h"
#include "ScrollEdgeFade.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "TabOrder.h"
#include "Tokens.h"
#include "TrackListModel.h"
#include "Typography.h"

namespace Ui {

namespace {
constexpr int kIconSize = 32;
constexpr int kCardMaxWidth = 520;
constexpr int kColumnMaxWidth = 640;
constexpr int kPlaylistRowHeight = 44;
constexpr int kPlaylistThumb = 32;
constexpr int kStarSide = 18;

// Small self-painted pieces — painted with the current Theme::palette()
// at paint time (no QSS), like the rest of the app's custom widgets.

// A section heading, same LabelUpper treatment as the sidebar's "PLAYLISTS".
class SectionTitle : public QWidget {
public:
    SectionTitle(const QString& text, QWidget* parent)
        : QWidget(parent)
        , text_(text.toUpper())
    {
        setFixedHeight(QFontMetrics(Theme::font(Theme::TextStyle::LabelUpper)).height() + Theme::Spacing::space1);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return { QFontMetrics(Theme::font(Theme::TextStyle::LabelUpper)).horizontalAdvance(text_), height() };
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setFont(Theme::font(Theme::TextStyle::LabelUpper));
        painter.setPen(Theme::palette().inkTertiary);
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, text_);
    }

private:
    QString text_;
};

// A line of secondary text, optionally led by a glyph.
class HintLine : public QWidget {
public:
    HintLine(QWidget* parent, const QString& glyph = QString())
        : QWidget(parent)
        , glyph_(glyph)
    {
        setFixedHeight(24);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setText(const QString& text)
    {
        text_ = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        int x = 0;
        if (!glyph_.isEmpty()) {
            constexpr int kSide = 16;
            Theme::icon(glyph_, Theme::IconColor::Accent, kSide)
                .paint(&painter, QRect(0, (height() - kSide) / 2, kSide, kSide));
            x = kSide + Theme::Spacing::space2;
        }
        painter.setFont(Theme::font(Theme::TextStyle::BodySecondary));
        painter.setPen(Theme::palette().inkSecondary);
        painter.drawText(rect().adjusted(x, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text_);
    }

private:
    QString glyph_;
    QString text_;
};

// A capability pill.
class Chip : public QWidget {
public:
    Chip(const QString& text, QWidget* parent)
        : QWidget(parent)
        , text_(text)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(Theme::font(Theme::TextStyle::Caption));
        return { metrics.horizontalAdvance(text_) + 2 * Theme::Spacing::space3,
            metrics.height() + 2 * Theme::Spacing::space1 };
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Theme::Palette& pal = Theme::palette();
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface200);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        painter.setFont(Theme::font(Theme::TextStyle::Caption));
        painter.setPen(pal.inkSecondary);
        painter.drawText(rect(), Qt::AlignCenter, text_);
    }

private:
    QString text_;
};

// The source's playlists as clickable rows: cover thumbnail, title and a
// "N tracks" / "Radio" caption; hover highlight like the track lists. A
// star at the right edge — filled on favorites, an outline on the hovered
// row otherwise — toggles whether the sidebar lists it.
class PlaylistRows : public QWidget {
public:
    PlaylistRows(Covers::CoverArtCache* coverCache, QWidget* parent)
        : QWidget(parent)
        , coverCache_(coverCache)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(coverCache_, &Covers::CoverArtCache::pixmapReady, this, qOverload<>(&QWidget::update));
    }

    void setPlaylists(const QList<Playlist>& playlists)
    {
        playlists_ = playlists;
        hovered_ = -1;
        setFixedHeight(int(playlists_.size()) * kPlaylistRowHeight);
        update();
    }

    std::function<void(const Playlist&)> onActivated;
    std::function<void(const Playlist&)> onFavoriteToggled;
    std::function<bool(const Playlist&)> isFavorite;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const Theme::Palette& pal = Theme::palette();
        const QFont titleFont = Theme::font(Theme::TextStyle::Body);
        const QFont captionFont = Theme::font(Theme::TextStyle::Caption);
        const QFontMetrics titleMetrics(titleFont);
        const QFontMetrics captionMetrics(captionFont);
        for (int i = 0; i < playlists_.size(); ++i) {
            const Playlist& p = playlists_[i];
            const QRect row(0, i * kPlaylistRowHeight, width(), kPlaylistRowHeight);
            if (i == hovered_) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(pal.surface300);
                painter.drawRoundedRect(row, Theme::Radius::md, Theme::Radius::md);
            }
            const int pad = (kPlaylistRowHeight - kPlaylistThumb) / 2;
            const QRect thumb(row.left() + pad, row.top() + pad, kPlaylistThumb, kPlaylistThumb);
            painter.save();
            QPainterPath clip;
            clip.addRoundedRect(thumb, Theme::Radius::sm, Theme::Radius::sm);
            painter.setClipPath(clip);
            QPixmap cover;
            if (p.coverUrl.has_value() && !p.coverUrl->isEmpty())
                cover = coverCache_->pixmap(*p.coverUrl, thumb.size());
            if (cover.isNull())
                cover = generatedCover(p.title);
            painter.drawPixmap(thumb, cover);
            painter.restore();

            const bool favorite = isFavorite && isFavorite(p);
            if (favorite || i == hovered_) {
                const QRect star = starRect(row);
                const QRect glyph(
                    star.center().x() - kStarSide / 2 + 1, star.center().y() - kStarSide / 2 + 1, kStarSide, kStarSide);
                const bool starHovered = i == hovered_ && starHovered_;
                Theme::icon(favorite ? QStringLiteral("star") : QStringLiteral("star_border"),
                    favorite ? Theme::IconColor::Accent
                             : (starHovered ? Theme::IconColor::Ink : Theme::IconColor::InkTertiary),
                    kStarSide)
                    .paint(&painter, glyph);
            }

            const int textLeft = thumb.right() + 1 + Theme::Spacing::space3;
            const int textWidth = starRect(row).left() - textLeft - Theme::Spacing::space2;
            const int blockTop = row.top() + (row.height() - titleMetrics.height() - captionMetrics.height()) / 2;
            painter.setFont(titleFont);
            painter.setPen(pal.ink);
            painter.drawText(QRect(textLeft, blockTop, textWidth, titleMetrics.height()),
                Qt::AlignLeft | Qt::AlignVCenter, titleMetrics.elidedText(p.title, Qt::ElideRight, textWidth));
            painter.setFont(captionFont);
            painter.setPen(pal.inkSecondary);
            painter.drawText(QRect(textLeft, blockTop + titleMetrics.height(), textWidth, captionMetrics.height()),
                Qt::AlignLeft | Qt::AlignVCenter, caption(p));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint pos = event->position().toPoint();
        const int row = rowAt(pos);
        const bool onStar = row >= 0 && starRect(rowRect(row)).contains(pos);
        if (onStar != starHovered_) {
            starHovered_ = onStar;
            setToolTip(onStar ? (isFavorite && isFavorite(playlists_[row]) ? QObject::tr("Remove from sidebar")
                                                                           : QObject::tr("Add to sidebar"))
                              : QString());
            update();
        }
        setHovered(row);
    }
    void leaveEvent(QEvent*) override
    {
        starHovered_ = false;
        setHovered(-1);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const QPoint pos = event->position().toPoint();
        const int row = rowAt(pos);
        if (event->button() != Qt::LeftButton || row < 0)
            return;
        if (starRect(rowRect(row)).contains(pos)) {
            if (onFavoriteToggled)
                onFavoriteToggled(playlists_[row]);
        } else if (onActivated) {
            onActivated(playlists_[row]);
        }
    }

private:
    QRect rowRect(int row) const { return QRect(0, row * kPlaylistRowHeight, width(), kPlaylistRowHeight); }

    // A square hit area at the row's right edge, as tall as the thumbnail.
    static QRect starRect(const QRect& row)
    {
        const int pad = (kPlaylistRowHeight - kPlaylistThumb) / 2;
        return QRect(row.right() + 1 - pad - kPlaylistThumb, row.top() + pad, kPlaylistThumb, kPlaylistThumb);
    }

    int rowAt(const QPoint& pos) const
    {
        const int row = pos.y() / kPlaylistRowHeight;
        return rect().contains(pos) && row < playlists_.size() ? row : -1;
    }

    void setHovered(int row)
    {
        if (row == hovered_)
            return;
        hovered_ = row;
        setCursor(row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }

    static QString caption(const Playlist& p)
    {
        if (p.kind == QStringLiteral("radioStation"))
            return QObject::tr("Radio");
        return trackCountText(p.trackCount);
    }

    QPixmap generatedCover(const QString& seed)
    {
        auto it = generated_.find(seed);
        if (it == generated_.end())
            it = generated_.insert(
                seed, generateMeshAuraGradientCover(seed, QSize(kPlaylistThumb, kPlaylistThumb) * devicePixelRatioF()));
        return it.value();
    }

    Covers::CoverArtCache* coverCache_;
    QList<Playlist> playlists_;
    QHash<QString, QPixmap> generated_;
    int hovered_ = -1;
    bool starHovered_ = false;
};
} // namespace

SourcePanel::SourcePanel(Covers::CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
{
    // Everything lives in one scrollable, top-aligned column — the auth
    // card can get tall (a multiline paste field) and a source can have
    // many playlists.
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* column = new QWidget(scroll);
    scroll->setWidget(column);
    // After setWidget(), which turns the widget's autoFillBackground on —
    // the column is transparent over the sheet's own background.
    column->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    SmoothScroller::attach(scroll);
    // The sheet's own background shows through this transparent column.
    ScrollEdgeFade::attach(scroll, [] { return Theme::palette().surface0; });
    OverlayScrollBar::attach(scroll);

    authCard_ = new AuthCard(AuthCard::Look::Card, column);
    authCard_->setMaximumWidth(kCardMaxWidth);
    connect(authCard_, &AuthCard::submitRequested, this,
        [this](const QJsonObject& fields) { emit submitRequested(currentSourceId_, fields); });
    connect(authCard_, &AuthCard::retryRequested, this, [this]() { emit retryRequested(currentSourceId_); });
    connect(authCard_, &AuthCard::codeCopied, this, &SourcePanel::codeCopied);
    authCard_->hide();

    auto* status = new HintLine(column, QStringLiteral("check"));
    status->setText(tr("Connected"));
    statusRow_ = status;

    auto* chipsRow = new QWidget(column);
    chipsLayout_ = new QHBoxLayout(chipsRow);
    chipsLayout_->setContentsMargins(0, 0, 0, 0);
    chipsLayout_->setSpacing(Theme::Spacing::space2);

    // A small round refresh button right beside the "PLAYLISTS" heading,
    // sized to the heading's own line so it reads as part of it.
    auto* playlistsTitle = new SectionTitle(tr("Playlists"), column);
    // 20px: the heading's own line height, and exactly twice the
    // [compact="true"] QSS radius, so it's a true circle.
    constexpr int compactSide = 20;
    refreshButton_ = new QToolButton(column);
    refreshButton_->setProperty("variant", "icon");
    refreshButton_->setProperty("filled", true);
    refreshButton_->setProperty("compact", true); // radius from StyleSheet.cpp's buttonsBlock()
    refreshButton_->setToolTip(tr("Refresh playlists"));
    Theme::followTheme(refreshButton_, [this]() {
        refreshButton_->setIcon(Theme::icon(QStringLiteral("refresh"), Theme::IconColor::InkSecondary, 14));
    });
    refreshButton_->setIconSize(QSize(14, 14));
    refreshButton_->setFixedSize(compactSide, compactSide);
    connect(refreshButton_, &QToolButton::clicked, this, [this]() { emit refreshRequested(currentSourceId_); });
    auto* playlistsHeader = new QHBoxLayout;
    playlistsHeader->setSpacing(Theme::Spacing::space2);
    playlistsHeader->addWidget(playlistsTitle, 0, Qt::AlignVCenter);
    playlistsHeader->addWidget(refreshButton_, 0, Qt::AlignVCenter);
    playlistsHeader->addStretch(1);

    auto* rows = new PlaylistRows(coverCache, column);
    rows->onActivated = [this](const Playlist& p) { emit playlistActivated(currentSourceId_, p); };
    rows->onFavoriteToggled = [this](const Playlist& p) { emit favoriteToggled(currentSourceId_, p.id); };
    rows->isFavorite = [this](const Playlist& p) { return favoriteCheck_ && favoriteCheck_(currentSourceId_, p.id); };
    playlistRows_ = rows;
    playlistsHint_ = new HintLine(column);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(Theme::Spacing::space2);
    content->addWidget(new SectionTitle(tr("Status"), column));
    content->addWidget(statusRow_);
    content->addWidget(authCard_);
    // Sign-in/out and the source's own options live in Settings; this is
    // the way there from the source itself.
    auto* settingsButton = new QPushButton(tr("Settings…"), column);
    settingsButton->setProperty("variant", "secondary");
    settingsButton->setFont(Theme::font(Theme::TextStyle::Button));
    connect(settingsButton, &QPushButton::clicked, this, [this]() { emit settingsRequested(currentSourceId_); });
    content->addSpacing(Theme::Spacing::space2);
    content->addWidget(settingsButton, 0, Qt::AlignLeft);
    content->addSpacing(Theme::Spacing::space4);
    content->addWidget(new SectionTitle(tr("Features"), column));
    content->addWidget(chipsRow);
    content->addSpacing(Theme::Spacing::space4);
    content->addLayout(playlistsHeader);
    content->addWidget(playlistRows_);
    content->addWidget(playlistsHint_);

    // Left-aligned column of a readable width, same left margin as the
    // sheet's header above it.
    auto* columnWrap = new QWidget(column);
    columnWrap->setMaximumWidth(kColumnMaxWidth);
    columnWrap->setLayout(content);
    auto* columnLayout = new QHBoxLayout;
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->addWidget(columnWrap, 1);
    columnLayout->addStretch(0);
    auto* outerColumn = new QVBoxLayout(column);
    outerColumn->setContentsMargins(
        Theme::Spacing::space4, Theme::Spacing::space2, Theme::Spacing::space3, Theme::Spacing::space4);
    outerColumn->addLayout(columnLayout);
    outerColumn->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    scroll->setFocusPolicy(Qt::NoFocus); // Tab goes to the buttons inside
    chainTabOrder(this);

    connect(&Theme::notifier(), &Theme::Notifier::changed, this, [column]() { column->update(); });
    hide();
}

void SourcePanel::setSource(const QString& sourceId, const QJsonObject& capabilities)
{
    currentSourceId_ = sourceId;

    QLayoutItem* item = nullptr;
    while ((item = chipsLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    const QJsonObject auth = capabilities.value(QStringLiteral("auth")).toObject();
    const QJsonObject browse = capabilities.value(QStringLiteral("browse")).toObject();
    QStringList chips;
    if (browse.value(QStringLiteral("playlists")).toBool())
        chips.append(tr("Playlists"));
    if (browse.value(QStringLiteral("likedTracks")).toBool())
        chips.append(tr("Liked songs"));
    if (browse.value(QStringLiteral("radio")).toBool())
        chips.append(tr("Radio"));
    if (browse.value(QStringLiteral("search")).toBool())
        chips.append(tr("Search"));
    if (capabilities.value(QStringLiteral("download")).toBool())
        chips.append(tr("Downloads"));
    if (auth.value(QStringLiteral("required")).toBool())
        chips.append(tr("Sign-in"));
    for (const QString& chip : chips)
        chipsLayout_->addWidget(new Chip(chip, chipsLayout_->parentWidget()));
    chipsLayout_->addStretch(1);
    chipsLayout_->parentWidget()->setVisible(!chips.isEmpty());
    show();
}

void SourcePanel::setPlaylists(const QList<Playlist>& playlists, bool loading)
{
    playlists_ = playlists;
    playlistsLoading_ = loading;
    refreshPlaylistsSection();
}

void SourcePanel::setFavoriteCheck(std::function<bool(const QString&, const QString&)> check)
{
    favoriteCheck_ = std::move(check);
    playlistRows_->update();
}

void SourcePanel::favoritesChanged() { playlistRows_->update(); }

void SourcePanel::refreshPlaylistsSection()
{
    // Until the source is signed in there's nothing to list — say so
    // instead of an empty/"loading" section under the auth card.
    const bool signInPending = !authCard_->isHidden();
    const bool showRows = !signInPending && !playlists_.isEmpty();
    static_cast<PlaylistRows*>(playlistRows_)->setPlaylists(showRows ? playlists_ : QList<Playlist>());
    playlistRows_->setVisible(showRows);
    QString hint;
    if (signInPending)
        hint = tr("Sign in to see playlists");
    else if (playlists_.isEmpty())
        hint = playlistsLoading_ ? tr("Loading…") : tr("No playlists");
    static_cast<HintLine*>(playlistsHint_)->setText(hint);
    playlistsHint_->setVisible(!hint.isEmpty());
    refreshButton_->setEnabled(!playlistsLoading_);
    refreshButton_->setVisible(!signInPending);
}

void SourcePanel::showAuthCard()
{
    authCard_->show();
    statusRow_->hide();
    refreshPlaylistsSection();
}

void SourcePanel::showPrompt(const QJsonObject& params)
{
    authCard_->showPrompt(params);
    showAuthCard();
}

void SourcePanel::showError(const QString& message)
{
    authCard_->showError(message);
    showAuthCard();
}

void SourcePanel::showSignInNeeded()
{
    authCard_->showSignInNeeded();
    showAuthCard();
}

void SourcePanel::clearAuthSection()
{
    authCard_->setBusy(false);
    authCard_->hide();
    statusRow_->show();
    refreshPlaylistsSection();
}

void SourcePanel::setAuthActionBusy(bool busy) { authCard_->setBusy(busy); }

} // namespace Ui
