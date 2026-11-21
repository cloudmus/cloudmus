import QtQuick

// Something's on its way: a thin arc going round.
Item {
    id: spinner

    property color color: Theme.accent
    property real lineWidth: Theme.dp(3)
    property bool running: true

    visible: running
    implicitWidth: Theme.dp(28)
    implicitHeight: Theme.dp(28)

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        Connections {
            target: spinner
            function onColorChanged() { canvas.requestPaint() }
        }
        onHeightChanged: requestPaint()
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = spinner.color
            context.lineWidth = spinner.lineWidth
            context.lineCap = "round"
            context.beginPath()
            const radius = Math.min(width, height) / 2 - spinner.lineWidth
            context.arc(width / 2, height / 2, radius, 0, Math.PI * 1.4)
            context.stroke()
        }
        RotationAnimator on rotation {
            running: spinner.running && spinner.visible
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }
}
