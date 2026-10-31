#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>

#include <memory>

class QRemoteObjectNode;
class QRemoteObjectDynamicReplica;

namespace App {
class MobileCore;
}

namespace Ui {

// What the views bind to: the core's view models — the objects themselves
// when the core runs in this process, Qt Remote Objects replicas of them
// when it runs in another one (the Android playback service; `--split` on
// the desktop). Both have the same properties and slots, so QML doesn't
// know which it got. Until `ready` the objects are null.
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
    void checkReady();

    std::unique_ptr<QRemoteObjectNode> node_;
    QPointer<QObject> nowPlaying_;
    QPointer<QObject> playlist_;
    QPointer<QObject> settings_;
    QPointer<QAbstractItemModel> tracks_;
    int pending_ = 0;
    bool ready_ = false;
};

} // namespace Ui
