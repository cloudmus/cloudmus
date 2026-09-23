#include "PlaylistSheet.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "AnimatedPresenter.h"
#include "CoverArtCache.h"
#include "GeneratedCoverArt.h"
#include "Icons.h"
#include "Metrics.h"
#include "OverlayScrollBar.h"
#include "Radius.h"
#include "SmoothScroller.h"
#include "SourcePanel.h"
#include "Spacing.h"
#include "Tokens.h"
#include "TrackListModel.h"
#include "TrackRowDelegate.h"
#include "Typography.h"

namespace Ui {

namespace {

constexpr int kCoverSide = 56;

// Slides in from the left while fading in; leaves the same way back.
// A short slide, not the full width: a full-width slide combined with a
// fade reads as a translucent sheet dragged across the screen, while a
// short one reads as the page arriving. See AnimatedPresenter.
const TransitionEffect kSheetEnter { 280, QEasingCurve::OutCubic, { 0.0, 1.0, QPointF(-80.0, 0.0) } };
const TransitionEffect kSheetExit { 220, QEasingCurve::InCubic, { 0.0, 1.0, QPointF(-80.0, 0.0) } };

// The radio page's hint text, painted with the current palette.
class RadioHint : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setFont(Theme::font(Theme::TextStyle::BodySecondary));
        painter.setPen(Theme::palette().inkSecondary);
        painter.drawText(rect().adjusted(Theme::Spacing::space4, Theme::Spacing::space4, -Theme::Spacing::space4, 0),
            Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            tr("A continuous radio station — press Play to start listening."));
    }
};

} // namespace

// Cover thumbnail + title + subtitle, painted at paint time from the
// current Theme::palette() like HeroPanel, so a theme flip needs no
// re-styling here.
class PlaylistSheet::HeaderInfo : public QWidget {
public:
    HeaderInfo(CoverArtCache* coverCache, QWidget* parent)
        : QWidget(parent)
        , coverCache_(coverCache)
    {
        setMinimumHeight(kCoverSide);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(coverCache_, &CoverArtCache::pixmapReady, this, [this](const QString& url) {
            if (url == coverUrl_)
                update();
        });
        connect(&Theme::notifier(), &Theme::Notifier::changed, this, qOverload<>(&QWidget::update));
    }

    void set(const QString& title, const QString& subtitle, const QString& coverUrl, const QString& coverSeed,
        const QString& iconPath)
    {
        iconPath_ = iconPath;
        title_ = title;
        subtitle_ = subtitle;
        coverUrl_ = coverUrl;
        generatedCover_ = coverUrl.isEmpty()
            ? generateMeshAuraGradientCover(coverSeed, QSize(kCoverSide, kCoverSide) * devicePixelRatioF())
            : QPixmap();
        update();
    }

    void setSubtitle(const QString& subtitle)
    {
        subtitle_ = subtitle;
        update();
    }

    QSize sizeHint() const override { return { 200, kCoverSide }; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const Theme::Palette& pal = Theme::palette();

        const QRect coverRect(0, (height() - kCoverSide) / 2, kCoverSide, kCoverSide);
        const QPixmap cover
            = coverUrl_.isEmpty() ? generatedCover_ : coverCache_->pixmap(coverUrl_, QSize(kCoverSide, kCoverSide));
        QPainterPath clip;
        clip.addRoundedRect(coverRect, Theme::Radius::md, Theme::Radius::md);
        painter.save();
        painter.setClipPath(clip);
        if (cover.isNull())
            painter.fillRect(coverRect, pal.surface300);
        else
            painter.drawPixmap(coverRect, cover);
        painter.restore();
        // A source's own icon over its generated cover (always dark-ish
        // and colorful, so plain white reads on it in either theme).
        if (!iconPath_.isEmpty()) {
            constexpr int kIconSide = 28;
            Theme::iconFromFile(iconPath_, QColor(255, 255, 255, 235), kIconSide)
                .paint(&painter,
                    QRect(coverRect.center().x() - kIconSide / 2 + 1, coverRect.center().y() - kIconSide / 2 + 1,
                        kIconSide, kIconSide));
        }

        const int textLeft = coverRect.right() + 1 + Theme::Spacing::space3;
        const QRect textRect(textLeft, 0, width() - textLeft, height());
        const QFont titleFont = Theme::font(Theme::TextStyle::Display);
        const QFont subtitleFont = Theme::font(Theme::TextStyle::BodySecondary);
        const QFontMetrics titleMetrics(titleFont);
        const QFontMetrics subtitleMetrics(subtitleFont);
        const int blockHeight
            = titleMetrics.height() + (subtitle_.isEmpty() ? 0 : Theme::Spacing::space1 + subtitleMetrics.height());
        int y = (height() - blockHeight) / 2;

        painter.setFont(titleFont);
        painter.setPen(pal.ink);
        painter.drawText(QRect(textLeft, y, textRect.width(), titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
            titleMetrics.elidedText(title_, Qt::ElideRight, textRect.width()));
        if (!subtitle_.isEmpty()) {
            y += titleMetrics.height() + Theme::Spacing::space1;
            painter.setFont(subtitleFont);
            painter.setPen(pal.inkSecondary);
            painter.drawText(QRect(textLeft, y, textRect.width(), subtitleMetrics.height()),
                Qt::AlignLeft | Qt::AlignVCenter,
                subtitleMetrics.elidedText(subtitle_, Qt::ElideRight, textRect.width()));
        }
    }

private:
    CoverArtCache* coverCache_;
    QString title_;
    QString subtitle_;
    QString coverUrl_;
    QPixmap generatedCover_;
    QString iconPath_;
};

// Case-insensitive substring match on title, artist names and album.
class PlaylistSheet::FilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setNeedle(const QString& needle)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        needle_ = needle.trimmed();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        needle_ = needle.trimmed();
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (needle_.isEmpty())
            return true;
        const Track track
            = sourceModel()->index(sourceRow, 0, sourceParent).data(TrackListModel::TrackRole).value<Track>();
        if (track.title.contains(needle_, Qt::CaseInsensitive))
            return true;
        for (const Artist& artist : track.artists) {
            if (artist.name.contains(needle_, Qt::CaseInsensitive))
                return true;
        }
        return track.album.has_value() && track.album->title.contains(needle_, Qt::CaseInsensitive);
    }

