#pragma once

#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

#include "Coro.h"
#include "Models.h"
#include "Settings.h"

namespace Rpc {
class SourceManager;
}

namespace ViewModel {

// What can be set about a source and the proxies sources go through, whichever
// view shows it: when each is applied is the view's (a dialog's Apply, a phone's
// at once), and so are restarts, told here rather than done.
class SourceSettings : public QObject {
    Q_OBJECT

public:
    SourceSettings(Rpc::SourceManager& sourceManager, Config::Settings& settings, QObject* parent = nullptr);

    // What the source says it has, while running; until then what's safe to
    // show: a source that hasn't started may well use the network.
    struct Facts {
        bool running = false;
        bool usesNetwork = true;
        bool hasRadio = false;
        // Its streams are downloaded here: not a source's own files.
        bool downloadsStreams = false;
        bool hasSettings = false;
        bool needsAuth = false;
        QString description;
    };
    Facts facts(const QString& sourceId) const;

    // Keeps the ids of backends not installed right now: reinstalling one
    // shouldn't silently switch it back on.
    void setEnabled(const QString& sourceId, bool enabled);
    // Whether a restart is due: its environment is set when it starts.
    bool setConnection(const QString& sourceId, const QString& connection);

    // The source's own settings (docs/protocol.md §7.7). Throws what the call
    // throws; without a running source, std::runtime_error.
    Rpc::Task<SettingsDescription> describe(QString sourceId);
    // What the source turned down: `key` names the field, if one.
    struct UpdateFailure {
        QString key;
        QString message;
    };
    Rpc::Task<std::optional<UpdateFailure>> update(QString sourceId, QMap<QString, QJsonValue> values);
    // Whether changing these takes the source's restart.
    static bool needRestart(const QList<SettingField>& fields, const QList<QString>& keys);
    // Each field's value as it stands, or its default if it reported nothing.
    static QJsonValue initialValue(const SettingField& field);

    // Saves the user's proxies. What's returned goes through one that changed
    // or went away, and has to restart for that to count.
    QSet<QString> saveProxies(const QList<Config::ProxyConfig>& proxies);
    // One request through a proxy, saved or not: `done` gets what went wrong,
    // or an empty string. Goes with `context`.
    static void checkProxy(
        const Config::ProxyConfig& proxy, QObject* context, std::function<void(const QString& error)> done);

private:
    Rpc::SourceManager& sourceManager_;
    Config::Settings& settings_;
};

} // namespace ViewModel
