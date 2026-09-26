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
            QJsonObject { { QStringLiteral("playlists"), true }, { QStringLiteral("likedTracks"), false },
                { QStringLiteral("radio"), false }, { QStringLiteral("search"), false },
                { QStringLiteral("editPlaylists"), true } } },
        { QStringLiteral("feedback"),
            QJsonObject { { QStringLiteral("like"), true }, { QStringLiteral("dislike"), true },
                { QStringLiteral("skip"), false } } },
        { QStringLiteral("download"), false },
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
            reply(id, { { QStringLiteral("accepted"), true } });
        } else if (method.startsWith(QStringLiteral("feedback."))) {
            // A track id of "fail" makes any feedback call fail.
            const QString trackId
                = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("trackId")).toString();
            if (trackId == QStringLiteral("fail"))
                replyError(id, 1200, QStringLiteral("service down"));
            else
                reply(id, { });
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
