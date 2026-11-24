#include <QMetaObject>
#include <QMetaProperty>
#include <QSet>
#include <QTest>

#include "ActivePlaylist.h"
#include "CoverArtCache.h"
#include "Models.h"
#include "NowPlaying.h"
#include "TrackListModel.h"

namespace Tests {

// What the mobile front's process boundary asks of the view models it shares
// with the desktop (fronts/mobile): see ViewModel::NowPlaying's comment.
class RemotingTest : public QObject {
    Q_OBJECT

private slots:
    void propertiesHaveANotifySignalOfTheirOwn_data()
    {
        QTest::addColumn<const QMetaObject*>("meta");
        QTest::newRow("NowPlaying") << &ViewModel::NowPlaying::staticMetaObject;
        QTest::newRow("ActivePlaylist") << &ViewModel::ActivePlaylist::staticMetaObject;
    }

    void propertiesHaveANotifySignalOfTheirOwn()
    {
        QFETCH(const QMetaObject*, meta);
        QSet<int> signalsSeen;
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const QMetaProperty property = meta->property(i);
            QVERIFY2(property.hasNotifySignal(), property.name());
            QVERIFY2(!signalsSeen.contains(property.notifySignalIndex()), property.name());
            signalsSeen.insert(property.notifySignalIndex());
        }
    }

    void trackRowsSpeakPlainTypes()
    {
        ViewModel::TrackListModel model;
        Track track;
        track.id = QStringLiteral("t1");
        track.title = QStringLiteral("Title");
        track.artists = { Artist { QStringLiteral("a1"), QStringLiteral("One") },
            Artist { QStringLiteral("a2"), QStringLiteral("Two") } };
        track.durationMs = 1000;
        model.setTracks(QStringLiteral("src"), { track });

        const QHash<int, QByteArray> names = model.roleNames();
        const auto value = [&](const QByteArray& name) { return model.data(model.index(0), names.key(name)); };
        QCOMPARE(value("sourceId").toString(), QStringLiteral("src"));
        QCOMPARE(value("trackId").toString(), QStringLiteral("t1"));
        QCOMPARE(value("title").toString(), QStringLiteral("Title"));
        QCOMPARE(value("artist").toString(), QStringLiteral("One, Two"));
        QCOMPARE(value("durationMs").toInt(), 1000);
        // No cover cache: nothing to show, and no crash.
        QCOMPARE(value("coverUrl").toString(), QString());
    }
};

QObject* makeRemotingTest() { return new RemotingTest; }

} // namespace Tests

#include "RemotingTest.moc"
