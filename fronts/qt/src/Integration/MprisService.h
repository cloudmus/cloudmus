#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Playback {
class PlaybackController;
}

namespace Integration {

// org.mpris.MediaPlayer2 (root interface) — see MprisPlayerAdaptor below for
// the Player interface. Both are attached to the same MprisService QObject
// and exported together at /org/mpris/MediaPlayer2. This is what makes
// KDE/GNOME route hardware media keys to the app (plus free lock-screen /
// Plasma "Now Playing" integration) — no raw key-grab needed.
class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(QWidget* mainWindow);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool canSetFullscreen() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("cloudmus"); }
    QStringList supportedUriSchemes() const { return { }; }
    QStringList supportedMimeTypes() const { return { }; }

public slots:
    void Quit();
    void Raise();

signals:
    // Not qApp->quit() directly: quitting must go through
    // Ui::MainWindow::quitForReal() so backends get a clean
    // shutdown()/terminate() first (see main.cpp's shutdownAllAndQuit) —
    // same reasoning as TrayIcon::quitRequested.
    void quitRequested();

private:
    QWidget* mainWindow_;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canTrue CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canTrue CONSTANT)
    Q_PROPERTY(bool CanPlay READ canTrue CONSTANT)
    Q_PROPERTY(bool CanPause READ canTrue CONSTANT)
    Q_PROPERTY(bool CanSeek READ canTrue CONSTANT)
    Q_PROPERTY(bool CanControl READ canTrue CONSTANT)

public:
    explicit MprisPlayerAdaptor(Playback::PlaybackController& playback, QObject* parent);

    QString playbackStatus() const;
    QVariantMap metadata() const { return metadata_; }
    double volume() const { return volume_; }
    void setVolume(double v);
    qlonglong position() const;
    bool canTrue() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);

private:
    void emitPropertiesChanged(const QStringList& properties);
    void onTrackChanged();
    void onPlayingChanged(bool playing);

    Playback::PlaybackController& playback_;
    QVariantMap metadata_;
    double volume_ = 1.0;
};

// Owns both adaptors and registers /org/mpris/MediaPlayer2 on the session
// bus as org.mpris.MediaPlayer2.cloudmus.
class MprisService : public QObject {
    Q_OBJECT

public:
    MprisService(QWidget* mainWindow, Playback::PlaybackController& playback, QObject* parent = nullptr);

signals:
    void quitRequested();

private:
    MprisRootAdaptor* root_;
    MprisPlayerAdaptor* player_;
};

} // namespace Integration
