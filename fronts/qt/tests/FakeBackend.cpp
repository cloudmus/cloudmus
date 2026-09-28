#include "FakeBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <cstdio>
#include <iostream>
#include <string>

namespace Tests {

namespace {

void send(const QJsonObject& message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void reply(const QJsonValue& id, const QJsonObject& result)
{
    send({ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), id },
        { QStringLiteral("result"), result } });
}

void replyError(const QJsonValue& id, int code, const QString& message)
{
    send({ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), id },
        { QStringLiteral("error"),
            QJsonObject { { QStringLiteral("code"), code }, { QStringLiteral("message"), message } } } });
}

void notify(const QString& method, const QJsonObject& params)
{
    send({ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("method"), method },
        { QStringLiteral("params"), params } });
}

QJsonObject capabilities()
{
    return {
        { QStringLiteral("playback"),
            QJsonObject { { QStringLiteral("providesStream"), true }, { QStringLiteral("selfPlayback"), false },
                { QStringLiteral("controls"),
                    QJsonObject { { QStringLiteral("pause"), false }, { QStringLiteral("seek"), false },
                        { QStringLiteral("volume"), false } } } } },
        { QStringLiteral("browse"),
            QJsonObject { { QStringLiteral("playlists"), true }, { QStringLiteral("likedTracks"), true },
                { QStringLiteral("radio"), false }, { QStringLiteral("search"), false },
                { QStringLiteral("editPlaylists"), true } } },
        { QStringLiteral("feedback"),
            QJsonObject { { QStringLiteral("like"), true }, { QStringLiteral("dislike"), true },
                { QStringLiteral("skip"), false } } },
        { QStringLiteral("download"), true },
        { QStringLiteral("downloadControl"), true },
        { QStringLiteral("auth"),
            QJsonObject { { QStringLiteral("required"), false }, { QStringLiteral("flow"), QStringLiteral("none") } } },
    };
}

} // namespace

QStringList fakeBackendArgv()
{
    return { QCoreApplication::applicationFilePath(), QString::fromLatin1(kFakeBackendArgument) };
}

void installFakeBackendManifest()
{
    qunsetenv("CLOUDMUS_DEV_BACKENDS");
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/cloudmus/backends.d"));
    dir.removeRecursively();
    dir.mkpath(QStringLiteral("."));
    const QJsonObject manifest {
        { QStringLiteral("id"), QStringLiteral("fake") },
        { QStringLiteral("name"), QStringLiteral("Fake Source") },
        { QStringLiteral("argv"), QJsonArray::fromStringList(fakeBackendArgv()) },
        { QStringLiteral("protocolVersion"), QStringLiteral("1.6") },
    };
    QFile file(dir.filePath(QStringLiteral("fake.json")));
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(manifest).toJson());
}

