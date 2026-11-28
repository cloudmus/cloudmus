#pragma once

#include <QObject>
#include <QRectF>
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
    // There's a browser to sign in to a source in (signInWithBrowser()).
    Q_PROPERTY(bool hasSignInBrowser READ hasSignInBrowser CONSTANT)
    // That browser can be sent through a proxy (an old WebView can't).
    Q_PROPERTY(bool signInBrowserTakesProxy READ signInBrowserTakesProxy CONSTANT)
    // ...and is old enough that sign-in pages are likely to turn it away.
    Q_PROPERTY(bool signInBrowserOutdated READ signInBrowserOutdated CONSTANT)
    // CloudMus Sign-in (signInHelper()).
    Q_PROPERTY(QString signInHelper READ signInHelper NOTIFY signInHelperChanged)
    Q_PROPERTY(bool canInstallSignInHelper READ canInstallSignInHelper CONSTANT)
    // This device can have a phone sign in for it (startPhoneSignIn()).
    Q_PROPERTY(bool canSignInByPhone READ canSignInByPhone CONSTANT)
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
    // The system's music volume and its top, in the system's own steps (0 and 0
    // where there's no way to tell).
    Q_PROPERTY(int systemVolume READ systemVolume NOTIFY systemVolumeChanged)
    Q_PROPERTY(int systemVolumeMax READ systemVolumeMax NOTIFY systemVolumeChanged)
    // exportSystemApps() under way, and how far: 0..1, -1 while it's still
    // counting what to copy; the package being copied.
    Q_PROPERTY(bool exportingApps READ exportingApps NOTIFY exportProgressChanged)
    Q_PROPERTY(double exportFraction READ exportFraction NOTIFY exportProgressChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportProgressChanged)
    // How this install of the app is updated (ViewModel::UpdateScreen's Via):
    // "app" — it downloads and installs the APK itself (the GitHub build);
    // "play" / "rustore" — through that store, the one it came from (the
    // store build); "page" — the user, from the release page (the store
    // build from elsewhere, or not Android).
    Q_PROPERTY(QString updateChannel READ updateChannel CONSTANT)
    // Developer tools' "Serve updates" (UpdateServer.java): empty while off;
    // else the addresses to give the other device, a line each, and the
    // version offered on the last line.
    Q_PROPERTY(QString updateServerStatus READ updateServerStatus NOTIFY updateServerChanged)
    // A store's update under way (startStoreUpdate()): 0..1, -1 not known.
    Q_PROPERTY(double storeUpdateProgress READ storeUpdateProgress NOTIFY storeUpdateChanged)

