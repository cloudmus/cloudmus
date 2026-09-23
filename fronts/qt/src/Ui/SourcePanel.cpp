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

#include "CoverArtCache.h"
#include "GeneratedCoverArt.h"
#include "Icons.h"
#include "Models.h"
#include "OverlayScrollBar.h"
#include "Radius.h"
#include "Spacing.h"
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
constexpr int kMultilineFieldMinHeight = 140;

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
// "N tracks" / "Radio" caption; hover highlight like the track lists.
class PlaylistRows : public QWidget {
public:
    PlaylistRows(CoverArtCache* coverCache, QWidget* parent)
        : QWidget(parent)
        , coverCache_(coverCache)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(coverCache_, &CoverArtCache::pixmapReady, this, qOverload<>(&QWidget::update));
    }

    void setPlaylists(const QList<Playlist>& playlists)
    {
        playlists_ = playlists;
        hovered_ = -1;
        setFixedHeight(int(playlists_.size()) * kPlaylistRowHeight);
        update();
    }

    std::function<void(const Playlist&)> onActivated;

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

            const int textLeft = thumb.right() + 1 + Theme::Spacing::space3;
            const int textWidth = row.right() - textLeft - pad;
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

    void mouseMoveEvent(QMouseEvent* event) override { setHovered(rowAt(event->position().toPoint())); }
    void leaveEvent(QEvent*) override { setHovered(-1); }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const int row = rowAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && row >= 0 && onActivated)
            onActivated(playlists_[row]);
    }

private:
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

    CoverArtCache* coverCache_;
    QList<Playlist> playlists_;
    QHash<QString, QPixmap> generated_;
    int hovered_ = -1;
};
} // namespace

SourcePanel::SourcePanel(CoverArtCache* coverCache, QWidget* parent)
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
    OverlayScrollBar::attach(scroll);

    authCard_ = new QWidget(column);
    authCard_->setObjectName(QStringLiteral("sourceAuthCard"));
    // Styled by Theme::StyleSheet's global #sourceAuthCard rule now (surface-200/
    // border/radius-md, regenerated on every theme change) — not a local
    // setStyleSheet() call, which used the palette(...) QSS functions to
    // follow the native theme; this app now owns a fixed palette instead.
    authCard_->setMaximumWidth(kCardMaxWidth);

    auto* iconLabel = new QLabel(authCard_);
    iconLabel->setPixmap(
        Theme::icon(QStringLiteral("warning"), Theme::IconColor::Accent, kIconSize).pixmap(kIconSize, kIconSize));

    messageLabel_ = new QLabel(authCard_);
    messageLabel_->setWordWrap(true);
    // Selectable + link-clickable for the deviceCode case; harmless for
    // plain error text (still just lets the user select/copy it).
    messageLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
    messageLabel_->setOpenExternalLinks(true);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(iconLabel);
    headerRow->addWidget(messageLabel_, 1);

    codeLabel_ = new QLabel(authCard_);
    QFont codeFont(QStringLiteral("monospace"));
    codeFont.setBold(true);
    codeFont.setPointSize(codeFont.pointSize() + 3);
    codeLabel_->setFont(codeFont);
    codeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel_->hide();

    copyCodeButton_ = new QPushButton(tr("Copy code"), authCard_);
    copyCodeButton_->setProperty("variant", "secondary");
    copyCodeButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(copyCodeButton_, &QPushButton::clicked, this,
        [this]() { QGuiApplication::clipboard()->setText(codeLabel_->text()); });
    copyCodeButton_->hide();

    formLayout_ = new QHBoxLayout;
    multilineFieldsLayout_ = new QVBoxLayout;

    submitButton_ = new QPushButton(tr("Submit"), authCard_);
    submitButton_->setProperty("variant", "primary");
    submitButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(submitButton_, &QPushButton::clicked, this, [this]() {
        QJsonObject fields;
        for (auto it = fieldEdits_.constBegin(); it != fieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->text());
        }
        for (auto it = multilineFieldEdits_.constBegin(); it != multilineFieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->toPlainText());
        }
        emit submitRequested(currentSourceId_, fields);
    });
    submitButton_->hide();

    openBrowserButton_ = new QPushButton(tr("Open Browser"), authCard_);
    openBrowserButton_->setProperty("variant", "secondary");
    openBrowserButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(openBrowserButton_, &QPushButton::clicked, this,
        [this]() { QDesktopServices::openUrl(QUrl(pendingOAuthUrl_)); });
    openBrowserButton_->hide();

    retryButton_ = new QPushButton(tr("Retry"), authCard_);
    retryButton_->setProperty("variant", "secondary");
    retryButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(retryButton_, &QPushButton::clicked, this, [this]() { emit retryRequested(currentSourceId_); });
    retryButton_->hide();

    busyIndicator_ = new QProgressBar(authCard_);
    busyIndicator_->setProperty("themed", true); // see StyleSheet.cpp's progressBarBlock()
    busyIndicator_->setRange(0, 0); // indeterminate
    busyIndicator_->setFixedWidth(80);
    busyIndicator_->setMaximumHeight(6);
    busyIndicator_->hide();

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(copyCodeButton_);
    buttonRow->addLayout(formLayout_);
    buttonRow->addWidget(submitButton_);
    buttonRow->addWidget(openBrowserButton_);
    buttonRow->addWidget(retryButton_);
    buttonRow->addWidget(busyIndicator_);
    buttonRow->addStretch(1);

    auto* cardLayout = new QVBoxLayout(authCard_);
    cardLayout->setContentsMargins(
        Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space6);
    cardLayout->setSpacing(10);
    cardLayout->addLayout(headerRow);
    cardLayout->addWidget(codeLabel_);
    cardLayout->addLayout(multilineFieldsLayout_);
    cardLayout->addLayout(buttonRow);
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
    refreshButton_->setIcon(Theme::icon(QStringLiteral("refresh"), Theme::IconColor::InkSecondary, 14));
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
    playlistRows_ = rows;
    playlistsHint_ = new HintLine(column);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(Theme::Spacing::space2);
    content->addWidget(new SectionTitle(tr("Status"), column));
    content->addWidget(statusRow_);
    content->addWidget(authCard_);
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

    connect(&Theme::notifier(), &Theme::Notifier::changed, this, [column]() { column->update(); });
    hide();
}