int runFakeBackend()
{
    // A catalog.downloadTrack of the track "slow" waits here, unanswered,
    // until a catalog.cancelDownload for it.
    QJsonValue slowDownloadRequestId;
    QString slowDownloadId;
    std::string line;
    while (std::getline(std::cin, line)) {
        const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString method = request.value(QStringLiteral("method")).toString();
        const QJsonValue id = request.value(QStringLiteral("id"));
        if (id.isUndefined())
            continue; // a notification from the front — nothing to answer

        if (method == QStringLiteral("initialize")) {
            reply(id,
                { { QStringLiteral("protocolVersion"), QStringLiteral("1.6") },
                    { QStringLiteral("source"),
                        QJsonObject { { QStringLiteral("id"), QStringLiteral("fake") },
                            { QStringLiteral("name"), QStringLiteral("Fake Source") } } },
                    { QStringLiteral("capabilities"), capabilities() } });
        } else if (method == QStringLiteral("catalog.listPlaylists")) {
            // A notification ahead of the reply, as a real backend may send
            // one at any time.
            notify(QStringLiteral("error"),
                { { QStringLiteral("code"), 1200 }, { QStringLiteral("message"), QStringLiteral("heads up") } });
            reply(id,
                { { QStringLiteral("playlists"),
                    QJsonArray {
                        QJsonObject { { QStringLiteral("id"), QStringLiteral("p1") },
                            { QStringLiteral("title"), QStringLiteral("First") }, { QStringLiteral("trackCount"), 2 },
                            { QStringLiteral("kind"), QStringLiteral("playlist") },
                            { QStringLiteral("editable"), true } },
                        // Saved from someone else — not the user's to edit.
                        QJsonObject { { QStringLiteral("id"), QStringLiteral("p2") },
                            { QStringLiteral("title"), QStringLiteral("Saved") }, { QStringLiteral("trackCount"), 5 },
                            { QStringLiteral("kind"), QStringLiteral("playlist") } },
                    } } });
        } else if (method == QStringLiteral("catalog.listTracks")) {
            const auto track = [](const QString& trackId) {
                return QJsonObject { { QStringLiteral("id"), trackId },
                    { QStringLiteral("title"), QStringLiteral("Track %1").arg(trackId) },
                    { QStringLiteral("artists"), QJsonArray { } }, { QStringLiteral("durationMs"), 180000 } };
            };
            reply(id,
                { { QStringLiteral("tracks"),
                    QJsonArray { track(QStringLiteral("t1")), track(QStringLiteral("t2")) } } });
        } else if (method == QStringLiteral("catalog.listLiked")) {
            reply(id,
                { { QStringLiteral("tracks"),
                    QJsonArray { QJsonObject { { QStringLiteral("id"), QStringLiteral("old-2") },
                        { QStringLiteral("title"), QStringLiteral("Old track 2") },
                        { QStringLiteral("artists"), QJsonArray { } }, { QStringLiteral("durationMs"), 180000 },
                        { QStringLiteral("liked"), true } } } } });
        } else if (method == QStringLiteral("catalog.startRadio")) {
            reply(id,
                { { QStringLiteral("stationId"), QStringLiteral("station") },
                    { QStringLiteral("initialTracks"),
                        QJsonArray { QJsonObject { { QStringLiteral("id"), QStringLiteral("old-2") },
                                         { QStringLiteral("title"), QStringLiteral("Stale old track") },
                                         { QStringLiteral("artists"), QJsonArray { } },
                                         { QStringLiteral("durationMs"), 180000 }, { QStringLiteral("liked"), false } },
                            QJsonObject { { QStringLiteral("id"), QStringLiteral("fresh") },
                                { QStringLiteral("title"), QStringLiteral("Fresh track") },
                                { QStringLiteral("artists"), QJsonArray { } },
                                { QStringLiteral("durationMs"), 180000 } } } } });
        } else if (method == QStringLiteral("catalog.getTrackPlaylists")) {
            reply(id, { { QStringLiteral("playlistIds"), QJsonArray { QStringLiteral("p1") } } });
        } else if (method == QStringLiteral("catalog.addToPlaylist")) {
            reply(id, { { QStringLiteral("trackCount"), 3 } });
        } else if (method == QStringLiteral("catalog.removeFromPlaylist")) {
            // A track id of "fail" can't be removed.
            const QString trackId
                = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("trackId")).toString();
            if (trackId == QStringLiteral("fail"))
                replyError(id, 1300, QStringLiteral("not in the playlist"));
            else
                reply(id, { { QStringLiteral("trackCount"), 1 } });
        } else if (method == QStringLiteral("playback.play")) {
            // Accepted, but no stream follows: the track counts as current
            // (what the tests look at) without anything actually playing.
            // Except for "refused": its stream is one nothing answers at.
            reply(id, { { QStringLiteral("accepted"), true } });
            const QString trackId
                = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("trackId")).toString();
            if (trackId == QStringLiteral("refused"))
                notify(QStringLiteral("track/streamReady"),
                    { { QStringLiteral("requestId"), id }, { QStringLiteral("trackId"), trackId },
                        { QStringLiteral("stream"),
                            QJsonObject { { QStringLiteral("kind"), QStringLiteral("url") },
                                { QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9/refused") },
                                { QStringLiteral("mimeType"), QStringLiteral("audio/mpeg") } } } });
        } else if (method.startsWith(QStringLiteral("feedback."))) {
            // A track id of "fail" makes any feedback call fail.
            const QString trackId
                = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("trackId")).toString();
            if (trackId == QStringLiteral("fail"))
                replyError(id, 1200, QStringLiteral("service down"));
            else
                reply(id, { });
        } else if (method == QStringLiteral("catalog.downloadTrack")) {
            const QJsonObject params = request.value(QStringLiteral("params")).toObject();
            const QString trackId = params.value(QStringLiteral("trackId")).toString();
            const QString downloadId = params.value(QStringLiteral("downloadId")).toString();
            const auto progress = [&](int received) {
                notify(QStringLiteral("download/progress"),
                    { { QStringLiteral("downloadId"), downloadId }, { QStringLiteral("receivedBytes"), received },
                        { QStringLiteral("totalBytes"), 100 } });
            };
            if (trackId == QStringLiteral("fail")) {
                replyError(id, 1300, QStringLiteral("no such track"));
            } else if (trackId == QStringLiteral("slow")) {
                progress(50);
                slowDownloadRequestId = id;
                slowDownloadId = downloadId;
            } else {
                progress(50);
                progress(100);
                reply(id,
                    { { QStringLiteral("path"),
                        params.value(QStringLiteral("destDir")).toString() + QStringLiteral("/") + trackId } });
            }
        } else if (method == QStringLiteral("catalog.cancelDownload")) {
            const QString downloadId
                = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("downloadId")).toString();
            reply(id, { });
            if (downloadId == slowDownloadId && !slowDownloadRequestId.isUndefined()) {
                replyError(slowDownloadRequestId, 1410, QStringLiteral("Download cancelled"));
                slowDownloadRequestId = QJsonValue();
                slowDownloadId.clear();
            }
        } else if (method == QStringLiteral("auth.getStatus")) {
            reply(id, { { QStringLiteral("status"), QStringLiteral("authenticated") } });
        } else if (method == QStringLiteral("auth.submit")) {
            replyError(id, 1002, QStringLiteral("wrong password"));
        } else if (method == QStringLiteral("shutdown")) {
            reply(id, { });
            return 0;
        } else {
            replyError(id, -32601, QStringLiteral("no such method: %1").arg(method));
        }
    }
    return 0;
}

} // namespace Tests
