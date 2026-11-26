#include <QSignalSpy>
#include <QTest>

#include "Models.h"
#include "TrackListModel.h"

namespace Tests {

namespace {

using Entry = ViewModel::TrackListModel::MixedSourceEntry;

QList<Entry> rows(const QStringList& ids)
{
    QList<Entry> result;
    for (const QString& id : ids) {
        Track track;
        track.id = id;
        track.title = id;
        track.durationMs = 1000;
        result.append({ QStringLiteral("src"), track, QDateTime() });
    }
    return result;
}

QStringList ids(const ViewModel::TrackListModel& model)
{
    QStringList result;
    for (const Track& track : model.allTracks())
        result.append(track.id);
    return result;
}

} // namespace

// What a view showing the list keeps: its scroll position and what's drawn, as
// long as nothing says the whole list is another.
class TrackListUpdateTest : public QObject {
    Q_OBJECT

    ViewModel::TrackListModel model_;

private slots:
    void init() { model_.setMixedSourceTracks(rows(QStringList { "a", "b", "c", "d" })); }

    void theSameListTellsNothing()
    {
        QSignalSpy reset(&model_, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model_, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model_, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model_, &QAbstractItemModel::dataChanged);
        model_.updateMixedSourceTracks(rows(QStringList { "a", "b", "c", "d" }));
        QCOMPARE(reset.size() + removed.size() + inserted.size() + changed.size(), 0);
    }

    // The track that plays changing touches its row and the one before, not
    // every row: a view in another process fetches each row it's told of.
    void theCurrentTrackTouchesTwoRows()
    {
        model_.setCurrentTrack(QStringLiteral("src"), QStringLiteral("a"));
        QSignalSpy changed(&model_, &QAbstractItemModel::dataChanged);
        model_.setCurrentTrack(QStringLiteral("src"), QStringLiteral("c"));
        QCOMPARE(changed.size(), 2);
        QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 0);
        QCOMPARE(changed.at(0).at(1).value<QModelIndex>().row(), 0);
        QCOMPARE(changed.at(1).at(0).value<QModelIndex>().row(), 2);
        QVERIFY(model_.index(2).data(ViewModel::TrackListModel::IsCurrentRole).toBool());
        QVERIFY(!model_.index(0).data(ViewModel::TrackListModel::IsCurrentRole).toBool());
    }

    // History's rows kept in place take their new time of play.
    void aKeptRowTakesItsNewPlayTime()
    {
        QList<Entry> again = rows(QStringList { "a", "b", "c", "d" });
        again[1].playedAt = QDateTime::fromSecsSinceEpoch(1000);
        QSignalSpy changed(&model_, &QAbstractItemModel::dataChanged);
        model_.updateMixedSourceTracks(again);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);
    }

    void aStationGrowingOnlyAddsToTheEnd()
    {
        QSignalSpy reset(&model_, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model_, &QAbstractItemModel::rowsInserted);
        model_.updateMixedSourceTracks(rows(QStringList { "a", "b", "c", "d", "e", "f" }));
        QCOMPARE(reset.size(), 0);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(inserted.first().at(1).toInt(), 4);
        QCOMPARE(inserted.first().at(2).toInt(), 5);
        QCOMPARE(ids(model_), (QStringList { "a", "b", "c", "d", "e", "f" }));
    }

    // What was played and downloaded stays; what's ahead is another now.
    void aStationReplacesOnlyWhatsAhead()
    {
        QSignalSpy reset(&model_, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model_, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model_, &QAbstractItemModel::rowsInserted);
        model_.updateMixedSourceTracks(rows(QStringList { "a", "b", "x", "y", "z" }));
        QCOMPARE(reset.size(), 0);
        QCOMPARE(removed.first().at(1).toInt(), 2);
        QCOMPARE(removed.first().at(2).toInt(), 3);
        QCOMPARE(inserted.first().at(1).toInt(), 2);
        QCOMPARE(inserted.first().at(2).toInt(), 4);
        QCOMPARE(ids(model_), (QStringList { "a", "b", "x", "y", "z" }));
    }

    void whatAStationLetsGoOfGoesFromTheStart()
    {
        QSignalSpy reset(&model_, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model_, &QAbstractItemModel::rowsRemoved);
        model_.updateMixedSourceTracks(rows(QStringList { "c", "d", "e" }));
        QCOMPARE(reset.size(), 0);
        QCOMPARE(removed.first().at(1).toInt(), 0);
        QCOMPARE(removed.first().at(2).toInt(), 1);
        QCOMPARE(ids(model_), (QStringList { "c", "d", "e" }));
    }

    void aRowFromTheMiddleGoesAlone()
    {
        QSignalSpy reset(&model_, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model_, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model_, &QAbstractItemModel::rowsInserted);
        model_.updateMixedSourceTracks(rows(QStringList { "a", "c", "d" }));
        QCOMPARE(reset.size(), 0);
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(removed.first().at(1).toInt(), 1);
        QCOMPARE(removed.first().at(2).toInt(), 1);
        QCOMPARE(ids(model_), (QStringList { "a", "c", "d" }));
    }

    void aRepeatedTrackDoesntConfuseIt()
    {
        model_.setMixedSourceTracks(rows(QStringList { "a", "b", "a", "c" }));
        model_.updateMixedSourceTracks(rows(QStringList { "a", "b", "a", "c", "a" }));
        QCOMPARE(ids(model_), (QStringList { "a", "b", "a", "c", "a" }));
        model_.updateMixedSourceTracks(rows(QStringList { "b", "a", "x" }));
        QCOMPARE(ids(model_), (QStringList { "b", "a", "x" }));
    }

    void anotherListIsANewOne()
    {
        QSignalSpy reset(&model_, &QAbstractItemModel::modelReset);
        model_.updateMixedSourceTracks(rows(QStringList { "x", "y" }));
        QCOMPARE(reset.size(), 1);
        QCOMPARE(ids(model_), (QStringList { "x", "y" }));
    }

    void aRowThatLooksDifferentIsToldOf()
    {
        QSignalSpy changed(&model_, &QAbstractItemModel::dataChanged);
        QList<Entry> fresh = rows(QStringList { "a", "b", "c", "d" });
        fresh[1].track.title = QStringLiteral("B!");
        model_.updateMixedSourceTracks(fresh);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 1);
        QCOMPARE(model_.allTracks().at(1).title, QStringLiteral("B!"));
    }

    // A playlist with no picture always gets a glyph: the source's, else its kind's.
    void playlistCoverIconFallsBackToKind()
    {
        Playlist playlist;
        playlist.kind = QStringLiteral("playlist");
        QCOMPARE(ViewModel::playlistCoverIcon(playlist), QStringLiteral("playlist"));
        playlist.kind = QStringLiteral("liked");
        QCOMPARE(ViewModel::playlistCoverIcon(playlist), QStringLiteral("liked"));
        playlist.kind = QStringLiteral("radioStation");
        QCOMPARE(ViewModel::playlistCoverIcon(playlist), QStringLiteral("wave"));
        playlist.coverIcon = QString();
        QCOMPARE(ViewModel::playlistCoverIcon(playlist), QStringLiteral("wave"));
        playlist.coverIcon = QStringLiteral("album");
        QCOMPARE(ViewModel::playlistCoverIcon(playlist), QStringLiteral("album"));
    }
};

QObject* makeTrackListUpdateTest() { return new TrackListUpdateTest; }

} // namespace Tests

#include "TrackListUpdateTest.moc"