void SourcePanel::clearFormFields()
{
    qDeleteAll(fieldEdits_);
    fieldEdits_.clear();
    qDeleteAll(multilineFieldEdits_);
    multilineFieldEdits_.clear();
    QLayoutItem* item = nullptr;
    while ((item = formLayout_->takeAt(0)) != nullptr) {
        delete item;
    }
    while ((item = multilineFieldsLayout_->takeAt(0)) != nullptr) {
        delete item;
    }
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

void SourcePanel::resetAuthChrome()
{
    clearFormFields();
    codeLabel_->hide();
    codeLabel_->clear();
    copyCodeButton_->hide();
    submitButton_->hide();
    openBrowserButton_->hide();
    retryButton_->hide();
    setAuthActionBusy(false);
}

void SourcePanel::showPrompt(const QJsonObject& params)
{
    resetAuthChrome();

    const QString flow = params.value(QStringLiteral("flow")).toString();
    if (flow == QStringLiteral("deviceCode")) {
        const QString url = params.value(QStringLiteral("url")).toString();
        const QString code = params.value(QStringLiteral("code")).toString();
        messageLabel_->setTextFormat(Qt::RichText);
        messageLabel_->setText(
            tr("Open <a href=\"%1\">%2</a> and enter the code below").arg(url.toHtmlEscaped(), url.toHtmlEscaped()));
        codeLabel_->setText(code);
        codeLabel_->show();
        copyCodeButton_->show();
    } else if (flow == QStringLiteral("usernamePassword")) {
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        bool hasMultilineField = false;
        for (const QJsonValue& v : fields) {
            if (v.toObject().value(QStringLiteral("multiline")).toBool()) {
                hasMultilineField = true;
                break;
            }
        }
        messageLabel_->setTextFormat(Qt::PlainText);
        // A generic "no source-specific UI" form (docs/protocol.md §10.2)
        // can't know it's specifically a browser-headers paste, but a
        // multiline field is a strong enough signal to justify a more
        // useful instruction than a bare "Sign in" — the DevTools steps
        // aren't otherwise discoverable from the form alone.
        messageLabel_->setText(hasMultilineField
                ? tr("Open music.youtube.com in your browser while signed in, open DevTools → Network tab, "
                     "click any request to music.youtube.com, and paste its Request Headers below.")
                : tr("Sign in"));
        for (const QJsonValue& v : fields) {
            const QJsonObject field = v.toObject();
            const QString name = field.value(QStringLiteral("name")).toString();
            if (field.value(QStringLiteral("multiline")).toBool()) {
                // A pasted-blob field (e.g. raw browser request headers) —
                // a QLineEdit would technically still round-trip the text
                // correctly (Qt doesn't strip embedded newlines from a
                // paste, only their on-screen rendering), but squished onto
                // one visible line the user has no way to actually read or
                // verify what they pasted before submitting something this
                // sensitive.
                auto* edit = new QPlainTextEdit(authCard_);
                edit->setPlaceholderText(tr("Paste here"));
                edit->setMinimumHeight(kMultilineFieldMinHeight);
                multilineFieldsLayout_->addWidget(edit);
                multilineFieldEdits_.insert(name, edit);
            } else {
                auto* edit = new QLineEdit(authCard_);
                if (field.value(QStringLiteral("secret")).toBool()) {
                    edit->setEchoMode(QLineEdit::Password);
                }
                edit->setPlaceholderText(name);
                formLayout_->addWidget(edit);
                fieldEdits_.insert(name, edit);
            }
        }
        submitButton_->show();
    } else if (flow == QStringLiteral("oauthRedirect")) {
        pendingOAuthUrl_ = params.value(QStringLiteral("url")).toString();
        QDesktopServices::openUrl(QUrl(pendingOAuthUrl_));
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(tr("Continue in your browser"));
        openBrowserButton_->show();
    } else {
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(tr("Sign-in required"));
    }
    authCard_->show();
    statusRow_->hide();
    refreshPlaylistsSection();
}

void SourcePanel::showError(const QString& message)
{
    resetAuthChrome();
    // PlainText, not the default AutoText: an arbitrary backend-supplied
    // error message shouldn't be interpreted as markup.
    messageLabel_->setTextFormat(Qt::PlainText);
    messageLabel_->setText(message);
    retryButton_->show();
    authCard_->show();
    statusRow_->hide();
    refreshPlaylistsSection();
}

void SourcePanel::clearAuthSection()
{
    resetAuthChrome();
    authCard_->hide();
    statusRow_->show();
    refreshPlaylistsSection();
}

void SourcePanel::setAuthActionBusy(bool busy)
{
    submitButton_->setEnabled(!busy);
    retryButton_->setEnabled(!busy);
    busyIndicator_->setVisible(busy);
}

} // namespace Ui
