#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "ActivePlaylist.h"
#include "AuthStates.h"
#include "Browse.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourcePage.h"
#include "SourceSession.h"
#include "Sources.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

class BrowseTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { installFakeBackendManifest(); }

    void init()
    {
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cloudmus/fronts/qt/config.ini"));
        settings_ = std::make_unique<Config::Settings>();
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        sourcePage_.reset();
        browse_.reset();
        active_.reset();
        sources_.reset();
        editing_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        settings_.reset();
    }

    void openingAPlaylistListsItsTracksAndIsRemembered()
    {
        start();
        browse_->openPlaylist(QStringLiteral("fake"), playlist(QStringLiteral("p1")));
        QCOMPARE(browse_->page(), ViewModel::Browse::Page::Playlist);
        QVERIFY(browse_->isLoading());
        QTRY_COMPARE(browse_->rows().size(), 2);
        QVERIFY(!browse_->isLoading());
        QCOMPARE(settings_->sidebarSelection(), QStringLiteral("playlist:fake:p1"));
    }

    void playingARowMakesItActiveAndClosesTheSheet()
    {
        start();
        browse_->openPlaylist(QStringLiteral("fake"), playlist(QStringLiteral("p1")));
        QTRY_COMPARE(browse_->rows().size(), 2);
        browse_->playRow(1);
        QCOMPARE(browse_->page(), ViewModel::Browse::Page::None);
        QCOMPARE(active_->context().playlist.id, QStringLiteral("p1"));
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
    }

    void anEditToTheOpenPlaylistShowsInItsRows()
    {
        start();
        browse_->openPlaylist(QStringLiteral("fake"), playlist(QStringLiteral("p1")));
        QTRY_COMPARE(browse_->rows().size(), 2);
        Track added;
        added.id = QStringLiteral("t3");
        QVERIFY(
            await(editing_->setTrackInPlaylist(QStringLiteral("fake"), added, playlist(QStringLiteral("p1")), true)));
        QCOMPARE(browse_->rows().size(), 3);
        QCOMPARE(browse_->context().playlist.trackCount, 3);
    }

    void theLastOpenPageComesBackOnceItsSourceListsIt()
    {
        settings_->setSidebarSelection(QStringLiteral("playlist:fake:p2"));
        start();
        QCOMPARE(browse_->page(), ViewModel::Browse::Page::Playlist);
        QCOMPARE(browse_->context().playlist.id, QStringLiteral("p2"));
    }

    void aSourcesPageClosesWhenTheSourceGoesAway()
    {
        start();
        browse_->openSource(QStringLiteral("fake"));
        QCOMPARE(settings_->sidebarSelection(), QStringLiteral("source:fake"));
        emit sourceManager_->sourceStopped(QStringLiteral("fake"));
        QCOMPARE(browse_->page(), ViewModel::Browse::Page::None);
    }

    void aRejectedSignInAnswerIsReportedToThePage()
    {
        start();
        QSignalSpy failed(sourcePage_.get(), &ViewModel::SourcePage::submitFailed);
        QSignalSpy busy(sourcePage_.get(), &ViewModel::SourcePage::busyChanged);
        sourcePage_->submit(QStringLiteral("fake"), { { QStringLiteral("password"), QStringLiteral("nope") } });
        QVERIFY(sourcePage_->isBusy(QStringLiteral("fake")));
        QVERIFY(failed.wait(10000));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("wrong password"));
        QVERIFY(!sourcePage_->isBusy(QStringLiteral("fake")));
        QCOMPARE(busy.count(), 2);
    }

private:
    static Playlist playlist(const QString& id)
    {
        Playlist p;
        p.id = id;
        p.title = id;
        p.kind = QStringLiteral("playlist");
        return p;
    }

    void start()
    {
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        editing_ = std::make_unique<App::PlaylistEditing>(*sourceManager_, messages_);
        sources_ = std::make_unique<ViewModel::Sources>(
            *sourceManager_, *session_, authStates_, *editing_, coverArtCache_, *settings_, messages_);
        active_ = std::make_unique<ViewModel::ActivePlaylist>(*playback_, *sourceManager_, trackStates_, coverArtCache_,
            history_, *settings_, *sources_, *editing_, messages_);
        browse_ = std::make_unique<ViewModel::Browse>(*sourceManager_, trackStates_, coverArtCache_, history_,
            *settings_, *sources_, *active_, *editing_, messages_);
        sourcePage_ = std::make_unique<ViewModel::SourcePage>(*session_);
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
    }

    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    History::PlaybackHistory history_;
    ViewModel::Messages messages_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<App::PlaylistEditing> editing_;
    std::unique_ptr<ViewModel::Sources> sources_;
    std::unique_ptr<ViewModel::ActivePlaylist> active_;
    std::unique_ptr<ViewModel::Browse> browse_;
    std::unique_ptr<ViewModel::SourcePage> sourcePage_;
};

QObject* makeBrowseTest() { return new BrowseTest; }

} // namespace Tests

#include "BrowseTest.moc"