private:
    QString needle_;
};

PlaylistSheet::PlaylistSheet(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);

    headerInfo_ = new HeaderInfo(coverCache, this);

    playAllButton_ = new QPushButton(
        Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::OnAccent, Theme::Metrics::iconGlyphSize), QString(),
        this);
    playAllButton_->setProperty("variant", "play");
    playAllButton_->setToolTip(tr("Play all"));
    playAllButton_->setFixedSize(Theme::Metrics::iconButtonSize, Theme::Metrics::iconButtonSize);
    playAllButton_->setIconSize(QSize(Theme::Metrics::iconGlyphSize, Theme::Metrics::iconGlyphSize));
    connect(playAllButton_, &QPushButton::clicked, this, &PlaylistSheet::playAllClicked);

    // Back, at the sheet's left edge — right next to the sidebar the sheet
    // was opened from, so closing it doesn't need a trip across the window.
    // Filled so it reads as a round button even at rest.
    backButton_ = new QToolButton(this);
    backButton_->setProperty("variant", "icon");
    backButton_->setProperty("filled", true);
    backButton_->setToolTip(tr("Back"));
    backButton_->setIcon(
        Theme::icon(QStringLiteral("arrow_back"), Theme::IconColor::Ink, Theme::Metrics::iconGlyphSize));
    backButton_->setFixedSize(Theme::Metrics::iconButtonSize, Theme::Metrics::iconButtonSize);
    backButton_->setIconSize(QSize(Theme::Metrics::iconGlyphSize, Theme::Metrics::iconGlyphSize));
    connect(backButton_, &QToolButton::clicked, this, &PlaylistSheet::closeRequested);

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(Theme::Spacing::space3);
    headerRow->addWidget(backButton_);
    headerRow->addWidget(headerInfo_, 1);
    headerRow->addWidget(playAllButton_);

    filterEdit_ = new QLineEdit(this);
    filterEdit_->setProperty("variant", "filter"); // see StyleSheet.cpp's inputsBlock()
    filterEdit_->setPlaceholderText(tr("Filter by title, artist or album"));
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->addAction(
        Theme::icon(QStringLiteral("search"), Theme::IconColor::InkTertiary, 16), QLineEdit::LeadingPosition);
    filterEdit_->setFont(Theme::font(Theme::TextStyle::Body));

    trackModel_ = new TrackListModel(this);
    filterProxy_ = new FilterProxy(this);
    filterProxy_->setSourceModel(trackModel_);
    connect(filterEdit_, &QLineEdit::textChanged, filterProxy_, &FilterProxy::setNeedle);

    trackDelegate_ = new TrackRowDelegate(coverCache, this);
    // Rows line up with the header: the thumbnail under the back button
    // (header margin minus the row's own thumbnail inset), the duration
    // under the Play button — which also keeps it clear of the overlay
    // scrollbar floating at the view's right edge.
    trackDelegate_->setRowInsets(Theme::Spacing::space2, Theme::Spacing::space1);
    connect(coverCache, &CoverArtCache::pixmapReady, this, [this]() { trackView_->viewport()->update(); });
    trackView_ = new QListView(this);
    trackView_->setObjectName(QStringLiteral("trackListView")); // same look as the main list — StyleSheet.cpp
    trackView_->setModel(filterProxy_);
    trackView_->setItemDelegate(trackDelegate_);
    trackView_->setMouseTracking(true);
    trackView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    trackView_->setContextMenuPolicy(Qt::CustomContextMenu);
    SmoothScroller::attach(trackView_);
    OverlayScrollBar::attach(trackView_);
    const auto activate = [this](const QModelIndex& index) {
        if (index.isValid())
            emit trackActivated(sourceRow(index));
    };
    connect(trackView_, &QListView::doubleClicked, this, activate);
    connect(trackDelegate_, &TrackRowDelegate::playRequested, this, activate);
    connect(trackView_, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = trackView_->indexAt(pos);
        if (index.isValid())
            emit trackContextMenuRequested(sourceRow(index), trackView_->viewport()->mapToGlobal(pos));
    });

    // Floating over the list's top edge rather than in a layout, so
    // showing it doesn't shove the list down — same as MainWindow's.
    busyIndicator_ = new QProgressBar(trackView_);
    busyIndicator_->setProperty("themed", true);
    busyIndicator_->setRange(0, 0);
    busyIndicator_->setTextVisible(false);
    busyIndicator_->hide();
    trackView_->installEventFilter(this);

    radioPage_ = new RadioHint(this);

    sourcePanel_ = new SourcePanel(coverCache, this);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(trackView_);
    pages_->addWidget(radioPage_);
    pages_->addWidget(sourcePanel_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* headerBlock = new QVBoxLayout;
    headerBlock->setContentsMargins(
        Theme::Spacing::space4, Theme::Spacing::space3, Theme::Spacing::space3, Theme::Spacing::space3);
    headerBlock->setSpacing(Theme::Spacing::space3);
    headerBlock->addLayout(headerRow);
    headerBlock->addWidget(filterEdit_);
    layout->addLayout(headerBlock);
    layout->addWidget(pages_, 1);

    // Window-wide, not just while focus is inside the sheet: clicking the
    // sidebar to open it leaves focus there. Enabled only while presented.
    escapeShortcut_ = new QShortcut(QKeySequence(Qt::Key_Escape), parent);
    escapeShortcut_->setContext(Qt::WindowShortcut);
    escapeShortcut_->setEnabled(false);
    connect(escapeShortcut_, &QShortcut::activated, this, &PlaylistSheet::closeRequested);

    presenter_ = new AnimatedPresenter(this, kSheetEnter, kSheetExit);
    connect(presenter_, &AnimatedPresenter::presented, this, [this]() {
        if (filterEdit_->isVisible())
            filterEdit_->setFocus();
        else
            setFocus();
    });
    connect(presenter_, &AnimatedPresenter::dismissed, this, &PlaylistSheet::dismissed);
}

