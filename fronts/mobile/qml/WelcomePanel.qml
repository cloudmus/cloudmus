import QtQuick

// The main screen with nothing to play yet: the logo and the clouds flying out
// of it, as on the About page, and the way to the Library. One short line: the
// first launch's slides (OnboardingPage) have told the rest.
Item {
    id: panel

    property QtObject backend
    signal libraryRequested()

    readonly property QtObject pulse: backend ? backend.pulse : null

    // The music is analyzed only while the panel is in view, the app in front
    // (as AboutPage): the logo's own sound moves the clouds even with none.
    readonly property bool watching: visible && Qt.application.state === Qt.ApplicationActive
    property bool _holding: false
    function _hold(on) {
        if (on === _holding || !backend)
            return
        _holding = on
        if (on)
            backend.holdPulse()
        else
            backend.releasePulse()
    }
    onWatchingChanged: _hold(watching)
    Component.onCompleted: _hold(watching)
    Component.onDestruction: _hold(false)

    CloudsBackdrop {
        id: backdrop
        objectName: "welcomeClouds"
        anchors.fill: parent
        origin: logo
        pulse: panel.pulse
        running: panel.watching
        onStepped: (dtMs) => logo.step(dtMs)
        lean: finger.active ? backdrop.mapFromItem(null, finger.point.scenePosition) : undefined
    }

    Column {
        id: content
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.padding * 2, Theme.dp(360))
        spacing: Theme.padding

        PulsingLogo {
            id: logo
            objectName: "welcomeLogo"
            anchors.horizontalCenter: parent.horizontalCenter
            width: side
            height: side
            side: Theme.dp(88)
            pulse: panel.pulse
        }
        Text {
            objectName: "welcomeText"
            width: parent.width
            topPadding: Theme.spacing
            text: qsTr("Pick your music in the Library")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }
        TextButton {
            objectName: "welcomeLibraryButton"
            anchors.horizontalCenter: parent.horizontalCenter
            height: Theme.touchLarge
            width: Math.max(Theme.dp(200), implicitWidth + Theme.dp(16))
            tint: Theme.accent
            text: qsTr("Open the Library")
            onClicked: panel.libraryRequested()
        }
    }

    // Taking nothing: only to know where a finger is, for the clouds to lean to it.
    Item {
        anchors.fill: parent
        PointHandler {
            id: finger
            acceptedButtons: Qt.LeftButton
        }
    }
}
