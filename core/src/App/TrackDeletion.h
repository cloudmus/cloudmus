#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "Coro.h"
#include "Models.h"

namespace Rpc {
class SourceManager;
}
namespace ViewModel {
class Messages;
class Sources;
}

namespace App {

// Deleting tracks, or a folder with everything in it, from where a source
// keeps them (docs/protocol.md §7.11): its files, and through the platform's
// media library (Rpc::HostMedia) what that keeps. The caller has asked the
// user; this deletes, says how it went, and has the source listed anew.
class TrackDeletion : public QObject {
    Q_OBJECT

public:
    TrackDeletion(Rpc::SourceManager& sourceManager, ViewModel::Sources& sources, ViewModel::Messages& messages,
        QObject* parent = nullptr);

    bool canDelete(const QString& sourceId, const Track& track) const;
    bool canDelete(const QString& sourceId, const Playlist& playlist) const;

    void deleteTracks(const QString& sourceId, const QStringList& trackIds);
    void deletePlaylist(const QString& sourceId, const Playlist& playlist);

signals:
    // Some of a source's tracks are gone: views showing them read them anew.
    void deleted(const QString& sourceId, const QStringList& trackIds);

private:
    bool supports(const QString& sourceId) const;
    Rpc::Task<void> run(QString sourceId, QStringList trackIds, std::optional<Playlist> playlist);

    Rpc::SourceManager& sourceManager_;
    ViewModel::Sources& sources_;
    ViewModel::Messages& messages_;
};

} // namespace App