void PlaylistSheet::setHeader(const QString& title, const QString& subtitle, const QString& coverUrl,
    const QString& coverSeed, const QString& iconPath)
{
    headerInfo_->set(title, subtitle, coverUrl, coverSeed, iconPath);
    headerInfo_->show();
}

void PlaylistSheet::showTracks(
    const QString& title, const QString& subtitle, const QString& coverUrl, const QString& coverSeed, bool canPlayAll)
{
    setHeader(title, subtitle, coverUrl, coverSeed);
    playAllButton_->setVisible(canPlayAll);
    filterEdit_->clear();
    filterEdit_->show();
    pages_->setCurrentWidget(trackView_);
    trackView_->scrollToTop();
}

void PlaylistSheet::showRadio(
    const QString& title, const QString& description, const QString& coverUrl, const QString& coverSeed)
{
    setHeader(title, description, coverUrl, coverSeed);
    playAllButton_->show();
    filterEdit_->hide();
    pages_->setCurrentWidget(radioPage_);
    setBusy(false);
}

void PlaylistSheet::showSource(const QString& name, const QString& description, const QString& iconPath)
{
    setHeader(name, description, QString(), name, iconPath);
    playAllButton_->hide();
    filterEdit_->hide();
    pages_->setCurrentWidget(sourcePanel_);
    setBusy(false);
}

void PlaylistSheet::setSubtitle(const QString& subtitle) { headerInfo_->setSubtitle(subtitle); }

void PlaylistSheet::setBusy(bool busy) { busyIndicator_->setVisible(busy); }

void PlaylistSheet::updateRows() { trackView_->viewport()->update(); }

void PlaylistSheet::present()
{
    escapeShortcut_->setEnabled(true);
    presenter_->present();
}

void PlaylistSheet::dismiss()
{
    escapeShortcut_->setEnabled(false);
    presenter_->dismiss();
}

bool PlaylistSheet::isPresented() const { return presenter_->isPresented(); }

int PlaylistSheet::sourceRow(const QModelIndex& viewIndex) const { return filterProxy_->mapToSource(viewIndex).row(); }

void PlaylistSheet::paintEvent(QPaintEvent*)
{
    // Opaque: the sheet fully covers the active playlist underneath.
    QPainter(this).fillRect(rect(), Theme::palette().surface0);
}

bool PlaylistSheet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == trackView_ && event->type() == QEvent::Resize)
        busyIndicator_->setGeometry(0, 0, trackView_->width(), 4);
    return QWidget::eventFilter(watched, event);
}

} // namespace Ui
