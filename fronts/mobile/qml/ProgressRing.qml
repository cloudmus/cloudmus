import QtQuick

// How far something has got, as a ring: filled clockwise from the top, or a
// spinner while there's nothing to measure it by (`progress` < 0).
Item {
    id: ring

    // 0..1, or -1: unknown.
    property real progress: -1
    property color color: Theme.accent
    property color trackColor: Qt.rgba(color.r, color.g, color.b, 0.25)
    property real lineWidth: Theme.dp(2)
    property bool running: true

    visible: running
    implicitWidth: Theme.dp(28)
    implicitHeight: Theme.dp(28)

    readonly property bool _known: progress >= 0
    // Eased, so that it doesn't jump by a track's share at a time.
    property real _shown: Math.min(1, Math.max(0, progress))
    Behavior on _shown { NumberAnimation { duration: Theme.durationFast } }

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: ring._known
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: ring
            function on_ShownChanged() { canvas.requestPaint() }
            function onColorChanged() { canvas.requestPaint() }
            function onTrackColorChanged() { canvas.requestPaint() }
        }
        onPaint: {
            const context = getContext("2d")
            context.reset()
            const radius = Math.min(width, height) / 2 - ring.lineWidth / 2
            context.lineWidth = ring.lineWidth
            context.strokeStyle = ring.trackColor
            context.beginPath()
            context.arc(width / 2, height / 2, radius, 0, Math.PI * 2)
            context.stroke()
            if (ring._shown <= 0)
                return
            context.strokeStyle = ring.color
            context.lineCap = "round"
            context.beginPath()
            context.arc(width / 2, height / 2, radius, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * ring._shown)
            context.stroke()
        }
    }
    Spinner {
        anchors.fill: parent
        running: ring.running && !ring._known
        color: ring.color
        lineWidth: ring.lineWidth
    }
}
