#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Dispatcher.h"
#include "Downloads.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "NowPlaying.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "ProxyRouting.h"
#include "Registry.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

namespace {
Track track(const QString& id)
{
    Track t;
    t.id = id;
    t.title = QStringLiteral("Track %1").arg(id);
    return t;
}

Hotkeys::Binding& bindingOf(QList<Hotkeys::Binding>& all, Hotkeys::Action action)
{
    for (Hotkeys::Binding& binding : all) {
        if (binding.action == action)
            return binding;
    }
    return all.first();
}
} // namespace

class HotkeysTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        Net::bypassProxyForLoopback();
    }

    void init()
    {
        settings_ = std::make_unique<Config::Settings>(dir_.filePath(QStringLiteral("config.ini")));
        settings_->setDownloadsEnabled(false);
        registry_ = std::make_unique<Hotkeys::Registry>(*settings_);
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        trackStates_ = std::make_unique<Library::TrackStates>();
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, *trackStates_, coverArtCache_, messages_);
        downloads_ = std::make_unique<ViewModel::Downloads>(
            *sourceManager_, *session_, *trackStates_, coverArtCache_, *settings_, messages_);
        nowPlaying_ = std::make_unique<ViewModel::NowPlaying>(
            *playback_, *sourceManager_, *trackStates_, history_, *settings_, *downloads_, messages_);
        dispatcher_ = std::make_unique<Hotkeys::Dispatcher>(*nowPlaying_, *registry_);
        QSignalSpy ready(sourceManager_.get(), &Rpc::SourceManager::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        dispatcher_.reset();
        nowPlaying_.reset();
        downloads_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        trackStates_.reset();
        registry_.reset();
        settings_.reset();
        QFile::remove(dir_.filePath(QStringLiteral("config.ini")));
    }

    void bindingsAreTheDefaultsUntilChanged()
    {
        QCOMPARE(registry_->bindings(), Hotkeys::Registry::defaultBindings());
        QVERIFY(!settings_->hotkey(QStringLiteral("like")).has_value());

        QSignalSpy changed(registry_.get(), &Hotkeys::Registry::bindingsChanged);
        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::Like).key = QKeySequence(QStringLiteral("Ctrl+Alt+J"));
        bindingOf(bindings, Hotkeys::Action::Like).notify = false;
        registry_->setBindings(bindings);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(registry_->binding(Hotkeys::Action::Like).key, QKeySequence(QStringLiteral("Ctrl+Alt+J")));
        QVERIFY(!registry_->binding(Hotkeys::Action::Like).notify);
        QVERIFY(settings_->hotkey(QStringLiteral("like")).has_value());

        // Back to the defaults: nothing is kept, and a default that later
        // changes applies again.
        registry_->setBindings(Hotkeys::Registry::defaultBindings());
        QCOMPARE(changed.count(), 2);
        QVERIFY(!settings_->hotkey(QStringLiteral("like")).has_value());
    }

    void aClearedKeyStaysCleared()
    {
        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::Stop).key = QKeySequence();
        registry_->setBindings(bindings);
        QVERIFY(registry_->binding(Hotkeys::Action::Stop).key.isEmpty());
    }

    void actionsWithoutNoticesNeverNotify()
    {
        QVERIFY(!registry_->binding(Hotkeys::Action::Next).notify);
        QVERIFY(registry_->binding(Hotkeys::Action::Like).notify);
    }

    void onlyFeedbackActionsPlaySounds()
    {
        QVERIFY(!registry_->binding(Hotkeys::Action::Next).sound);
        QVERIFY(registry_->binding(Hotkeys::Action::Like).sound);
        QVERIFY(registry_->binding(Hotkeys::Action::Dislike).sound);
        QVERIFY(registry_->binding(Hotkeys::Action::Download).sound);

        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::Like).sound = false;
        bindingOf(bindings, Hotkeys::Action::Next).sound = true;
        registry_->setBindings(bindings);
        QVERIFY(!registry_->binding(Hotkeys::Action::Like).sound);
        QVERIFY(registry_->binding(Hotkeys::Action::Like).notify);
        QVERIFY(!registry_->binding(Hotkeys::Action::Next).sound);
    }

    void theSameKeyOnTwoActionsIsAConflict()
    {
        QList<Hotkeys::Binding> bindings = registry_->bindings();
        QVERIFY(Hotkeys::Registry::conflicts(bindings).isEmpty());
        bindingOf(bindings, Hotkeys::Action::Next).key = bindingOf(bindings, Hotkeys::Action::Previous).key;
        const auto conflicts = Hotkeys::Registry::conflicts(bindings);
        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(conflicts.first().first, Hotkeys::Action::Next);
        QCOMPARE(conflicts.first().second, Hotkeys::Action::Previous);

        // Two actions without a key don't clash.
        bindingOf(bindings, Hotkeys::Action::Next).key = QKeySequence();
        bindingOf(bindings, Hotkeys::Action::Previous).key = QKeySequence();
        QVERIFY(Hotkeys::Registry::conflicts(bindings).isEmpty());
    }

    void aKeyFromTheDesktopIsAGlobalOne()
    {
        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::Download).global = false;
        registry_->setBindings(bindings);

        registry_->adoptSystemKey(QStringLiteral("download"), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F")));
        QCOMPARE(registry_->binding(Hotkeys::Action::Download).key, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F")));
        QVERIFY(registry_->binding(Hotkeys::Action::Download).global);
    }

    void volumeStepsAndStopsAtTheEnds()
    {
        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);
        nowPlaying_->setVolume(98);
        dispatcher_->trigger(Hotkeys::Action::VolumeUp);
        QCOMPARE(nowPlaying_->volume(), 100);
        // Quiet by default: the slider already shows the level.
        QCOMPARE(notices.count(), 0);

        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::VolumeUp).notify = true;
        registry_->setBindings(bindings);
        nowPlaying_->setVolume(98);
        dispatcher_->trigger(Hotkeys::Action::VolumeUp);
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().first().value<Hotkeys::Dispatcher::Notice>().body, QStringLiteral("100%"));

        nowPlaying_->setVolume(3);
        dispatcher_->trigger(Hotkeys::Action::VolumeDown);
        QCOMPARE(nowPlaying_->volume(), 0);
    }

    void quietModeKeepsItsOwnLevelAndTheKeyTogglesIt()
    {
        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);
        nowPlaying_->setQuiet(false);
        nowPlaying_->setVolume(80);
        dispatcher_->trigger(Hotkeys::Action::ToggleQuiet);
        QVERIFY(nowPlaying_->quiet());
        QCOMPARE(notices.count(), 1);
        // The slider now shows the quiet level, and editing it leaves the normal one alone.
        QCOMPARE(nowPlaying_->volume(), settings_->quietVolume());
        nowPlaying_->setVolume(15);
        QCOMPARE(settings_->quietVolume(), 15);
        QCOMPARE(settings_->volume(), 80);
        QVERIFY(settings_->quiet());

        dispatcher_->trigger(Hotkeys::Action::ToggleQuiet);
        QVERIFY(!nowPlaying_->quiet());
        QCOMPARE(nowPlaying_->volume(), 80);
        QCOMPARE(settings_->quietVolume(), 15);
        QVERIFY(!settings_->quiet());
    }

    void aLikeKeyLikesThenUnlikes()
    {
        play({ track(QStringLiteral("t1")) });
        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);

        dispatcher_->trigger(Hotkeys::Action::Like);
        QTRY_VERIFY(!nowPlaying_->feedback().likeBusy);
        QVERIFY(nowPlaying_->feedback().liked);
        QCOMPARE(notices.count(), 1);
        const auto liked = notices.last().first().value<Hotkeys::Dispatcher::Notice>();
        QCOMPARE(liked.title, QStringLiteral("Liked"));
        QCOMPARE(liked.body, QStringLiteral("Track t1"));

        dispatcher_->trigger(Hotkeys::Action::Like);
        QTRY_VERIFY(!nowPlaying_->feedback().likeBusy);
        QVERIFY(!nowPlaying_->feedback().liked);
        QCOMPARE(
            notices.last().first().value<Hotkeys::Dispatcher::Notice>().title, QStringLiteral("Removed from liked"));
    }

    void aDislikeNoticeNamesTheDislikedTrackNotTheNextOne()
    {
        // A dislike moves on to t2 at once.
        play({ track(QStringLiteral("t1")), track(QStringLiteral("t2")) });
        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);

        dispatcher_->trigger(Hotkeys::Action::Dislike);
        QCOMPARE(notices.count(), 1);
        const auto notice = notices.last().first().value<Hotkeys::Dispatcher::Notice>();
        QCOMPARE(notice.title, QStringLiteral("Disliked"));
        QCOMPARE(notice.body, QStringLiteral("Track t1"));
    }

    void noticesCanBeTurnedOffPerAction()
    {
        play({ track(QStringLiteral("t1")) });
        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::Like).notify = false;
        registry_->setBindings(bindings);

        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);
        dispatcher_->trigger(Hotkeys::Action::Like);
        QTRY_VERIFY(nowPlaying_->feedback().liked && !nowPlaying_->feedback().likeBusy);
        QCOMPARE(notices.count(), 0);
    }

    void likeKeysSoundWhatTheyDid()
    {
        play({ track(QStringLiteral("t1")) });
        QSignalSpy cues(dispatcher_.get(), &Hotkeys::Dispatcher::cueRequested);

        dispatcher_->trigger(Hotkeys::Action::Like);
        QTRY_VERIFY(!nowPlaying_->feedback().likeBusy);
        QCOMPARE(cues.count(), 1);
        QCOMPARE(cues.last().first().value<Playback::Cue>(), Playback::Cue::Like);

        dispatcher_->trigger(Hotkeys::Action::Like);
        QTRY_VERIFY(!nowPlaying_->feedback().likeBusy);
        QCOMPARE(cues.count(), 2);
        QCOMPARE(cues.last().first().value<Playback::Cue>(), Playback::Cue::Unlike);
    }

    void aDislikeKeySoundsADislike()
    {
        play({ track(QStringLiteral("t1")), track(QStringLiteral("t2")) });
        QSignalSpy cues(dispatcher_.get(), &Hotkeys::Dispatcher::cueRequested);
        dispatcher_->trigger(Hotkeys::Action::Dislike);
        QCOMPARE(cues.count(), 1);
        QCOMPARE(cues.last().first().value<Playback::Cue>(), Playback::Cue::Dislike);
    }

    void soundsCanBeTurnedOffPerAction()
    {
        play({ track(QStringLiteral("t1")) });
        QList<Hotkeys::Binding> bindings = registry_->bindings();
        bindingOf(bindings, Hotkeys::Action::Like).sound = false;
        registry_->setBindings(bindings);

        QSignalSpy cues(dispatcher_.get(), &Hotkeys::Dispatcher::cueRequested);
        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);
        dispatcher_->trigger(Hotkeys::Action::Like);
        QTRY_VERIFY(nowPlaying_->feedback().liked && !nowPlaying_->feedback().likeBusy);
        QCOMPARE(cues.count(), 0);
        QCOMPARE(notices.count(), 1);
    }

    void withNothingPlayingALikeKeyIsSilent()
    {
        QSignalSpy cues(dispatcher_.get(), &Hotkeys::Dispatcher::cueRequested);
        dispatcher_->trigger(Hotkeys::Action::Like);
        QCOMPARE(cues.count(), 0);
    }

    void withNothingPlayingALikeKeySaysSo()
    {
        QSignalSpy notices(dispatcher_.get(), &Hotkeys::Dispatcher::noticeRequested);
        dispatcher_->trigger(Hotkeys::Action::Like);
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().first().value<Hotkeys::Dispatcher::Notice>().body, QStringLiteral("Nothing to like"));
    }

    void showPlayerPassesOnTheActivationToken()
    {
        QSignalSpy shown(dispatcher_.get(), &Hotkeys::Dispatcher::showPlayerRequested);
        dispatcher_->trigger(QStringLiteral("showPlayer"), QStringLiteral("token-1"));
        QCOMPARE(shown.count(), 1);
        QCOMPARE(shown.first().first().toString(), QStringLiteral("token-1"));
    }

private:
    void play(const QList<Track>& tracks)
    {
        QSignalSpy changed(nowPlaying_.get(), &ViewModel::NowPlaying::trackChanged);
        playback_->loadQueue(QStringLiteral("fake"), tracks, 0);
        QVERIFY(changed.wait(10000));
        QVERIFY(nowPlaying_->hasTrack());
    }

    QTemporaryDir dir_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<Hotkeys::Registry> registry_;
    History::PlaybackHistory history_;
    ViewModel::Messages messages_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<Library::TrackStates> trackStates_;
    Rpc::AuthStates authStates_;
    Covers::CoverArtCache coverArtCache_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<ViewModel::Downloads> downloads_;
    std::unique_ptr<ViewModel::NowPlaying> nowPlaying_;
    std::unique_ptr<Hotkeys::Dispatcher> dispatcher_;
};

QObject* makeHotkeysTest() { return new HotkeysTest; }

} // namespace Tests

#include "HotkeysTest.moc"
