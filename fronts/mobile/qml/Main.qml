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
        property real flickMaxVelocity: 12000
        property real flickDeceleration: 2000
        // Read by main() before the window exists (see applyRenderingSettings()).
        property string renderLoop: ""
        // Off: under a finger Qt otherwise merges touch moves into one per
        // frame out of step with the screen, and drags stutter (Pixel 7).
        property bool touchCompression: false
        property bool showFps: false
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

    StackView {
        id: stack
        parent: pages
        objectName: "stack"
        anchors.fill: parent
        // A bar on the side, on a phone on its side: only the bottom is drawn under.
        anchors.topMargin: Theme.insetTop
        anchors.leftMargin: Math.round(window.platform.insetLeft / Screen.devicePixelRatio)
        anchors.rightMargin: Math.round(window.platform.insetRight / Screen.devicePixelRatio)
        visible: window.backend.ready
        initialItem: mainPage
        // Receding as Now Playing comes up, and not drawn at all under it.
        scale: 1 - 0.06 * nowPlayingSheet.progress
        opacity: nowPlayingSheet.progress >= 1 ? 0 : 1 - 0.6 * nowPlayingSheet.progress

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
            else
                close()
        }
        Keys.onBackPressed: (event) => {
            event.accepted = depth > 1 || nowPlayingSheet.shown || trackSheet.visible
            goBack()
        }
        Keys.onEscapePressed: goBack()
        focus: true
    }

    Component {
        id: mainPage
        MainPage {
            objectName: "playlist"
            backend: window.backend
            gesturesEnabled: !trackSheet.visible
            covered: nowPlayingSheet.progress >= 1
            onNowPlayingRequested: nowPlayingSheet.open()
            onNowPlayingPulled: (distance) => nowPlayingSheet.follow(distance / nowPlayingSheet.height)
            onNowPlayingPullEnded: (velocity) => nowPlayingSheet.settleAfterPull(velocity)
            onSettingsRequested: stack.open(settingsPage)
            onLibraryRequested: {
                if (window.backend.library)
                    window.backend.library.selectActiveSource()
                stack.open(libraryPage)
            }
            onTrackMenuRequested: trackSheet.open()
        }
    }
    Component {
        id: libraryPage
        LibraryPage {
            objectName: "library"
            onCloseRequested: stack.close()
            onSettingsRequested: stack.open(settingsPage)
            onPlaylistRequested: stack.open(playlistPage)
            onDownloadsRequested: stack.open(downloadsPage)
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
        id: playlistPage
        PlaylistPage {
            objectName: "playlistScreen"
            onCloseRequested: {
                backend.playlistScreen.close()
                stack.close()
            }
            onTrackMenuRequested: trackSheet.open()
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
    // Now Playing, on a phone: a layer over the pages rather than one of them, as
    // it follows the finger both ways, from the mini player and back down.
    Item {
        id: nowPlayingSheet
        objectName: "nowPlayingSheet"
        parent: pages
        // Over the pages, whichever order they got here in: under it they'd go on
        // taking every press, however see-through.
        z: 1
        anchors.fill: stack
        visible: progress > 0 || settling.running

        // How much of it is up: 0 to 1, so that it stays right as the screen turns.
        property real progress: 0
        readonly property bool shown: progress > 0
        // Kept once made: making it under a finger costs a frame or two.
        property bool wanted: false
        // Far enough, or thrown: the rest of the way. Otherwise back.
        readonly property real settleShare: 0.18
        readonly property real settleVelocity: Theme.dp(600)

        function open() {
            settling.to = 1
            settling.restart()
            // Up already as it's made: its background is only drawn when visible.
            wanted = true
        }
        function close() {
            settling.to = 0
            settling.restart()
        }
        function follow(share) {
            settling.stop()
            progress = Math.max(0, Math.min(1, share))
            wanted = true
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

        NumberAnimation {
            id: settling
            target: nowPlayingSheet
            property: "progress"
            duration: Theme.durationPage
            easing.type: Easing.OutCubic
        }
        Loader {
            width: parent.width
            height: parent.height
            y: parent.height * (1 - parent.progress)
            active: parent.wanted
            sourceComponent: NowPlayingPage {
                objectName: "nowPlaying"
                backend: window.backend
                platform: window.platform
                // Under a track's sheet, a swipe is the sheet's.
                swipeEnabled: !trackSheet.visible
                onCloseRequested: nowPlayingSheet.close()
                onTrackMenuRequested: trackSheet.open()
                onPushed: (distance) => nowPlayingSheet.follow(1 - distance / nowPlayingSheet.height)
                onPushEnded: (velocity) => nowPlayingSheet.settleAfterPush(velocity)
            }
        }
    }

    TrackSheet {
        id: trackSheet
        objectName: "trackSheet"
        behind: pages
        backend: window.backend
    }
    // What the app has to tell the user, over everything.
    Toast {
        id: appToast
        objectName: "appToast"
        parent: Overlay.overlay
        z: 1000
    }
    Connections {
        target: window.backend.mainScreen
        ignoreUnknownSignals: true
        // ViewModel::Messages::Kind: 2 is Error.
        function onMessage(kind, text) { appToast.show(text, kind === 2) }
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
    Component {
        id: settingsPage
        SettingsPage {
            objectName: "settings"
            ui: uiSettings
            onCloseRequested: stack.close()
            onDeveloperRequested: stack.open(developerPage)
            onSourceRequested: (sourceId) => {
                window.backend.sourceSettings.open(sourceId)
                stack.open(sourceSettingsPage)
            }
            onProxyRequested: (proxy) => stack.open(proxyPage, proxy)
            onStopRequested: window.stopAndQuit()
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
