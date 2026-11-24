#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

#include <memory>

class QRemoteObjectNode;
class QRemoteObjectReplica;

namespace App {
class MobileCore;
}

namespace Ui {

// What the views bind to: the core's view models — the objects themselves
// when the core runs in this process, Qt Remote Objects replicas of them
// when it runs in another one (the Android playback service; `--split` on
// the desktop). Both have the same properties and slots, so QML doesn't
// know which it got. Until `ready` the objects are null; a remote core
// that goes away (its process killed) makes it not ready again until the
// core is back, so the views never act on replicas that lead nowhere.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool remote READ remote CONSTANT)
    Q_PROPERTY(QObject* nowPlaying READ nowPlaying NOTIFY readyChanged)
    Q_PROPERTY(QObject* playlist READ playlist NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* tracks READ tracks NOTIFY readyChanged)
    Q_PROPERTY(QObject* settings READ settings NOTIFY readyChanged)
    Q_PROPERTY(QObject* library READ library NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* sources READ sources NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* favorites READ favorites NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* otherPlaylists READ otherPlaylists NOTIFY readyChanged)
    Q_PROPERTY(QObject* mainScreen READ mainScreen NOTIFY readyChanged)
    Q_PROPERTY(QObject* downloadsScreen READ downloadsScreen NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* downloads READ downloads NOTIFY readyChanged)
    Q_PROPERTY(QObject* playlistScreen READ playlistScreen NOTIFY readyChanged)
    // Opens a folder's subfolder at the next level, closes a level's page
    // (ViewModel::PlaylistScreens).
    Q_PROPERTY(QObject* playlistScreens READ playlistScreens NOTIFY readyChanged)
    // Goes up as a deeper level's screen and models come to hand: what
    // playlistScreenAt() and the like give then.
    Q_PROPERTY(int levelsRevision READ levelsRevision NOTIFY levelsRevisionChanged)
    Q_PROPERTY(QAbstractItemModel* openTracks READ openTracks NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* openFolders READ openFolders NOTIFY readyChanged)
    // The subfolders and then the tracks, as one list (ViewModel::PlaylistEntries).
    Q_PROPERTY(QAbstractItemModel* openEntries READ openEntries NOTIFY readyChanged)
    Q_PROPERTY(QObject* trackScreen READ trackScreen NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* trackPlaylists READ trackPlaylists NOTIFY readyChanged)
    Q_PROPERTY(QObject* settingsScreen READ settingsScreen NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* settingsSources READ settingsSources NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* settingsProxies READ settingsProxies NOTIFY readyChanged)
    Q_PROPERTY(QObject* sourceSettings READ sourceSettings NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* sourceConnections READ sourceConnections NOTIFY readyChanged)
    Q_PROPERTY(QAbstractItemModel* sourceFields READ sourceFields NOTIFY readyChanged)
    // What the music is doing (ViewModel::PulseScreen): `level`, `beat`. A
    // view that moves with it holds it while shown (holdPulse()).
    Q_PROPERTY(QObject* pulse READ pulse NOTIFY readyChanged)
    // An update of the app (ViewModel::UpdateScreen).
    Q_PROPERTY(QObject* updateScreen READ updateScreen NOTIFY readyChanged)

public:
#ifndef Q_OS_ANDROID
    // The core in this process.
    explicit Backend(App::MobileCore& core, QObject* parent = nullptr);
#endif
    // The core behind the socket at `socketPath`.
    explicit Backend(const QString& socketPath, QObject* parent = nullptr);
    ~Backend() override;

    // Called while a remote core is out of reach, every few seconds: brings
    // its process up again (Android: starts the playback service).
    void setRestartCore(std::function<void()> restart);

