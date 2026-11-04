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
    // What the system's bars cover of the window, in pixels.
    Q_PROPERTY(int insetTop READ insetTop NOTIFY insetsChanged)
    Q_PROPERTY(int insetLeft READ insetLeft NOTIFY insetsChanged)
    Q_PROPERTY(int insetRight READ insetRight NOTIFY insetsChanged)
    Q_PROPERTY(int insetBottom READ insetBottom NOTIFY insetsChanged)
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
    int insetTop() const { return insetTop_; }
    int insetLeft() const { return insetLeft_; }
    int insetRight() const { return insetRight_; }
    int insetBottom() const { return insetBottom_; }
    // The theme for what's the system's to draw: the bars, the first frame of a
    // start. `scheme`: "auto", "light" or "dark"; `dark`: what that comes to.
    Q_INVOKABLE void setAppearance(
        const QString& scheme, bool dark, const QString& barColor, const QString& backColor) const;
    // Android told of new insets: from its thread.
    void readInsets();

    // Packs the log into a zip a file manager can reach; what to tell the
    // user (where it went, or what went wrong).
    Q_INVOKABLE QString exportLogs() const;
    Q_INVOKABLE void copyText(const QString& text) const;
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
    // Whether Python and the backends run on this device, into the log, the
    // outcome in pythonProbeFinished().
    Q_INVOKABLE void runPythonProbe();
    // The window's first frame: startup timing.
    // Also logs how it's drawn (refresh rate, graphics API, render loop).
    Q_INVOKABLE void markFirstFrame(QObject* window) const;
    // Starts the playback service (Android); the core comes up with it.
    static void startService();
    // Ends the process at once, the log written (Android).
    static void exitNow(int code);
    // Keeps Qt's accessibility from the screen unless a screen reader needs it
    // (Android): see the definition.
    static void limitAccessibility();

signals:
    void insetsChanged();
    void exportProgressChanged();
    // What to tell the user: where the zip went, or what went wrong.
    void systemAppsExported(const QString& result);
    void pythonProbeFinished(const QString& result);

private:
    void pollExportProgress();

    QString logDirectory_;
    int insetTop_ = 0;
    int insetLeft_ = 0;
    int insetRight_ = 0;
    int insetBottom_ = 0;
    QThread* exportThread_ = nullptr;
    QTimer* exportPoll_ = nullptr;
    double exportFraction_ = -1;
    QString exportStatus_;
};

} // namespace Ui
