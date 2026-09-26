#pragma once

#include <QObject>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QTimer>

#include <functional>

class QAbstractItemView;

namespace Covers {
class CoverArtCache;
}

namespace Ui {

// Hover card for track lists: once the cursor rests on a row for a moment,
// a floating card shows the track's large cover and everything known
// about it — title, artists, album, duration, source, like/dislike,
// explicit, when it was last played. Reads the row through
// TrackListModel's roles, so it works on any view over one (directly or
// through a proxy). Hides on leaving the row, clicking, or scrolling.
//
// One call per view, lives as long as it:
//   TrackHoverCard::attach(view, coverCache, sourceNameFn);
class TrackHoverCard : public QObject {
    Q_OBJECT

public:
    // Maps a sourceId to its display name ("Yandex Music").
    using SourceNameFn = std::function<QString(const QString& sourceId)>;

    static void attach(QAbstractItemView* view, Covers::CoverArtCache* coverCache, SourceNameFn sourceName);
    // The card is a parentless top-level window — deleted with this.
    ~TrackHoverCard() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    TrackHoverCard(QAbstractItemView* view, Covers::CoverArtCache* coverCache, SourceNameFn sourceName);

    void showFor(const QModelIndex& index, const QPoint& globalPos);
    void hideCard();

    QAbstractItemView* view_;
    Covers::CoverArtCache* coverCache_;
    SourceNameFn sourceName_;
    QPersistentModelIndex shownIndex_;
    // The row the cursor is resting on and where, until restTimer_ fires.
    QTimer restTimer_;
    QPersistentModelIndex restIndex_;
    QPoint restGlobalPos_;
    // Concrete popup type lives in the .cpp (see ThemedToolTip's).
    QWidget* popup_ = nullptr;
};

} // namespace Ui
