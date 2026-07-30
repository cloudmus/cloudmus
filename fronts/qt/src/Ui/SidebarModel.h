#pragma once

#include <QJsonObject>
#include <QStandardItemModel>
#include <QString>

#include "Models.h"

namespace Ui {

// Two-level tree, source -> category, populated per-source from its
// capabilities (browse.radio/likedTracks/playlists) — same model
// fronts/tui/cloudmus_tui/app.py's sidebar already uses. Built on
// QStandardItemModel rather than a fully custom QAbstractItemModel: the
// data is small and simple enough that hand-rolling one wouldn't earn its
// keep.
class SidebarModel : public QStandardItemModel {
    Q_OBJECT

public:
    // Scoped enum: an unscoped `Playlist` enumerator here would shadow the
    // generated global `::Playlist` struct used right below in this same
    // class scope.
    enum class Kind {
        SourceHeader,
        Wave,
        Liked,
        PlaylistsHeader,
        Playlist,
    };
    enum Role {
        KindRole = Qt::UserRole + 1,
        SourceIdRole,
        PlaylistIdRole,
    };

    explicit SidebarModel(QObject* parent = nullptr);

    void setSource(const QString& sourceId, const QString& sourceName, const QJsonObject& capabilities,
                   const QList<Playlist>& playlists);
    void removeSource(const QString& sourceId);

private:
    QStandardItem* findOrCreateSourceRoot(const QString& sourceId, const QString& sourceName);
};

} // namespace Ui
