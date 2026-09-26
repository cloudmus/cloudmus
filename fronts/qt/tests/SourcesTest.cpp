#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "Sources.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

namespace {
// The ids of the playlists the sidebar shows under the source's root, in
// order (favorites first, then the "Playlists" group's).
QStringList sidebarIds(ViewModel::SidebarModel& model)
{
    QStringList ids;
    const QModelIndex root = model.indexForSource(QStringLiteral("fake"));
    const std::function<void(const QModelIndex&)> collect = [&](const QModelIndex& parent) {
        for (int row = 0; row < model.rowCount(parent); ++row) {
            const QModelIndex index = model.index(row, 0, parent);
            const QString id = index.data(ViewModel::SidebarModel::PlaylistIdRole).toString();
            if (!id.isEmpty())
                ids.append(id);
            collect(index);
        }
    };
    collect(root);
    return ids;
}
} // namespace

class SourcesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { installFakeBackendManifest(); }

    void init()
    {
        // A fresh settings file — favorites and collapsed rows live there
        // (the test-mode location, see main.cpp).
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cloudmus/fronts/qt/config.ini"));
        settings_ = std::make_unique<Config::Settings>();
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        editing_ = std::make_unique<App::PlaylistEditing>(*sourceManager_, messages_);
        sources_ = std::make_unique<ViewModel::Sources>(
            *sourceManager_, *session_, authStates_, *editing_, coverArtCache_, *settings_, messages_);
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        sources_.reset();
        editing_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        settings_.reset();
    }

    void aStartingSourceShowsAsLoadingThenListsItsPlaylists()
    {
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(sources_->model().indexForSource(QStringLiteral("fake")).isValid());
        QVERIFY(sources_->isLoading(QStringLiteral("fake")));
        QVERIFY(loaded.wait(10000));
        QVERIFY(!sources_->isLoading(QStringLiteral("fake")));
        QCOMPARE(sources_->playlists(QStringLiteral("fake")).size(), 2);
        QCOMPARE(sidebarIds(sources_->model()), (QStringList { QStringLiteral("p1"), QStringLiteral("p2") }));
        // Handed on to playlist editing: only the editable one.
        QCOMPARE(editing_->editablePlaylists(QStringLiteral("fake")).size(), 1);
    }

    void aFavoriteMovesToTheTopAndSurvivesARefresh()
    {
        startAndLoad();
        QVERIFY(!sources_->toggleFavorite(QStringLiteral("fake"), QStringLiteral("p2")));
        QVERIFY(sources_->isFavorite(QStringLiteral("fake"), QStringLiteral("p2")));
        QCOMPARE(sidebarIds(sources_->model()), (QStringList { QStringLiteral("p2"), QStringLiteral("p1") }));
        QCOMPARE(settings_->favorites(QStringLiteral("fake")), std::optional<QStringList>({ QStringLiteral("p2") }));

        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sources_->refresh(QStringLiteral("fake"));
        QVERIFY(loaded.wait(10000));
        QCOMPARE(sidebarIds(sources_->model()), (QStringList { QStringLiteral("p2"), QStringLiteral("p1") }));
    }

    void signingOutEmptiesTheSourcesList()
    {
        startAndLoad();
        authStates_.setSignedOut(QStringLiteral("fake"));
        QVERIFY(sources_->playlists(QStringLiteral("fake")).isEmpty());
        QVERIFY(editing_->editablePlaylists(QStringLiteral("fake")).isEmpty());
    }

    void collapsedRowsAreRemembered()
    {
        sources_->setCollapsed(QStringLiteral("source:fake"), true);
        QVERIFY(sources_->isCollapsed(QStringLiteral("source:fake")));
        QCOMPARE(settings_->sidebarCollapsed(), QStringList { QStringLiteral("source:fake") });
        sources_->setCollapsed(QStringLiteral("source:fake"), false);
        QVERIFY(settings_->sidebarCollapsed().isEmpty());
    }

private:
    void startAndLoad()
    {
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
    }

    std::unique_ptr<Config::Settings> settings_;
    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<App::PlaylistEditing> editing_;
    std::unique_ptr<ViewModel::Sources> sources_;
};

QObject* makeSourcesTest() { return new SourcesTest; }

} // namespace Tests

#include "SourcesTest.moc"
