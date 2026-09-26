#include <QSignalSpy>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

class SourceSessionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { installFakeBackendManifest(); }

    void init()
    {
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        QSignalSpy ready(session_.get(), &App::SourceSession::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
    }

    void aSourcesErrorNotificationIsReported()
    {
        QSignalSpy errors(&messages_, &ViewModel::Messages::posted);
        await(Rpc::catalogListPlaylists(*sourceManager_->client(QStringLiteral("fake"))));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().at(1).toString(), QStringLiteral("heads up"));
    }

    void signingInAnAuthenticatedSourceReportsNothing()
    {
        QSignalSpy errors(&messages_, &ViewModel::Messages::posted);
        await(session_->signIn(QStringLiteral("fake")));
        QCOMPARE(errors.count(), 0);
    }

    void aRejectedSubmitReturnsAndReportsWhy()
    {
        QSignalSpy errors(&messages_, &ViewModel::Messages::posted);
        const QString error = await(
            session_->submitSignIn(QStringLiteral("fake"), { { QStringLiteral("password"), QStringLiteral("nope") } }));
        QCOMPARE(error, QStringLiteral("wrong password"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().at(1).toString(), QStringLiteral("Fake Source: wrong password"));
    }

private:
    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
};

QObject* makeSourceSessionTest() { return new SourceSessionTest; }

} // namespace Tests

#include "SourceSessionTest.moc"
