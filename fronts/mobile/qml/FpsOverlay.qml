import QtQuick

// Frames the window actually drew in the last second, in a corner: 0 when
// nothing moves, the screen's rate while something does. It only counts,
// never asks for a frame — besides its own once-a-second redraw.
Rectangle {
    id: overlay

    readonly property Window window: Window.window
    property int frames: 0
    property int fps: 0

    width: label.implicitWidth + Theme.dp(12)
    height: label.implicitHeight + Theme.dp(6)
    radius: Theme.dp(4)
    color: "#B0000000"

    Text {
        id: label
        anchors.centerIn: parent
        text: overlay.fps + " fps"
        color: overlay.fps >= 55 ? "#7CFC00" : overlay.fps >= 40 ? "#FFD700" : "#FF6347"
        font.family: Theme.monoFamily
        font.pixelSize: Theme.dp(13)
    }

    Connections {
        target: overlay.window
        function onFrameSwapped() { overlay.frames++ }
    }
    Timer {
        interval: 1000
        repeat: true
        running: overlay.visible
        onTriggered: {
            overlay.fps = overlay.frames
            overlay.frames = 0
        }
    }
}
