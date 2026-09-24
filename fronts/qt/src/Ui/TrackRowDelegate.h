#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStyledItemDelegate>

namespace Ui {

class CoverArtCache;

// Compact, one-line row: small inline cover thumbnail + title/artist
// stacked + duration right-aligned — see the plan's UI/UX design (chosen
// over a multi-column table). Colors/fonts come from Theme::Tokens/
// Theme::Typography (the CloudMus design system's own fixed palette), not
// QPalette — the app no longer rides the native/KDE theme for these tokens.
class TrackRowDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit TrackRowDelegate(CoverArtCache* coverCache, QObject* parent = nullptr);

    // Marks (sourceId, trackId) as the row to render as "currently
    // playing" — an accent-colored, bold title. Pass empty strings to
    // clear the indicator (e.g. nothing loaded). Does not itself trigger a
    // repaint — callers update the view (see MainWindow's
    // PlaybackController::trackChanged wiring).
    void setCurrentlyPlaying(const QString& sourceId, const QString& trackId);
    // Horizontal padding inside the view: each row (highlight and content)
    // is inset by this much, e.g. to line up with a header above the list
    // and keep the right-hand text clear of an overlay scrollbar.
    void setRowInsets(int left, int right);

    // "Today, 16:34" / "Yesterday, 16:34" / "31.07.2026, 16:34" in the
    // viewer's local time zone (History::PlaybackHistory persists UTC —
    // see its record()). Shared with TrackHoverCard.
    static QString formatPlayedAt(const QDateTime& utcWhen);
    // "3:07"
    static QString formatDuration(qint64 ms);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
        const QModelIndex& index) override;

signals:
    // The cover thumbnail's hover-only play overlay (see paint()) was
    // clicked. MainWindow wires this to the same handler as a row
    // double-click — both mean "play this one track".
    void playRequested(const QModelIndex& index);

private:
    // Shared between paint() (drawing the hover overlay) and editorEvent()
    // (hit-testing a click against it) so they can never disagree.
    QRect thumbRect(const QRect& rowRect) const;
    QRect insetRow(const QRect& itemRect) const { return itemRect.adjusted(insetLeft_, 0, -insetRight_, 0); }

    // Cover fade-in: a cover that arrives while its row is showing the
    // placeholder fades in over it instead of popping. Keyed by cover URL;
    // mutable since paint() is const. See paintThumb().
    void paintThumb(QPainter* painter, const QRect& thumb, const QString& coverUrl, const QString& stableId,
        const QWidget* view) const;
    void tickFades();
    mutable QSet<QString> placeholderShown_; // URLs painted as a placeholder, still loading
    mutable QHash<QString, qint64> fadeStartMs_; // URL -> when its fade-in began
    mutable QHash<QWidget*, QPointer<QWidget>> fadingViews_; // viewports to repaint while fading
    QElapsedTimer clock_;
    QTimer* fadeTimer_ = nullptr;

    CoverArtCache* coverCache_;
    QString currentSourceId_;
    QString currentTrackId_;
    int insetLeft_ = 0;
    int insetRight_ = 0;
    static constexpr int kRowHeight = 52; // design system's TrackRow spec
    static constexpr int kThumbSize = 36;
    static constexpr int kBadgeSize = 14;
};

} // namespace Ui