public:
    explicit Platform(QString logDirectory, QObject* parent = nullptr);
    ~Platform() override;

    QString logDirectory() const { return logDirectory_; }
    QString publicLogDirectory() const;
    QString deviceInfo() const;
    bool android() const;
    bool hasSignInBrowser() const;
    bool signInBrowserTakesProxy() const;
    bool signInBrowserOutdated() const;
    bool canSignInByPhone() const;
    // Offers a source's `browser` method to a phone on the same network
    // (PhoneSignInServer): the addresses for the phone to open, one per
    // network of this device's, the likeliest first — to be shown as a QR
    // code, one at a time — or none with no network to offer it on. What the phone's
    // browser ends up with comes in signedInWithBrowser(), as for
    // signInWithBrowser(). Until stopPhoneSignIn(), or
    // phoneSignInEnded() once it's expired.
    Q_INVOKABLE QStringList startPhoneSignIn(const QString& request, const QString& method, const QString& title) const;
    Q_INVOKABLE void stopPhoneSignIn() const;
    // Opens a source's `browser` sign-in method (`method`, as JSON) in the
    // built-in browser (SignInWebActivity); what it ends up with comes in
    // signedInWithBrowser(), with `request` passed back. `title` is shown
    // over it.
    Q_INVOKABLE void signInWithBrowser(const QString& request, const QString& method, const QString& title) const;
    // CloudMus Sign-in, a helper app with a modern browser, for a device whose
    // own is too old (SignInHelper): "installed", "foreign" (a build signed
    // with another key) or "". Looked at again by refreshSignInHelper(), and
    // whenever the app comes to the front: the user may install it by hand.
    QString signInHelper() const { return signInHelper_; }
    // This build may install it itself.
    bool canInstallSignInHelper() const;
    Q_INVOKABLE void refreshSignInHelper();
    // The downloaded APK, to Android's installer (after
    // requestInstallPermission()); signInHelperInstallFinished() after.
    Q_INVOKABLE void installSignInHelper(const QString& path) const;
    // Android asks the user, then removes it; signInHelper changes once the
    // app is back in front.
    Q_INVOKABLE void uninstallSignInHelper() const;
    // As signInWithBrowser(), in the helper.
    Q_INVOKABLE void signInWithHelper(const QString& request, const QString& method, const QString& title) const;
    QString version() const;
    QString licenseText() const;
    QVariantList authors() const;
    QVariantList contributors() const;
    int insetTop() const { return insetTop_; }
    int insetLeft() const { return insetLeft_; }
    int insetRight() const { return insetRight_; }
    int insetBottom() const { return insetBottom_; }
    int insetKeyboard() const { return insetKeyboard_; }
    int systemVolume() const { return volume_; }
    int systemVolumeMax() const { return volumeMax_; }
    Q_INVOKABLE void setSystemVolume(int index);
    // Asks the system again: its own keys and knobs change it too.
    Q_INVOKABLE void readVolume();
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
    // While on, the screen doesn't go dark and is at its brightest: for
    // something another device's camera reads (QrCode.qml).
    Q_INVOKABLE void setScreenAwake(bool on) const;
    // The head unit's media setup (launcher, media services, the apps the
    // car's widget lists) into the log; what to tell the user.
    Q_INVOKABLE QString logSystemProbe() const;
    Q_INVOKABLE QString probeMusicStorage() const;
    // The system's question for reading the device's music, which a source
    // asked for (ViewModel::SettingsScreen::mediaPermissionNeeded).
    Q_INVOKABLE void requestMusicPermission();
    // A source's path setting from the system's picker, a folder or a file:
    // the answer in pathPicked(). Android only.
    Q_INVOKABLE void pickPath(bool directory) const;
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
    QString updateChannel() const;
    double storeUpdateProgress() const { return storeUpdateProgress_; }
    // Android lets this app install apps ("Install unknown apps"); asked
    // again each time: the user changes it in Android's settings.
    Q_INVOKABLE bool canInstallPackages() const;
    // That setting's screen.
    Q_INVOKABLE void requestInstallPermission() const;
    // The downloaded APK to Android's package installer; the outcome, unless
    // the app is replaced, in updateInstallFinished(). `playing`: the music
    // goes on in the new version. The texts are the notification's that
    // says it's updated (Android 10+, which doesn't let it open by itself).
    Q_INVOKABLE void installUpdate(const QString& path, bool playing, const QString& noticeChannel,
        const QString& noticeTitle, const QString& noticeText) const;
    // The store's own check, its answer in storeUpdateChecked().
    Q_INVOKABLE void checkStoreUpdate(bool manual) const;
    // The store's update flow; how it goes in storeUpdateProgress and
    // storeUpdateEnded().
    Q_INVOKABLE void startStoreUpdate();
    QString updateServerStatus() const { return updateServerStatus_; }
    Q_INVOKABLE void startUpdateServer();
    Q_INVOKABLE void stopUpdateServer();
    // The APK to offer, from the system's file picker; this app's own until one is.
    Q_INVOKABLE void chooseServedApk() const;
    void readUpdateServer();
    // From Android's thread.
    void takeInstallOutcome(bool cancelled, int installerStatus, const QString& message);
    void takeStoreState(int state, qint64 done, qint64 total, const QString& error);
    // Whether Python and the backends run on this device, into the log, the
    // outcome in pythonProbeFinished().
    Q_INVOKABLE void runPythonProbe();
    // The window's first frame: startup timing.
    // Also logs how it's drawn (refresh rate, graphics API, render loop).
    Q_INVOKABLE void markFirstFrame(QObject* window) const;
    // Where Android's start screen has its logo, in the window's pixels (not
    // device-independent): StartSplash.qml draws its own there. Empty without
    // one (before Android 12, off Android, or it's gone).
    Q_INVOKABLE QRectF splashIcon() const;
    // StartSplash.qml has drawn the same: Android's start screen goes.
    Q_INVOKABLE void hideSplash() const;
    // Starts the playback service (Android); the core comes up with it.
    static void startService();
    // Ends the process at once, the log written (Android).
    static void exitNow(int code);
    // Keeps Qt's accessibility from the screen unless a screen reader needs it
    // (Android): see the definition.
    static void limitAccessibility();

signals:
    void insetsChanged();
    void systemVolumeChanged();
    void exportProgressChanged();
    // What to tell the user: where the zip went, or what went wrong.
    void systemAppsExported(const QString& result);
    void pythonProbeFinished(const QString& result);
    // signInWithBrowser()'s answer: auth.submit's `browser` as JSON, or
    // empty if the user left.
    void signedInWithBrowser(const QString& request, const QString& result);
    void signInHelperChanged();
    // installSignInHelper() is over: `error` empty if installed, or turned
    // down by the user.
    void signInHelperInstallFinished(const QString& error);
    // startPhoneSignIn()'s offer expired unused.
    void phoneSignInEnded();
    // Another app had audio opened here ("Open with CloudMus"): on to the
    // track. The audio itself goes to the core, which plays it.
    void audioOpened();
    // The downloads notification was tapped: on to the Downloads page.
    void downloadsOpened();
    // installUpdate() didn't replace the app: the user turned the system's
    // question down (`cancelled`), or `error` says why it failed.
    void updateInstallFinished(bool cancelled, const QString& error);
    // checkStoreUpdate()'s answer: the store's versionCode (0: nothing newer),
    // or what went wrong.
    void storeUpdateChecked(int versionCode, const QString& error, bool manual);
    void storeUpdateChanged();
    void updateServerChanged();
    // pickPath()'s answer: the path, or empty for what isn't on the device's
    // storage (a cloud's document); `cancelled` when nothing was picked.
    void pathPicked(const QString& path, bool cancelled);
    // The store's flow is over without the app restarting: left
    // (`cancelled`) or failed.
    void storeUpdateEnded(bool cancelled, const QString& error);

private:
    void pollExportProgress();

    QString logDirectory_;
    QString signInHelper_;
    int insetTop_ = 0;
    int insetLeft_ = 0;
    int insetRight_ = 0;
    int insetBottom_ = 0;
    int insetKeyboard_ = 0;
    int volume_ = 0;
    int volumeMax_ = 0;
    QThread* exportThread_ = nullptr;
    QTimer* exportPoll_ = nullptr;
    double exportFraction_ = -1;
    QString exportStatus_;
    double storeUpdateProgress_ = -1;
    QString updateServerStatus_;
};

} // namespace Ui
