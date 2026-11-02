#pragma once

#include <QObject>
#include <QString>

class QThread;
class QTimer;

namespace Ui {

// What the views need from the platform: where the log is, sending it out,
// the device's description for the Developer tools page.
class Platform : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    // Where a file manager finds the log (the shared Download folder on
    // Android); empty when it isn't copied anywhere.
    Q_PROPERTY(QString publicLogDirectory READ publicLogDirectory CONSTANT)
    Q_PROPERTY(QString deviceInfo READ deviceInfo CONSTANT)
    Q_PROPERTY(bool android READ android CONSTANT)
    // exportSystemApps() under way, and how far: 0..1, -1 while it's still
    // counting what to copy; the package being copied.
    Q_PROPERTY(bool exportingApps READ exportingApps NOTIFY exportProgressChanged)
    Q_PROPERTY(double exportFraction READ exportFraction NOTIFY exportProgressChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportProgressChanged)

public:
    explicit Platform(QString logDirectory, QObject* parent = nullptr);
    ~Platform() override;

    QString logDirectory() const { return logDirectory_; }
    QString publicLogDirectory() const;
    QString deviceInfo() const;
    bool android() const;

    // Packs the log into a zip a file manager can reach; what to tell the
    // user (where it went, or what went wrong).
    Q_INVOKABLE QString exportLogs() const;
    // GWM head units: has Media Center pick this player, which is what puts
    // it in the car's media widget; what to tell the user.
    Q_INVOKABLE QString connectToMediaCenter() const;
    // The head unit's media setup (launcher, media services, the apps the
    // car's widget lists) into the log; what to tell the user.
    Q_INVOKABLE QString logSystemProbe() const;
    // The head unit's system APKs into a zip, for working out its media
    // widget at home (Android). Hundreds of MB: on a worker thread, the
    // progress in exportFraction/exportStatus, the outcome in
    // systemAppsExported().
    Q_INVOKABLE void exportSystemApps();
    bool exportingApps() const { return exportThread_ != nullptr; }
    double exportFraction() const { return exportFraction_; }
    QString exportStatus() const { return exportStatus_; }
    // The window's first frame: startup timing.
    // Also logs how it's drawn (refresh rate, graphics API, render loop).
    Q_INVOKABLE void markFirstFrame(QObject* window) const;
    // Starts the playback service (Android); the core comes up with it.
    static void startService();

signals:
    void exportProgressChanged();
    // What to tell the user: where the zip went, or what went wrong.
    void systemAppsExported(const QString& result);

private:
    void pollExportProgress();

    QString logDirectory_;
    QThread* exportThread_ = nullptr;
    QTimer* exportPoll_ = nullptr;
    double exportFraction_ = -1;
    QString exportStatus_;
};

} // namespace Ui
