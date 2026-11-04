#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

#include "TrackFilterModel.h"

namespace App {
class Core;
}

namespace ViewModel {

// The main screen's list: the active playlist's rows, filtered where they are.
// A row here is not a row of the playlist, which is what this minds.
class MainScreen : public QObject {
    Q_OBJECT
    // No network: said here, for all to see, not as an error each time.
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)
    // Too little room on the disk, and what that stopped; empty otherwise.
    Q_PROPERTY(QString spaceWarning READ spaceWarning NOTIFY spaceWarningChanged)
    // How far each track downloaded ahead has got, 0 to 1, in play order.
    Q_PROPERTY(QVariantList preloadFractions READ preloadFractions NOTIFY preloadFractionsChanged)

    // Where swiping the track panel would go, and what to show coming in. Without
    // a next one a station can still go on: its next one is fetched on going there.
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY canGoNextChanged)
    Q_PROPERTY(QString nextCoverUrl READ nextCoverUrl NOTIFY nextCoverUrlChanged)
    Q_PROPERTY(QString nextSeed READ nextSeed NOTIFY nextSeedChanged)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY canGoPreviousChanged)
    Q_PROPERTY(QString previousCoverUrl READ previousCoverUrl NOTIFY previousCoverUrlChanged)
    Q_PROPERTY(QString previousSeed READ previousSeed NOTIFY previousSeedChanged)

public:
    explicit MainScreen(App::Core& core, QObject* parent = nullptr);

    TrackFilterModel& rows() { return filter_; }

    bool offline() const;
    QString spaceWarning() const;
    QVariantList preloadFractions() const;
    bool canGoNext() const { return next_.exists || radioGoesOn(); }
    QString nextCoverUrl() const { return next_.coverUrl; }
    QString nextSeed() const { return next_.seed; }
    bool canGoPrevious() const { return previous_.exists; }
    QString previousCoverUrl() const { return previous_.coverUrl; }
    QString previousSeed() const { return previous_.seed; }

public slots:
    void setFilter(const QString& text);
    // `row`: of the filtered rows.
    void playRow(int row);

signals:
    // What the app has to tell the user (ViewModel::Messages), in plain types.
    void message(int kind, const QString& text);
    void offlineChanged();
    void spaceWarningChanged();
    void preloadFractionsChanged();
    // Playback went on to another track, in this row of the filtered ones (-1:
    // not among them). Only then, for a view to go to it: a row that merely
    // moved isn't news.
    void playingRowChanged(int row);
    void canGoNextChanged();
    void nextCoverUrlChanged();
    void nextSeedChanged();
    void canGoPreviousChanged();
    void previousCoverUrlChanged();
    void previousSeedChanged();

private:
    struct Neighbor {
        bool exists = false;
        QString coverUrl;
        QString seed;
        bool operator==(const Neighbor&) const = default;
    };
    Neighbor neighbor(int delta) const;
    bool radioGoesOn() const;
    void refreshNeighbors();

    App::Core& core_;
    Neighbor next_;
    Neighbor previous_;
    TrackFilterModel filter_;
};

} // namespace ViewModel
