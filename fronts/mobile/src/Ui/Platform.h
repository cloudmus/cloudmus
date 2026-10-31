#pragma once

#include <QObject>
#include <QString>

namespace Ui {

// What the views need from the platform: where the log is, sending it out,
// the device's description for the Diagnostics sheet.
class Platform : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    // Where a file manager finds the log (the shared Download folder on
    // Android); empty when it isn't copied anywhere.
    Q_PROPERTY(QString publicLogDirectory READ publicLogDirectory CONSTANT)
    Q_PROPERTY(QString deviceInfo READ deviceInfo CONSTANT)
    Q_PROPERTY(bool android READ android CONSTANT)

public:
    explicit Platform(QString logDirectory, QObject* parent = nullptr);

    QString logDirectory() const { return logDirectory_; }
    QString publicLogDirectory() const;
    QString deviceInfo() const;
    bool android() const;

    // Packs the log into a zip a file manager can reach; what to tell the
    // user (where it went, or what went wrong).
    Q_INVOKABLE QString exportLogs() const;
    // The window's first frame: startup timing.
    // Also logs how it's drawn (refresh rate, graphics API, render loop).
    Q_INVOKABLE void markFirstFrame(QObject* window) const;
    // Starts the playback service (Android); the core comes up with it.
    static void startService();

private:
    QString logDirectory_;
};

} // namespace Ui
