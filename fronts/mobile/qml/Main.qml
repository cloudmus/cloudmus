import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtCore
import Cloudmus.Mobile

ApplicationWindow {
    id: window

    required property QtObject backend
    required property QtObject platform
    // Null in the tests that don't switch languages.
    property QtObject localizer: null

    width: 420
    height: 860
    visible: true
    title: "CloudMus"
    color: Theme.windowBackground

    Settings {
        id: uiSettings
        category: "ui"
        property real scale: -1
        // "auto": as the system has it; "light"; "dark".
        property string colorScheme: "auto"
        property real flickMaxVelocity: 20000
        property real flickDeceleration: 1000
        // Read by main() before the window exists (see applyRenderingSettings()).
        property string renderLoop: ""
        // Off: under a finger Qt otherwise merges touch moves into one per
        // frame out of step with the screen, and drags stutter (Pixel 7).
        property bool touchCompression: false
        property bool showFps: false
        property bool dynamicBlur: true
        // A phone's mini player over every page, not just the main one.
        property bool miniPlayerEverywhere: true
    }

    Binding {
        target: Theme
        property: "dark"
        value: uiSettings.colorScheme === "auto" ? Qt.styleHints.colorScheme !== Qt.ColorScheme.Light
                                                 : uiSettings.colorScheme === "dark"
    }
    // Down, not to the nearest: rounded up it left a line of the window's color
    // between the status bar and the page. The bar's opaque over what's under it.
    Binding { target: Theme; property: "insetTop"; value: Math.floor(window.platform.insetTop / Screen.devicePixelRatio) }
    Binding { target: Theme; property: "insetBottom"; value: Math.round(window.platform.insetBottom / Screen.devicePixelRatio) }
    Binding { target: Theme; property: "insetKeyboard"; value: Math.round(window.platform.insetKeyboard / Screen.devicePixelRatio) }
    Binding { target: Theme; property: "insetLeft"; value: Math.round(window.platform.insetLeft / Screen.devicePixelRatio) }
    Binding { target: Theme; property: "insetRight"; value: Math.round(window.platform.insetRight / Screen.devicePixelRatio) }
    function applyAppearance() {
        window.platform.setAppearance(uiSettings.colorScheme, Theme.dark, Theme.surface100.toString(), Theme.windowBackground.toString())
    }
    Connections {
        target: Theme
        function onDarkChanged() { window.applyAppearance() }
    }
    Connections {
        target: uiSettings
        function onColorSchemeChanged() { window.applyAppearance() }
    }
    // A source's Device music was switched on: the system asks once, right
    // then. Settings keeps a row for asking again.
    Connections {
        target: window.backend.settingsScreen
        ignoreUnknownSignals: true
        function onMediaPermissionNeededChanged() {
            if (window.backend.settingsScreen.mediaPermissionNeeded === true)
                window.platform.requestMusicPermission()
        }
        function onMediaDeleteConsentNeeded(uris) { window.platform.requestMediaDelete(uris) }
    }
    Binding { target: Theme; property: "dynamicBlur"; value: uiSettings.dynamicBlur }
    Binding { target: Theme; property: "flickMaxVelocityBase"; value: uiSettings.flickMaxVelocity }
    Binding { target: Theme; property: "flickDecelerationBase"; value: uiSettings.flickDeceleration }

    Binding {
        target: Theme
        property: "scale"
        value: uiSettings.scale > 0 ? uiSettings.scale : Theme.autoScale(window.width, window.height)
    }

    // Stops the player for good and closes the app (on Android the playback
    // service ends too).
    function stopAndQuit() {
        if (backend.nowPlaying)
            backend.nowPlaying.stop()
        Qt.callLater(Qt.quit)
    }

    // Back on the main page closes the window on purpose; anywhere else it's a bug
    // that the log should be able to tell apart.
    onClosing: console.info("the window closes: depth", stack.depth, "now playing", nowPlayingSheet.shown)

    FontLoader { source: "qrc:/fonts/Manrope-Regular.ttf" }
    FontLoader { source: "qrc:/fonts/Manrope-Medium.ttf" }
    FontLoader { source: "qrc:/fonts/Manrope-SemiBold.ttf" }
    FontLoader { source: "qrc:/fonts/Manrope-Bold.ttf" }

    property bool coreSeen: false
    Connections {
        target: window.backend
        function onReadyChanged() { if (window.backend.ready) window.coreSeen = true }
    }

    // All that's under what comes over it, as one: a track's sheet is glass over it.
    Item {
        id: pages
        anchors.fill: parent
    }

    // Under the status bar, which is see-through: a page with its own picture
    // (the generated background) draws it on up there itself, any other has the
    // bar's color. The icons turn light only where the picture is all of the bar's
    // width; on a head unit's main page it's just the track's side, and they stay
    // as the theme has them (black in the light theme: the lesser evil).
    readonly property bool statusOverAura: nowPlayingSheet.progress > 0.5
        || (stack.currentItem !== null && stack.currentItem.auraUnderStatusBar === true
            && stack.currentItem.auraSpansStatusBar === true)
    onStatusOverAuraChanged: platform.setStatusBarOverAura(statusOverAura)
    Rectangle {
        objectName: "statusFill"
        parent: pages
        z: -1
        width: parent.width
        height: Theme.insetTop
        color: Theme.surface100
    }

    StackView {
        id: stack
        parent: pages
        objectName: "stack"
        anchors.fill: parent
        // A bar on the side, on a phone on its side: only the bottom is drawn under.
        anchors.topMargin: Theme.insetTop
        anchors.leftMargin: Theme.insetLeft
        anchors.rightMargin: Theme.insetRight
        visible: window.backend.ready
        initialItem: mainPage

        // Opaque all the way: fading in, a page let the one below show through.
        pushEnter: Transition {
            NumberAnimation { property: "x"; from: stack.width; to: 0; duration: Theme.durationPage; easing.type: Easing.OutCubic }
        }
        pushExit: Transition {
            NumberAnimation { property: "scale"; from: 1; to: 0.94; duration: Theme.durationPage; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 1; to: 0.4; duration: Theme.durationPage }
        }
        popEnter: Transition {
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Theme.durationPage; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0.4; to: 1; duration: Theme.durationPage }
        }
        popExit: Transition {
            NumberAnimation { property: "x"; from: 0; to: stack.width; duration: Theme.durationPage; easing.type: Easing.InCubic }
        }

        function open(component, properties) {
            push(component, Object.assign({ backend: window.backend, platform: window.platform }, properties))
        }
        function close() {
            if (depth > 1)
                pop()
        }

        function goBack() {
            if (trackSheet.visible)
                trackSheet.close()
            else if (nowPlayingSheet.shown)
                nowPlayingSheet.close()
            else if (!(currentItem && currentItem.backInside && currentItem.backInside()))
                close()
        }
        Keys.onBackPressed: (event) => {
            event.accepted = depth > 1 || nowPlayingSheet.shown || trackSheet.visible
            goBack()
        }
        Keys.onEscapePressed: goBack()
        focus: true
        // Receding as Now Playing comes up over a page that's not the main
        // one, which does so itself.
        readonly property real _recede: player.everywhere && depth > 1 ? nowPlayingSheet.progress : 0
        scale: 1 - 0.06 * _recede
        opacity: 1 - 0.6 * _recede
    }

    // A phone's player: the mini player at the bottom and Now Playing grown out
    // of it (NowPlayingPage). Over every page, staying put as they come and go;
    // or, set so, on the main page only, going with it.
    Item {
        id: playerLayer
        parent: pages
        // Over the stack whichever of the two got into `pages` first.
        z: 1
        anchors.fill: stack
        visible: stack.visible
        // Now Playing lets presses through: none reach the page it's over.
        MouseArea {
            anchors.fill: parent
            enabled: player.parent === playerLayer && nowPlayingSheet.progress >= 1
        }
    }
    property Item mainPageItem: null
    Binding { target: Theme; property: "miniPlayerOnPages"; value: player.everywhere && player.visible && stack.depth > 1 }
    // Not made on a head unit: its cover would be decoded for nothing at every
    // change of track.
    Loader {
        id: player
        readonly property bool everywhere: uiSettings.miniPlayerEverywhere
        parent: everywhere || !window.mainPageItem ? playerLayer : window.mainPageItem.playerSlot
        anchors.fill: parent
        active: nowPlayingSheet.narrow
        visible: active && (nowPlayingSheet.shown
            || (window.backend.nowPlaying !== null && window.backend.nowPlaying.hasTrack === true))
        sourceComponent: NowPlayingPage {
            objectName: "nowPlaying"
            backend: window.backend
            platform: window.platform
            progress: nowPlayingSheet.progress
            swipeEnabled: !trackSheet.visible
            onOpenRequested: nowPlayingSheet.open()
            onCloseRequested: nowPlayingSheet.close()
            onTrackMenuRequested: trackSheet.open()
            onDragged: (share) => nowPlayingSheet.follow(share)
            onDragEnded: (velocity, fromOpen) => nowPlayingSheet.settleAfterDrag(velocity, fromOpen)
        }
    }

    Component {
        id: mainPage
        MainPage {
            objectName: "playlist"
            backend: window.backend
            gesturesEnabled: !trackSheet.visible
            platform: window.platform
            nowPlayingProgress: nowPlayingSheet.progress
            onNowPlayingRequested: nowPlayingSheet.open()
            onNowPlayingCloseRequested: nowPlayingSheet.close()
            onNowPlayingDragged: (share) => nowPlayingSheet.follow(share)
            onNowPlayingDragEnded: (velocity, fromOpen) => nowPlayingSheet.settleAfterDrag(velocity, fromOpen)
            onSettingsRequested: stack.open(settingsPage)
            onDownloadsRequested: window.openDownloads()
            onLibraryRequested: {
                if (window.backend.library)
                    window.backend.library.selectActiveSource()
                stack.open(libraryPage)
            }
            onTrackMenuRequested: trackSheet.open()
            Component.onCompleted: window.mainPageItem = this
        }
    }
    Component {
        id: libraryPage
        LibraryPage {
            objectName: "library"
            onCloseRequested: stack.close()
            onSettingsRequested: stack.open(settingsPage)
            onSourceSettingsRequested: {
                window.backend.sourceSettings.open(window.backend.library.currentSourceId)
                stack.open(sourceSettingsPage)
            }
            onPlaylistRequested: stack.open(playlistPage)
            onDownloadsRequested: window.openDownloads()
            onAboutRequested: stack.open(aboutPage)
        }
    }
    Component {
        id: playlistPage
        PlaylistPage {
            objectName: depth === 0 ? "playlistScreen" : "playlistScreen" + depth
            onCloseRequested: stack.close()
            onFolderRequested: (depth) => stack.open(playlistPage, { depth: depth })
            onTrackMenuRequested: trackSheet.open()
            onDownloadsRequested: window.openDownloads()
        }
    }
    // Now Playing's background, ready before it's asked for: drawing one takes a
    // moment, and it would come only once the page had risen. Kept current with
    // the track; a page that opens finds it in the cache. A head unit has the
    // background on screen all along.
    Image {
        objectName: "auraPreload"
        visible: false
        asynchronous: true
        readonly property var _now: window.backend.nowPlaying
        readonly property var _playlist: window.backend.playlist
        // As CrossfadeImage asks for it: the cache goes by that.
        sourceSize: Qt.size(Math.ceil(Math.round(window.width) / Screen.devicePixelRatio),
                            Math.ceil(Math.round(window.height) / Screen.devicePixelRatio))
        source: window.backend.ready && window.width <= window.height * 1.1
            ? Theme.auraUrl(_now && _now.backgroundSeed ? _now.backgroundSeed : _playlist ? _playlist.title : "",
                            Math.round(Theme.dp(45)))
            : ""
    }
    // How far Now Playing is up on a phone: 0 the mini player at the bottom of
    // the main screen .. 1 all of it (NowPlayingPage, on the main page). Kept
    // here, where Back and the status bar look at it; it follows the finger
    // both ways.
    Item {
        id: nowPlayingSheet
        objectName: "nowPlayingSheet"
        readonly property bool narrow: window.width <= window.height * 1.1

        // So that it stays right as the screen turns.
        property real progress: 0
        readonly property bool shown: progress > 0
        // Far enough, or thrown: the rest of the way. Otherwise back.
        readonly property real settleShare: 0.18
        readonly property real settleVelocity: Theme.dp(600)
        // A head unit or a phone on its side has the track on the main screen.
        onNarrowChanged: if (!narrow) { settling.stop(); progress = 0 }

        function open() {
            if (!narrow)
                return
            settling.to = 1
            settling.restart()
        }
        function close() {
            settling.to = 0
            settling.restart()
        }
        function follow(share) {
            settling.stop()
            progress = Math.max(0, Math.min(1, share))
        }
        function settleAfterPull(velocity) {
            if (progress > settleShare || velocity > settleVelocity)
                open()
            else
                close()
        }
        function settleAfterPush(velocity) {
            if (1 - progress > settleShare || velocity > settleVelocity)
                close()
            else
                open()
        }
        // Up a finger's drag, at that speed upwards, from where it was taken.
        function settleAfterDrag(velocity, fromOpen) {
            if (fromOpen)
                settleAfterPush(-velocity)
            else
                settleAfterPull(velocity)
        }

        NumberAnimation {
            id: settling
            target: nowPlayingSheet
            property: "progress"
            duration: Theme.durationPage
            easing.type: Easing.OutCubic
        }
    }

    TrackSheet {
        id: trackSheet
        objectName: "trackSheet"
        behind: pages
        backend: window.backend
        platform: window.platform
    }
    // What the app has to tell the user, over everything.
    Toast {
        id: appToast
        objectName: "appToast"
        parent: Overlay.overlay
        platform: window.platform
        z: 1000
    }
    Connections {
        target: window.backend.mainScreen
        ignoreUnknownSignals: true
        // ViewModel::Messages::Kind: 2 is Error.
        function onMessage(kind, text, details) { appToast.show(text, kind === 2, details) }
    }
    // The services are checked every 30 seconds — only while the app is seen,
    // or plays (ViewModel::ConnectionStatus): the core may be in a service
    // of its own that can't tell.
    readonly property bool appShown: Qt.application.state === Qt.ApplicationActive
    function tellViewShown() {
        if (window.backend.mainScreen !== null)
            window.backend.mainScreen.setViewShown(window.appShown)
    }
    onAppShownChanged: tellViewShown()
    Connections {
        target: window.backend
        ignoreUnknownSignals: true
        function onReadyChanged() { window.tellViewShown() }
    }
    // A playlist started from the Library: on to the main screen.
    Connections {
        target: window.backend.library
        ignoreUnknownSignals: true
        function onActivated() {
            nowPlayingSheet.close()
            stack.pop(null)
        }
    }
    // Audio opened from another app: its track, with nothing over it — Now
    // Playing on a phone, the track's screen on a head unit. Once there's a
    // track to show: on a cold start the core comes up after the window.
    property bool _openedTrackPending: false
    readonly property bool _trackShowable: backend.ready && backend.nowPlaying !== null
        && backend.nowPlaying.hasTrack === true
    on_TrackShowableChanged: showOpenedTrack()
    function showOpenedTrack() {
        if (!_openedTrackPending || !_trackShowable)
            return
        _openedTrackPending = false
        trackSheet.close()
        stack.pop(null)
        stack.currentItem.showTrack()
        nowPlayingSheet.open()
    }
    Connections {
        target: window.platform
        ignoreUnknownSignals: true
        function onAudioOpened() {
            window._openedTrackPending = true
            window.showOpenedTrack()
        }
    }
    // The Downloads page, from the main screen's indicator, Settings, or the
    // notification: on top of where the user is, once.
    function openDownloads() {
        trackSheet.close()
        nowPlayingSheet.close()
        if (stack.currentItem && stack.currentItem.objectName === "downloadsPage")
            return
        stack.open(downloadsPage)
    }
    Connections {
        target: window.platform
        ignoreUnknownSignals: true
        function onDownloadsOpened() {
            if (window.backend.ready)
                window.openDownloads()
            else
                window._downloadsPending = true
        }
    }
    // Tapped before the core was reachable (a cold start from the notification).
    property bool _downloadsPending: false
    Connections {
        target: window.backend
        function onReadyChanged() {
            if (window.backend.ready && window._downloadsPending) {
                window._downloadsPending = false
                window.openDownloads()
            }
        }
    }
    Component {
        id: downloadsPage
        DownloadsPage {
            objectName: "downloadsPage"
            onCloseRequested: stack.close()
        }
    }
    Component {
        id: settingsPage
        SettingsPage {
            objectName: "settings"
            ui: uiSettings
            localizer: window.localizer
            onCloseRequested: stack.close()
            onDeveloperRequested: stack.open(developerPage)
            onDownloadsRequested: window.openDownloads()
            onSourceRequested: (sourceId) => {
                window.backend.sourceSettings.open(sourceId)
                stack.open(sourceSettingsPage)
            }
            onProxyRequested: (proxy) => stack.open(proxyPage, proxy)
        }
    }
    Component {
        id: sourceSettingsPage
        SourceSettingsPage {
            objectName: "sourceSettings"
            onCloseRequested: stack.close()
        }
    }
    Component {
        id: proxyPage
        ProxyPage {
            objectName: "proxy"
            onCloseRequested: stack.close()
        }
    }
    Component {
        id: developerPage
        DeveloperPage {
            objectName: "developer"
            ui: uiSettings
            onCloseRequested: stack.close()
            onLogRequested: stack.open(logPage)
            onStopRequested: window.stopAndQuit()
        }
    }
    Component {
        id: aboutPage
        AboutPage {
            objectName: "aboutPage"
            onCloseRequested: stack.close()
        }
    }
    Component {
        id: logPage
        LogPage {
            objectName: "log"
            onCloseRequested: stack.close()
        }
    }

    // While the core comes up (in the playback service on Android).
    Column {
        anchors.centerIn: parent
        spacing: Theme.padding
        visible: !window.backend.ready
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/icons/logo.svg"
            sourceSize.width: Theme.dp(72)
            sourceSize.height: Theme.dp(72)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            // Gone after having been there: the player's process was ended.
            text: window.coreSeen ? qsTr("Reconnecting to the player…") : qsTr("Starting…")
            color: Theme.inkSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }

    FpsOverlay {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.dp(6)
        z: 100
        visible: uiSettings.showFps
    }

    Component.onCompleted: {
        frameMark.enabled = true
        applyAppearance()
    }
    Connections {
        id: frameMark
        enabled: false
        target: window
        function onFrameSwapped() {
            frameMark.enabled = false
            window.platform.markFirstFrame(window)
        }
    }
}
