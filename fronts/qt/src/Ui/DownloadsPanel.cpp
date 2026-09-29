#include "DownloadsPanel.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "Downloads.h"
#include "Icons.h"
#include "NowPlaying.h"
#include "OverlayScrollBar.h"
#include "PopupWindow.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

namespace {

// Room around the panel for its shadow, as for menus (Theme::CloudMusStyle).
constexpr int kShadowReach = 12;
// The margin reserved for the shadow — none unless it's painted in the
// panel's own window (Theme::PopupLook).
int shadowMargin() { return Theme::popupShadowMargin(kShadowReach); }
// Stronger over glass, as for menus.
Theme::PopupShadow shadow() { return { kShadowReach, 2, Theme::glassEnabled() ? 56 : 32 }; }
constexpr int kWidth = 360;
constexpr int kMaxRowsHeight = 360;
constexpr int kPad = Theme::Spacing::space3;
constexpr int kBarHeight = 3;
constexpr int kCancelSide = 24;
#ifdef Q_OS_WIN
constexpr int kFadeMs = 150;
#endif

// "3.2 of 8.1 MB" — or in KB, for a file under a megabyte.
QString sizeText(qint64 received, qint64 total)
{
    constexpr double kMb = 1024.0 * 1024.0;
    if (total < 1024 * 1024)
        return QObject::tr("%1 of %2 KB").arg((received + 1023) / 1024).arg((total + 1023) / 1024);
    return QObject::tr("%1 of %2 MB")
        .arg(QString::number(double(received) / kMb, 'f', 1), QString::number(double(total) / kMb, 'f', 1));
}

// What a download's second line says.
QString statusText(const ViewModel::Downloads::Job& job)
{
    using State = ViewModel::Downloads::State;
    switch (job.state) {
        case State::Queued:
            return QObject::tr("Waiting");
        case State::Done:
            return job.isPlaylist && job.failed > 0
                ? QObject::tr("Saved %1 of %2 tracks").arg(job.saved).arg(job.tracks.size())
                : QObject::tr("Saved");
        case State::Failed:
            return QObject::tr("Failed");
        case State::Cancelled:
            return QObject::tr("Cancelled");
        case State::Running:
            break;
    }
    if (job.isPlaylist) {
        if (job.tracks.isEmpty())
            return QObject::tr("Listing tracks…");
        return QObject::tr("%1 of %2 tracks").arg(job.saved + job.failed).arg(job.tracks.size());
    }
    if (job.total > 0)
        return sizeText(job.received, job.total);
    return QObject::tr("Saving…");
}

// The downloads, painted — a row each, plus a line for the track a running
// playlist is on. Cancel buttons are hit-tested here too.
class Rows : public QWidget {
public:
    Rows(ViewModel::Downloads& downloads, QWidget* parent)
        : QWidget(parent)
        , downloads_(downloads)
    {
        setMouseTracking(true);
        // A sweep for bars with no progress to show yet.
        sweep_ = new QVariantAnimation(this);
        sweep_->setStartValue(0.0);
        sweep_->setEndValue(1.0);
        sweep_->setDuration(1200);
        sweep_->setLoopCount(-1);
        connect(sweep_, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
        sweep_->start();
    }

    // Layout for the jobs as they are now; the height it needs.
    int relayout()
    {
        const QFontMetrics title(Theme::font(Theme::TextStyle::Body));
        const QFontMetrics caption(Theme::font(Theme::TextStyle::Caption));
        const int rowHeight = kPad + title.height() + Theme::Spacing::space1 + caption.height() + Theme::Spacing::space1
            + kBarHeight + kPad;
        const int subHeight = caption.height() + Theme::Spacing::space1 + kBarHeight + kPad;
        int y = 0;
        slots_.clear();
        for (const ViewModel::Downloads::Job& job : downloads_.jobs()) {
            Slot slot { job.id, QRect(0, y, width(), rowHeight), QRect() };
            y += rowHeight;
            if (job.isPlaylist && job.state == ViewModel::Downloads::State::Running && !job.currentTitle().isEmpty()) {
                slot.sub = QRect(0, y - kPad, width(), subHeight);
                y += subHeight - kPad;
            }
            slots_.append(slot);
        }
        return y;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Theme::Palette& pal = Theme::palette();
        const QFont titleFont = Theme::font(Theme::TextStyle::Body);
        const QFont captionFont = Theme::font(Theme::TextStyle::Caption);
        const QFontMetrics title(titleFont);
        const QFontMetrics caption(captionFont);
        const auto jobs = downloads_.jobs();
        for (int i = 0; i < slots_.size() && i < jobs.size(); ++i) {
            const ViewModel::Downloads::Job& job = jobs[i];
            const QRect row = slots_[i].row.adjusted(kPad, kPad, -kPad, -kPad);
            // Queued ones can be cancelled too; only a running one has a bar.
            const bool running
                = job.state == ViewModel::Downloads::State::Running || job.state == ViewModel::Downloads::State::Queued;
            const int textRight = running ? row.right() - kCancelSide - Theme::Spacing::space2 : row.right();

            painter.setFont(titleFont);
            painter.setPen(pal.ink);
            painter.drawText(QRect(row.left(), row.top(), textRight - row.left(), title.height()),
                Qt::AlignLeft | Qt::AlignVCenter, title.elidedText(job.title, Qt::ElideRight, textRight - row.left()));
            int y = row.top() + title.height() + Theme::Spacing::space1;
            painter.setFont(captionFont);
            painter.setPen(job.state == ViewModel::Downloads::State::Failed ? pal.danger : pal.inkSecondary);
            painter.drawText(QRect(row.left(), y, textRight - row.left(), caption.height()),
                Qt::AlignLeft | Qt::AlignVCenter, statusText(job));
            y += caption.height() + Theme::Spacing::space1;
            if (job.state == ViewModel::Downloads::State::Running)
                drawBar(painter, QRect(row.left(), y, textRight - row.left(), kBarHeight), job.progress());

            if (running) {
                const QRect cancel(
                    row.right() - kCancelSide, row.top() + (row.height() - kCancelSide) / 2, kCancelSide, kCancelSide);
                if (cancel.contains(hover_)) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(pal.surface300);
                    painter.drawEllipse(cancel);
                }
                Theme::icon(QStringLiteral("close"),
                    cancel.contains(hover_) ? Theme::IconColor::Ink : Theme::IconColor::InkSecondary, 16)
                    .paint(&painter, cancel.adjusted(4, 4, -4, -4));
            }

            // A running playlist's second level: the track it's on.
            if (slots_[i].sub.isValid()) {
                const QRect sub = slots_[i].sub.adjusted(kPad + Theme::Spacing::space4, 0, -kPad, -kPad);
                const int subRight = textRight;
                painter.setFont(captionFont);
                painter.setPen(pal.inkSecondary);
                const QString text = job.total > 0
                    ? tr("%1 — %2").arg(job.currentTitle(), sizeText(job.received, job.total))
                    : job.currentTitle();
                painter.drawText(QRect(sub.left(), sub.top(), subRight - sub.left(), caption.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, caption.elidedText(text, Qt::ElideMiddle, subRight - sub.left()));
                drawBar(painter,
                    QRect(sub.left(), sub.top() + caption.height() + Theme::Spacing::space1, subRight - sub.left(),
                        kBarHeight),
                    job.total > 0 ? double(job.received) / double(job.total) : -1);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        hover_ = event->position().toPoint();
        update();
    }
    void leaveEvent(QEvent*) override
    {
        hover_ = QPoint(-1, -1);
        update();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        const QPoint pos = event->position().toPoint();
        const auto jobs = downloads_.jobs();
        for (int i = 0; i < slots_.size() && i < jobs.size(); ++i) {
            const QRect row = slots_[i].row.adjusted(kPad, kPad, -kPad, -kPad);
            const QRect cancel(
                row.right() - kCancelSide, row.top() + (row.height() - kCancelSide) / 2, kCancelSide, kCancelSide);
            if (cancel.contains(pos)) {
                downloads_.cancel(slots_[i].jobId);
                return;
            }
        }
    }

private:
    void drawBar(QPainter& painter, const QRect& bar, double progress)
    {
        const Theme::Palette& pal = Theme::palette();
        painter.setPen(Qt::NoPen);
        painter.setBrush(pal.surface400);
        painter.drawRoundedRect(bar, kBarHeight / 2.0, kBarHeight / 2.0);
        painter.setBrush(pal.accent);
        if (progress >= 0) {
            QRect fill = bar;
            fill.setWidth(int(bar.width() * qBound(0.0, progress, 1.0)));
            if (fill.width() > 0)
                painter.drawRoundedRect(fill, kBarHeight / 2.0, kBarHeight / 2.0);
        } else {
            // No progress to show: a segment sweeping across.
            const int segment = bar.width() / 4;
            const int x = bar.left() + int((bar.width() + segment) * sweep_->currentValue().toReal()) - segment;
            const QRect fill = QRect(x, bar.top(), segment, bar.height()).intersected(bar);
            painter.drawRoundedRect(fill, kBarHeight / 2.0, kBarHeight / 2.0);
        }
    }

    struct Slot {
        int jobId;
        QRect row;
        QRect sub;
    };

    ViewModel::Downloads& downloads_;
    QVariantAnimation* sweep_;
    QList<Slot> slots_;
    QPoint hover_ { -1, -1 };
};

} // namespace

DownloadsPanel::DownloadsPanel(ViewModel::Downloads& downloads, ViewModel::NowPlaying& nowPlaying, QWidget* parent)
    // No native drop shadow (popupWindowFlags()): Windows would put a
    // rectangular one around the whole window, shadow margin included;
    // paintEvent() draws ours — except with Windows 11's own popup look.
    : QWidget(parent, Theme::popupWindowFlags(Qt::Popup | Qt::FramelessWindowHint))
    , downloads_(downloads)
    , nowPlaying_(nowPlaying)
{
    Theme::preparePopup(this, /*clickThrough=*/false);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* heading = new QLabel(tr("Downloads").toUpper(), this);
    heading->setFont(Theme::font(Theme::TextStyle::LabelUpper));
    heading->setObjectName(QStringLiteral("sectionLabel")); // color: see StyleSheet.cpp's panelsBlock()
    saveCurrentButton_ = new QPushButton(tr("Save Playing Track"), this);
    saveCurrentButton_->setProperty("variant", "secondary");
    saveCurrentButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(saveCurrentButton_, &QPushButton::clicked, this, [this]() { nowPlaying_.download(); });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(kPad, kPad, kPad, 0);
    header->addWidget(heading);
    header->addStretch(1);
    header->addWidget(saveCurrentButton_);

    rows_ = new Rows(downloads_, this);
    scroll_ = new QScrollArea(this);
    scroll_->setWidget(rows_);
    scroll_->setWidgetResizable(false);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet(QStringLiteral("background: transparent;"));
    scroll_->viewport()->setAutoFillBackground(false);
    OverlayScrollBar::attach(scroll_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(shadowMargin(), shadowMargin(), shadowMargin(), shadowMargin());
    layout->setSpacing(Theme::Spacing::space2);
    layout->addLayout(header);
    layout->addWidget(scroll_);

    connect(&downloads_, &ViewModel::Downloads::changed, this, &DownloadsPanel::refresh);
    connect(&nowPlaying_, &ViewModel::NowPlaying::feedbackChanged, this, &DownloadsPanel::refresh);
    refresh();
}

void DownloadsPanel::refresh()
{
    const ViewModel::NowPlaying::Feedback feedback = nowPlaying_.feedback();
    saveCurrentButton_->setEnabled(feedback.downloadSupported && !feedback.downloadBusy);
    const int contentWidth = kWidth - 2 * shadowMargin();
    rows_->setFixedWidth(contentWidth);
    const int height = static_cast<Rows*>(rows_)->relayout();
    rows_->setFixedHeight(height);
    scroll_->setFixedSize(contentWidth, qMin(height, kMaxRowsHeight));
    rows_->update();
    adjustSize();
    setFixedWidth(kWidth);
    if (isVisible())
        place();
}

void DownloadsPanel::popup(const QPoint& anchor)
{
    anchor_ = anchor;
    adjustSize();
    place();
#ifdef Q_OS_WIN
    // windowOpacity, not painting with opacity like ThemedToolTip: that
    // wouldn't reach the child widgets. Windows only — Wayland has no
    // window opacity, and Linux compositors animate popups themselves.
    // Not Windows 11's own popup look (Theme::popupsFade()): window
    // opacity would make it a layered window, which DWM doesn't back.
    if (!Theme::popupsFade()) {
        show();
        return;
    }
    fade_ = new QPropertyAnimation(this, "windowOpacity", this);
    fade_->setDuration(kFadeMs);
    fade_->setEasingCurve(QEasingCurve::OutCubic);
    // A shadow window of its own (Theme::PopupLook::Clipped) fades along.
    connect(fade_, &QPropertyAnimation::valueChanged, this,
        [this](const QVariant& value) { Theme::setPopupOpacity(this, value.toReal()); });
    connect(fade_, &QPropertyAnimation::finished, this, [this]() {
        if (fade_->endValue().toReal() <= 0.0) {
            fadedOut_ = true;
            close();
        }
    });
    setWindowOpacity(0.0);
    fade_->setStartValue(0.0);
    fade_->setEndValue(1.0);
    fade_->start();
#endif
    show();
}

void DownloadsPanel::place()
{
    // Its visible panel's bottom-left just above the button's top-left.
    QPoint pos(anchor_.x() - shadowMargin(), anchor_.y() - height() + shadowMargin() - Theme::Spacing::space1);
    if (const QScreen* screen = QGuiApplication::screenAt(anchor_)) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left() - shadowMargin(), pos.x(), avail.right() - width() + shadowMargin()));
        pos.setY(qMax(avail.top() - shadowMargin(), pos.y()));
    }
    move(pos);
}

void DownloadsPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect panel = rect().adjusted(shadowMargin(), shadowMargin(), -shadowMargin(), -shadowMargin());
    Theme::paintSoftShadow(&painter, panel, shadowMargin(), shadow().offsetY, shadow().maxAlpha, Theme::Radius::md);
    const Theme::Palette& pal = Theme::palette();
    QPainterPath path;
    path.addRoundedRect(QRectF(panel).adjusted(0.5, 0.5, -0.5, -0.5), Theme::Radius::md, Theme::Radius::md);
    painter.setPen(QPen(pal.border, 1));
    painter.setBrush(Theme::glass(pal.surface200));
    painter.drawPath(path);
}

void DownloadsPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    const QRect panel = rect().adjusted(shadowMargin(), shadowMargin(), -shadowMargin(), -shadowMargin());
    Theme::setUpPopup(this, panel, Theme::Radius::md, shadow());
}

void DownloadsPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // The glass's shape follows the panel's (on show it's set up anyway).
    if (isVisible()) {
        const QRect panel = rect().adjusted(shadowMargin(), shadowMargin(), -shadowMargin(), -shadowMargin());
        Theme::setUpPopup(this, panel, Theme::Radius::md, shadow());
    }
}

void DownloadsPanel::closeEvent(QCloseEvent* event)
{
#ifdef Q_OS_WIN
    // Fade out first, then close for real (fade_'s finished()). A click
    // outside while fading asks again — nothing to do, already going.
    if (fade_ && !fadedOut_) {
        event->ignore();
        if (fade_->endValue().toReal() > 0.0) {
            fade_->stop();
            fade_->setStartValue(windowOpacity());
            fade_->setEndValue(0.0);
            fade_->start();
        }
        return;
    }
#endif
    // What's over has been seen by now.
    if (!downloads_.isActive())
        downloads_.clearFinished();
    QWidget::closeEvent(event);
}

} // namespace Ui
