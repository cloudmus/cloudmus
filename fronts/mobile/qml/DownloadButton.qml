import QtQuick

// The download icon, ringed by how far a download has got while there's one.
IconButton {
    id: control

    property bool downloading: false
    // 0..1, or -1 while there's nothing to measure it by.
    property real progress: -1
    property color ringColor: Theme.accent

    iconName: "file_download"
    // Smaller inside the ring.
    iconSize: Math.round(size * (downloading ? 0.38 : 0.5))

    ProgressRing {
        anchors.centerIn: parent
        width: Math.round(control.size * 0.66)
        height: width
        running: control.downloading
        progress: control.progress
        color: control.ringColor
        trackColor: Qt.rgba(control.color.r, control.color.g, control.color.b, 0.25)
        lineWidth: Math.max(Theme.dp(2), control.size / 20)
    }
}
