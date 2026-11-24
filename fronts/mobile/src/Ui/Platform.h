#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QThread;
class QTimer;

namespace Ui {

// What the views need from the platform: where the log is, sending it out,
// the device's description for the Developer tools page, the app's version
// and license for the About page.
class Platform : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    // Where a file manager finds the log (the shared Download folder on
    // Android); empty when it isn't copied anywhere.
    Q_PROPERTY(QString publicLogDirectory READ publicLogDirectory CONSTANT)
    Q_PROPERTY(QString deviceInfo READ deviceInfo CONSTANT)
    Q_PROPERTY(bool android READ android CONSTANT)
    // The app's version, from the release tags: "0.6.4", "0.6.4-163-geaef2c2".
    Q_PROPERTY(QString version READ version CONSTANT)
    // LICENSE, a paragraph to a line, for the About page.
    Q_PROPERTY(QString licenseText READ licenseText CONSTANT)
    // Who makes it (About::authors()) and who helped (About::contributors()):
    // each a map of name, email, url and shownUrl.
    Q_PROPERTY(QVariantList authors READ authors CONSTANT)
    Q_PROPERTY(QVariantList contributors READ contributors CONSTANT)
    // What the system's bars cover of the window, in pixels.
    Q_PROPERTY(int insetTop READ insetTop NOTIFY insetsChanged)
    Q_PROPERTY(int insetLeft READ insetLeft NOTIFY insetsChanged)
    Q_PROPERTY(int insetRight READ insetRight NOTIFY insetsChanged)
    Q_PROPERTY(int insetBottom READ insetBottom NOTIFY insetsChanged)
    // What the on-screen keyboard covers from the bottom of the window, the
    // navigation bar included; 0 with it hidden.
    Q_PROPERTY(int insetKeyboard READ insetKeyboard NOTIFY insetsChanged)
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
    QString version() const;
    QString licenseText() const;
    QVariantList authors() const;
    QVariantList contributors() const;
    int insetTop() const { return insetTop_; }
    int insetLeft() const { return insetLeft_; }
    int insetRight() const { return insetRight_; }
    int insetBottom() const { return insetBottom_; }
    int insetKeyboard() const { return insetKeyboard_; }
    // The theme for what's the system's to draw: the bars, the first frame of a
    // start. `scheme`: "auto", "light" or "dark"; `dark`: what that comes to.
    Q_INVOKABLE void setAppearance(
        const QString& scheme, bool dark, const QString& barColor, const QString& backColor) const;
    // A page's own picture is under the status bar: its icons go light, whatever
    // the theme.
    Q_INVOKABLE void setStatusBarOverAura(bool over) const;
    // Android told of new insets: from its thread.
    void readInsets();
    // Audio opened from another app, or the downloads notification tapped,
    // which Android may have told before Qt was up: audioOpened() and
    // downloadsOpened() for them.
    void takeOpenRequests();

    // Packs the log into a zip a file manager can reach; what to tell the
    // user (where it went, or what went wrong).
    Q_INVOKABLE QString exportLogs() const;
    Q_INVOKABLE void copyText(const QString& text) const;
    // The head unit's media setup (launcher, media services, the apps the
    // car's widget lists) into the log; what to tell the user.
    Q_INVOKABLE QString logSystemProbe() const;
    Q_INVOKABLE QString probeMusicStorage() const;
    // The system's question for reading the device's music, which a source
    // asked for (ViewModel::SettingsScreen::mediaPermissionNeeded).
    Q_INVOKABLE void requestMusicPermission();
    // The system's question for deleting other apps' tracks.
    Q_INVOKABLE void requestMediaDelete(const QStringList& uris);
    Q_INVOKABLE void grantMusicStorage() const;
    // The system's question for posting notifications (Android 13+), for the
    // one of what's being saved; asked when a download starts.
    Q_INVOKABLE void requestNotificationPermission() const;
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
    // Another app had audio opened here ("Open with CloudMus"): on to the
    // track. The audio itself goes to the core, which plays it.
    void audioOpened();
    // The downloads notification was tapped: on to the Downloads page.
    void downloadsOpened();

private:
    void pollExportProgress();

    QString logDirectory_;
    int insetTop_ = 0;
    int insetLeft_ = 0;
    int insetRight_ = 0;
    int insetBottom_ = 0;
    int insetKeyboard_ = 0;
    QThread* exportThread_ = nullptr;
    QTimer* exportPoll_ = nullptr;
    double exportFraction_ = -1;
    QString exportStatus_;
};

} // namespace Ui
