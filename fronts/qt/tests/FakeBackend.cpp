#include "FakeBackend.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
                { QStringLiteral("radio"), false }, { QStringLiteral("search"), false } } },
        { QStringLiteral("feedback"),
            QJsonObject { { QStringLiteral("like"), false }, { QStringLiteral("dislike"), false },
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
                    QJsonArray { QJsonObject { { QStringLiteral("id"), QStringLiteral("p1") },
                        { QStringLiteral("title"), QStringLiteral("First") }, { QStringLiteral("trackCount"), 2 },
                        { QStringLiteral("kind"), QStringLiteral("playlist") } } } } });
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
