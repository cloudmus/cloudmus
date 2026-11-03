import QtQuick
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
    color: Theme.surface0

    Settings {
        id: uiSettings
        category: "ui"
        property real scale: -1
        property real flickMaxVelocity: 12000
        property real flickDeceleration: 2000
        // Read by main() before the window exists (see applyRenderingSettings()).
        property string renderLoop: ""
        // Off: under a finger Qt otherwise merges touch moves into one per
        // frame out of step with the screen, and drags stutter (Pixel 7).
        property bool touchCompression: false
        property bool showFps: false
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

    // Page transitions differ per page: Now Playing rises from the mini
    // player over a receding playlist, the others slide in from the side.
    property string transitionKind: "rise"

    StackView {
        id: stack
        objectName: "stack"
        anchors.fill: parent
        visible: window.backend.ready
        initialItem: mainPage

        pushEnter: Transition {
            NumberAnimation { property: window.transitionKind === "rise" ? "y" : "x"; from: window.transitionKind === "rise" ? stack.height : stack.width; to: 0; duration: Theme.durationPage; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0.6; to: 1; duration: Theme.durationPage }
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
            NumberAnimation { property: window.transitionKind === "rise" ? "y" : "x"; from: 0; to: window.transitionKind === "rise" ? stack.height : stack.width; duration: Theme.durationPage; easing.type: Easing.InCubic }
        }

        function open(component, kind) {
            window.transitionKind = kind
            push(component, { backend: window.backend, platform: window.platform })
        }
        function close() {
            if (depth > 1) {
                window.transitionKind = currentItem.objectName === "nowPlaying" ? "rise" : "slide"
                pop()
            }
        }

        Keys.onBackPressed: (event) => {
            event.accepted = depth > 1
            close()
        }
        Keys.onEscapePressed: close()
        focus: true
    }

    Component {
        id: mainPage
        MainPage {
            objectName: "playlist"
            backend: window.backend
            onNowPlayingRequested: stack.open(nowPlayingPage, "rise")
            onSettingsRequested: stack.open(settingsPage, "slide")
            onLibraryRequested: stack.open(libraryPage, "slide")
        }
    }
    Component {
        id: libraryPage
        LibraryPage {
            objectName: "library"
            onCloseRequested: stack.close()
            onSettingsRequested: stack.open(settingsPage, "slide")
        }
    }
    // A playlist started from the Library: on to the main screen.
    Connections {
        target: window.backend.library
        ignoreUnknownSignals: true
        function onActivated() {
            window.transitionKind = "slide"
            stack.pop(null)
        }
    }
    Component {
        id: nowPlayingPage
        NowPlayingPage {
            objectName: "nowPlaying"
            onCloseRequested: stack.close()
        }
    }
    Component {
        id: settingsPage
        SettingsPage {
            objectName: "settings"
            ui: uiSettings
            onCloseRequested: stack.close()
            onDeveloperRequested: stack.open(developerPage, "slide")
            onStopRequested: window.stopAndQuit()
        }
    }
    Component {
        id: developerPage
        DeveloperPage {
            objectName: "developer"
            ui: uiSettings
            onCloseRequested: stack.close()
            onLogRequested: stack.open(logPage, "slide")
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

    Component.onCompleted: frameMark.enabled = true
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
