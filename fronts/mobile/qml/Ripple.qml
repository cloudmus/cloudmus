import QtQuick

// The touch feedback of every button and menu entry, in place of a flat
// highlight: a wave of faint twinkling "stars" from where the finger came
// down, soft-edged and lit at its front (shaders/ripple.frag), as Android 12
// does. Put it in the control's background, filling it, and bind `pressed`,
// `pressX`/`pressY` and the shape's `radius`. Without a GPU a shader shows
// nothing: `flat` tells the control to keep its own highlight then.
Item {
    id: ripple

    property bool pressed: false
    property real pressX: width / 2
    property real pressY: height / 2
    property real radius: 0
    property color color: Theme.ink
    // How long the wave takes: out under the finger, out the rest of the way
    // once let go, and fading after a pause.
    property int spreadDuration: 900
    property int releaseDuration: 220
    property int fadeDelay: 90
    property int fadeDuration: 380
    // How far the wave goes, if not past the control's farthest corner: for a
    // control that grows meanwhile, as far as it will have grown.
    property real reach: 0
    readonly property bool flat: GraphicsInfo.api === GraphicsInfo.Software || GraphicsInfo.api === GraphicsInfo.Unknown

    property real _radius: 0
    property real _fade: 0
    // Well past the farthest corner from the touch: the wave's broad soft
    // edge (shaders/ripple.frag) should cross the whole control, not stop in
    // it. In item pixels, and `Theme.dp` carries the app's scale.
    readonly property real _reach: {
        if (reach > 0)
            return reach
        const dx = Math.max(_origin.x, width - _origin.x)
        const dy = Math.max(_origin.y, height - _origin.y)
        return Math.sqrt(dx * dx + dy * dy) * 1.6 + Theme.dp(28)
    }
    property point _origin: Qt.point(width / 2, height / 2)

    anchors.fill: parent

    // A handler may say `pressed` before it has the touch's position: the
    // origin follows it while the wave is still just setting out.
    onPressXChanged: if (pressed && _radius < Theme.dp(8)) _origin = Qt.point(pressX, pressY)
    onPressYChanged: if (pressed && _radius < Theme.dp(8)) _origin = Qt.point(pressX, pressY)

    onPressedChanged: {
        if (pressed) {
            released.stop()
            _origin = Qt.point(pressX, pressY)
            _radius = 0
            touched.restart()
        } else {
            released.restart()
        }
    }

    // Down: the wave sets out, quick then slowing, the stars come on.
    ParallelAnimation {
        id: touched
        NumberAnimation { target: ripple; property: "_fade"; to: 1; duration: 80 }
        NumberAnimation { target: ripple; property: "_radius"; from: 0; to: ripple._reach; duration: ripple.spreadDuration; easing.type: Easing.OutCubic }
    }
    // Up: it reaches the edges and fades away.
    ParallelAnimation {
        id: released
        NumberAnimation { target: ripple; property: "_radius"; to: ripple._reach; duration: ripple.releaseDuration; easing.type: Easing.OutCubic }
        SequentialAnimation {
            PauseAnimation { duration: ripple.fadeDelay }
            NumberAnimation { target: ripple; property: "_fade"; to: 0; duration: ripple.fadeDuration; easing.type: Easing.InQuad }
        }
    }

    // Made on a touch and gone once the wave has faded: a list has one of these
    // in every row, and a shader item each made them slow to build.
    Loader {
        anchors.fill: parent
        active: ripple._fade > 0 || ripple.pressed
        sourceComponent: waveComponent
    }
    Component {
        id: waveComponent
    ShaderEffect {
        objectName: "rippleShader"
        visible: !ripple.flat && ripple._fade > 0
        property size size: Qt.size(width, height)
        property point origin: ripple._origin
        property real radius: ripple._radius
        property real cornerRadius: ripple.radius
        property real fade: ripple._fade
        property real time: 0
        // Item pixels per dp: the user's interface scale, unrounded.
        property real dp: Theme.scale
        property color color: ripple.color
        fragmentShader: "qrc:/shaders/ripple.frag.qsb"

        NumberAnimation on time {
            running: ripple._fade > 0 && !ripple.flat
            from: 0
            to: 1000
            duration: 1000 * 1000
            loops: Animation.Infinite
        }
    }
    }
}