    bool ready() const { return ready_; }
    bool remote() const { return node_ != nullptr; }
    QObject* nowPlaying() const { return ready_ ? nowPlaying_.data() : nullptr; }
    QObject* playlist() const { return ready_ ? playlist_.data() : nullptr; }
    QAbstractItemModel* tracks() const { return ready_ ? tracks_.data() : nullptr; }
    QObject* settings() const { return ready_ ? settings_.data() : nullptr; }
    QObject* library() const { return ready_ ? library_.data() : nullptr; }
    QAbstractItemModel* sources() const { return ready_ ? sources_.data() : nullptr; }
    QAbstractItemModel* favorites() const { return ready_ ? favorites_.data() : nullptr; }
    QAbstractItemModel* otherPlaylists() const { return ready_ ? otherPlaylists_.data() : nullptr; }
    QObject* downloadsScreen() const { return ready_ ? downloadsScreen_.data() : nullptr; }
    QAbstractItemModel* downloads() const { return ready_ ? downloads_.data() : nullptr; }
    QObject* mainScreen() const { return ready_ ? mainScreen_.data() : nullptr; }
    QObject* playlistScreen() const { return ready_ ? playlistScreen_.data() : nullptr; }
    QObject* playlistScreens() const { return ready_ ? playlistScreens_.data() : nullptr; }
    int levelsRevision() const { return levelsRevision_; }
    // The Playlist page's at `depth` of the stack (0: the one the Library
    // opens), and its rows and subfolders; null until they're to hand
    // (levelsRevision). prepareLevel() asks for them ahead.
    Q_INVOKABLE QObject* playlistScreenAt(int depth);
    Q_INVOKABLE QAbstractItemModel* openTracksAt(int depth);
    Q_INVOKABLE QAbstractItemModel* openFoldersAt(int depth);
    Q_INVOKABLE QAbstractItemModel* openEntriesAt(int depth);
    Q_INVOKABLE void prepareLevel(int depth);
    QAbstractItemModel* openTracks() const { return ready_ ? openTracks_.data() : nullptr; }
    QAbstractItemModel* openFolders() const { return ready_ ? openFolders_.data() : nullptr; }
    QAbstractItemModel* openEntries() const { return ready_ ? openEntries_.data() : nullptr; }
    QObject* trackScreen() const { return ready_ ? trackScreen_.data() : nullptr; }
    QAbstractItemModel* trackPlaylists() const { return ready_ ? trackPlaylists_.data() : nullptr; }
    QObject* settingsScreen() const { return ready_ ? settingsScreen_.data() : nullptr; }
    QAbstractItemModel* settingsSources() const { return ready_ ? settingsSources_.data() : nullptr; }
    QAbstractItemModel* settingsProxies() const { return ready_ ? settingsProxies_.data() : nullptr; }
    QObject* sourceSettings() const { return ready_ ? sourceSettings_.data() : nullptr; }
    QAbstractItemModel* sourceConnections() const { return ready_ ? sourceConnections_.data() : nullptr; }
    QAbstractItemModel* sourceFields() const { return ready_ ? sourceFields_.data() : nullptr; }
    QObject* pulse() const { return ready_ ? pulse_.data() : nullptr; }
    QObject* updateScreen() const { return ready_ ? updateScreen_.data() : nullptr; }
    // A view that shows the pulse holds it while shown, and lets it go: the
    // core analyzes the music only while any view holds it. Counted here, so
    // views come and go as they like; the core is told again when it's back.
    Q_INVOKABLE void holdPulse();
    Q_INVOKABLE void releasePulse();

signals:
    void readyChanged();
    void levelsRevisionChanged();

private:
    // A level of the Playlist pages past the first.
    struct Level {
        QPointer<QObject> screen;
        QPointer<QAbstractItemModel> tracks;
        QPointer<QAbstractItemModel> folders;
        QPointer<QAbstractItemModel> entries;
        bool screenValid = false;
        int modelsInitialized = 0;
        bool ready() const { return screenValid && modelsInitialized >= 3; }
    };
    // Acquired (remote) on first use; null depth 0 or past the levels.
    Level* level(int depth);
    void acquire(QPointer<QObject>& slot, const QString& name);
    void acquireModel(QPointer<QAbstractItemModel>& slot, const QString& name);
    void updateReady();
    void tellPulseWatching();

    std::unique_ptr<QRemoteObjectNode> node_;
    QPointer<QObject> nowPlaying_;
    QPointer<QObject> playlist_;
    QPointer<QObject> settings_;
    QPointer<QObject> library_;
    QPointer<QObject> mainScreen_;
    QPointer<QObject> downloadsScreen_;
    QPointer<QObject> playlistScreen_;
    QPointer<QObject> playlistScreens_;
    QHash<int, Level> levels_;
    int levelsRevision_ = 0;
#ifndef Q_OS_ANDROID
    App::MobileCore* core_ = nullptr;
#endif
    QPointer<QObject> trackScreen_;
    QPointer<QObject> settingsScreen_;
    QPointer<QObject> sourceSettings_;
    QPointer<QObject> pulse_;
    QPointer<QObject> updateScreen_;
    int pulseHolders_ = 0;
    QPointer<QAbstractItemModel> tracks_;
    QPointer<QAbstractItemModel> sources_;
    QPointer<QAbstractItemModel> favorites_;
    QPointer<QAbstractItemModel> otherPlaylists_;
    QPointer<QAbstractItemModel> downloads_;
    QPointer<QAbstractItemModel> openTracks_;
    QPointer<QAbstractItemModel> openFolders_;
    QPointer<QAbstractItemModel> openEntries_;
    QPointer<QAbstractItemModel> trackPlaylists_;
    QPointer<QAbstractItemModel> settingsSources_;
    QPointer<QAbstractItemModel> settingsProxies_;
    QPointer<QAbstractItemModel> sourceConnections_;
    QPointer<QAbstractItemModel> sourceFields_;
    QList<QRemoteObjectReplica*> replicas_;
    int modelsAcquired_ = 0;
    int modelsInitialized_ = 0;
    bool ready_ = false;
    bool everReady_ = false;
    std::function<void()> restartCore_;
    QTimer restartTimer_;
};

} // namespace Ui
