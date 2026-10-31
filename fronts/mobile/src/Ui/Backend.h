#pragma once

#include <QAbstractItemModel>
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

signals:
    void readyChanged();

private:
    void acquire(QPointer<QObject>& slot, const QString& name);
    void updateReady();

    std::unique_ptr<QRemoteObjectNode> node_;
    QPointer<QObject> nowPlaying_;
    QPointer<QObject> playlist_;
    QPointer<QObject> settings_;
    QPointer<QAbstractItemModel> tracks_;
    QList<QRemoteObjectReplica*> replicas_;
    bool tracksInitialized_ = false;
    bool ready_ = false;
    bool everReady_ = false;
    std::function<void()> restartCore_;
    QTimer restartTimer_;
};

} // namespace Ui
