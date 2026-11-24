#include <QCoreApplication>
#include <QStandardPaths>
#include <QTest>

#include <cstring>
#include <memory>

#include "FakeBackend.h"

namespace Tests {
QObject* makeActivePlaylistTest();
QObject* makeAnalyticsTest();
QObject* makeAudioFileProbeTest();
QObject* makeAudioPulseTest();
QObject* makeConnectivityTest();
QObject* makeCoverArtCacheTest();
QObject* makeDownloadsTest();
QObject* makeI18nTest();
QObject* makeNowPlayingTest();
QObject* makeOutboxTest();
QObject* makeParallelDownloadTest();
QObject* makePathsTest();
QObject* makePlaybackPreloadTest();
QObject* makePlaylistEditingTest();
QObject* makePreloaderTest();
QObject* makePreloadTest();
QObject* makeRemotingTest();
QObject* makeRpcClientTest();
QObject* makeSequenceTest();
QObject* makeSourceSessionTest();
QObject* makeSourceSettingsTest();
QObject* makeSourcesTest();
QObject* makeStreamRelayTest();
QObject* makeTimingTest();
QObject* makeTrackCacheTest();
QObject* makeTrackFileTest();
QObject* makeTrackListUpdateTest();
QObject* makeTrackStatesTest();
}

int main(int argc, char** argv)
{
    // The same binary doubles as the backend the tests start — see
    // FakeBackend.h.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], Tests::kFakeBackendArgument) == 0)
            return Tests::runFakeBackend();
    }

    QCoreApplication app(argc, argv);
    // Anything a test saves goes to throwaway locations, not the user's.
    QStandardPaths::setTestModeEnabled(true);
    int failures = 0;
    for (auto make : { Tests::makeActivePlaylistTest,
             Tests::makeAnalyticsTest,
             Tests::makeAudioFileProbeTest,
             Tests::makeAudioPulseTest,
             Tests::makeConnectivityTest,
             Tests::makeCoverArtCacheTest,
             Tests::makeDownloadsTest,
             Tests::makeI18nTest,
             Tests::makeNowPlayingTest,
             Tests::makeOutboxTest,
             Tests::makeParallelDownloadTest,
             Tests::makePathsTest,
             Tests::makePlaybackPreloadTest,
             Tests::makePlaylistEditingTest,
             Tests::makePreloaderTest,
             Tests::makePreloadTest,
             Tests::makeRemotingTest,
             Tests::makeRpcClientTest,
             Tests::makeSequenceTest,
             Tests::makeSourceSessionTest,
             Tests::makeSourceSettingsTest,
             Tests::makeSourcesTest,
             Tests::makeStreamRelayTest,
             Tests::makeTimingTest,
             Tests::makeTrackCacheTest,
             Tests::makeTrackFileTest,
             Tests::makeTrackListUpdateTest,
             Tests::makeTrackStatesTest }) {
        std::unique_ptr<QObject> test(make());
        failures += QTest::qExec(test.get(), argc, argv);
    }
    return failures == 0 ? 0 : 1;
}
