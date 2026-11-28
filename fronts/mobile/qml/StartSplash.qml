import QtQuick
import QtQuick.Window
import QtQuick.Shapes

// The start screen, over everything until the core is up. It takes over from
// Android's own (MainActivity's StartScreen; the window's background before
// Android 12) with the same logo in the same place, and only then has that
// one taken down: no blink. Then the color goes out of the logo, the white
// (or, in a light theme, ink) one stays and beats, a glow under it, as on the
// website's front page; then it fades away from over the pages.
Item {
    id: splash

    required property QtObject platform
    // The core's up: on to the pages.
    property bool ready: false
    // Under the logo: after a while on the start, at once when coming back
    // (the player's process was ended while the pages were up).
    property string startingText: ""
    property string reconnectingText: ""
    // The pages under it are seen, any of them: draw them.
    readonly property bool contentShown: opacity < 1

    // Before Android 12 (splash_background.xml), or with no icon to go by: in
    // the middle, this large. Device-independent pixels, as Android has them.
    readonly property real fallbackSide: 128

    // handoff: drawing what Android's showing; morph: the color going out;
    // pulse: waiting for the core; reveal: going; done.
    property string _phase: "handoff"
    // Where Android 12+'s start screen has the logo (StartScreen.java).
    property rect _icon: Qt.rect(0, 0, 0, 0)
    readonly property real _side: _icon.width > 0 ? _icon.width : fallbackSide
    readonly property real _left: _icon.width > 0 ? _icon.x : (width - _side) / 2
    readonly property real _top: _icon.height > 0 ? _icon.y : (height - _side) / 2

    // The site's logo (viewBox 16) in the colored one's (logo.svg, 64): the
    // white of the one is exactly the other. Worked out from their paths.
    readonly property real _monoLeft: _left + 0.14448 * _side
    readonly property real _monoTop: _top + 0.14928 * _side
    readonly property real _monoSide: 0.69130 * _side

    property real _ink: 0 // white to the theme's ink, in a light theme
    property real _glowIn: 0
    property real _lift: 1 // the logo grows a little as it goes
    property bool _waitedLong: false
    property bool _again: false

    // How hard the logo was last hit, settling: the beat, as on the site.
    property real _env: 0
    property real _bpm: 112
    property real _untilBeatMs: 500
    property int _beats: 0
    // The site's swells, larger: there the logo's a screen wide, here it's
    // small and they didn't show.
    property var _parts: [
        { amp: 0.08, k: 0.16, x: 0, v: 0 }, // the cloud
        { amp: 0.15, k: 0.2, x: 0, v: 0 }, // the outer ring
        { amp: 0.24, k: 0.26, x: 0, v: 0 }, // the inner ring
        { amp: 0.36, k: 0.34, x: 0, v: 0 }, // the core
    ]
    property var _scales: [1, 1, 1, 1]

    anchors.fill: parent
    visible: _phase !== "done"
    // Fading as one picture: part by part, the glow showed through the logo.
    layer.enabled: _phase === "reveal" || back.running

    function _readIcon() {
        const px = platform.splashIcon()
        const ratio = Screen.devicePixelRatio
        const icon = px.width > 0 ? Qt.rect(px.x / ratio, px.y / ratio, px.width / ratio, px.height / ratio)
                                  : Qt.rect(0, 0, 0, 0)
        if (icon.x === _icon.x && icon.y === _icon.y && icon.width === _icon.width)
            return false
        _icon = icon
        return true
    }

    function _maybeReveal() {
        if (ready && _phase === "pulse")
            reveal.start()
    }
    onReadyChanged: {
        if (ready) {
            _maybeReveal()
        } else if (_phase === "done" || _phase === "reveal") {
            // The player's process was ended: back over the pages till it's up.
            reveal.stop()
            _again = true
            back.start()
        }
    }

    Component.onCompleted: _readIcon()

    // Android's screen goes once this one's surely shown the same: two frames
    // in, the first one maybe not on the screen yet. Its place could have been
    // learned late: then drawn there first.
    property int _framesShown: 0
    Connections {
        target: splash.Window.window
        enabled: splash._phase === "handoff"
        function onFrameSwapped() {
            if (++splash._framesShown < 2)
                return
            if (splash._readIcon()) {
                splash._framesShown = 0
                return
            }
            splash.platform.hideSplash()
            splash._phase = "morph"
            morph.start()
        }
    }

    SequentialAnimation {
        id: morph
        PauseAnimation { duration: 150 }
        ParallelAnimation {
            NumberAnimation { target: colorLogo; property: "opacity"; to: 0; duration: 550; easing.type: Easing.InOutQuad }
            NumberAnimation { target: splash; property: "_glowIn"; to: 1; duration: 900; easing.type: Easing.OutCubic }
            NumberAnimation { target: splash; property: "_ink"; to: 1; duration: 550; easing.type: Easing.InOutQuad }
        }
        ScriptAction {
            script: {
                splash._phase = "pulse"
                splash._maybeReveal()
            }
        }
    }

    ParallelAnimation {
        id: reveal
        onStarted: splash._phase = "reveal"
        onFinished: splash._phase = "done"
        NumberAnimation { target: splash; property: "opacity"; to: 0; duration: 450; easing.type: Easing.InOutQuad }
        NumberAnimation { target: splash; property: "_lift"; to: 1.15; duration: 450; easing.type: Easing.OutCubic }
    }

    SequentialAnimation {
        id: back
        ScriptAction {
            script: {
                colorLogo.opacity = 0
                splash._glowIn = 1
                splash._ink = 1
                splash._lift = 1
                splash._phase = "pulse"
            }
        }
        NumberAnimation { target: splash; property: "opacity"; to: 1; duration: 250; easing.type: Easing.OutQuad }
        ScriptAction { script: splash._maybeReveal() }
    }

    Timer {
        interval: 2000
        running: splash._phase === "pulse"
        onTriggered: splash._waitedLong = true
    }

    // The site's beat, there being no music yet: about 112 a minute, now and
    // then off it, a bar's first one the strongest; each part of the logo on
    // its own spring, the smaller the livelier.
    function _step(dtMs) {
        _untilBeatMs -= dtMs
        if (_untilBeatMs <= 0) {
            ++_beats
            if (Math.random() > 0.08)
                _env = Math.max(_env, _beats % 4 === 1 ? 1 : 0.55 + Math.random() * 0.3)
            if (_beats % 32 === 0)
                _bpm = 96 + Math.random() * 40
            _untilBeatMs += 60000 / _bpm * (1 + (Math.random() - 0.5) * 0.04)
            if (_untilBeatMs < 0)
                _untilBeatMs = 60000 / _bpm
        }
        _env *= Math.pow(0.0035, dtMs / 1000)
        const scales = []
        const steps = Math.ceil(dtMs / 4)
        const st = dtMs / steps / 1000
        for (const part of _parts) {
            const w = 2 * Math.PI * (5 + part.k * 12)
            for (let i = 0; i < steps; ++i) {
                part.v += ((_env - part.x) * w * w - 2 * 0.32 * w * part.v) * st
                part.x += part.v * st
            }
            if (!isFinite(part.x)) {
                part.x = 0
                part.v = 0
            }
            part.x = Math.max(-0.5, Math.min(1.5, part.x))
            scales.push(1 + part.x * part.amp)
        }
        _scales = scales
    }

    FrameAnimation {
        running: splash.visible && splash._phase !== "handoff"
        // A long gap (the app was away) isn't caught up with at once.
        onTriggered: splash._step(Math.min(frameTime, 0.05) * 1000)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.windowBackground
    }

    // The site's glow: brighter and wider on a beat.
    Shape {
        id: glow
        readonly property real base: splash._monoSide * 4.5
        x: splash._monoLeft + splash._monoSide / 2 - width / 2
        y: splash._monoTop + splash._monoSide / 2 - height / 2
        width: base
        height: base
        scale: splash._lift * splash._glowIn * (160 + splash._env * 140) / 300
        opacity: splash._glowIn * (0.45 + splash._env * 0.55)
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.width / 2
                centerY: glow.height / 2
                centerRadius: glow.width / 2
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0; color: Qt.rgba(1, 110 / 255, 60 / 255, 0.7) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 90 / 255, 43 / 255, 0.25) }
                GradientStop { position: 1; color: Qt.rgba(1, 90 / 255, 43 / 255, 0) }
            }
            startX: 0
            startY: 0
            PathLine { x: glow.width; y: 0 }
            PathLine { x: glow.width; y: glow.height }
            PathLine { x: 0; y: glow.height }
            PathLine { x: 0; y: 0 }
        }
    }

    // The site's logo, in four parts that beat apart: drawn at 16 and scaled,
    // as sharp at any size.
    Item {
        id: mono
        readonly property color ink: Theme.dark ? "#FFFFFF" : Theme.ink
        readonly property color fill: Qt.rgba(1 + (ink.r - 1) * splash._ink, 1 + (ink.g - 1) * splash._ink,
                                              1 + (ink.b - 1) * splash._ink, 1)
        x: splash._monoLeft
        y: splash._monoTop
        width: splash._monoSide
        height: splash._monoSide
        scale: splash._lift

        Item {
            width: 16
            height: 16
            transformOrigin: Item.TopLeft
            scale: mono.width / 16

            Repeater {
                model: [
                    // The cloud.
                    "M 10.805134,2.0034 c -0.8362928,0 -1.7080148,0.3356 -2.2734378,1.0332 a 5.0000003,4.9999737 0 0 1 4.4687498,4.9668 5.0000003,4.9999737 0 0 1 -4.8457028,4.9922 c 1.9267958,0 3.9492808,0.01 5.7070308,0 1.185276,-0.029 2.138672,-1.0927 2.138672,-2.4024 0,-1.0484 -0.609309,-1.9363 -1.460938,-2.2656 0.226555,-0.5585 0.353516,-1.1769 0.353516,-1.8281 0,-2.4817 -1.829407,-4.4961 -4.08789,-4.4961 z M 5.6996652,3.2222 c -1.401019,-0.067 -2.481787,1.1337 -2.289063,2.4375 -2.3248568,-0.6557 -3.37528284,1.5189 -3.41015594317,3.6933 -0.0286499968,1.7521 1.32587614317,3.6065 3.00585894317,3.6426 1.725453,0 3.305552,0 4.994141,0 l 0,-0.9922 a 4.0000001,3.999963 0 0 1 -4,-4 4.0000001,3.999963 0 0 1 3.679688,-3.9844 c -0.393282,-0.4851 -0.974408,-0.8156 -1.980469,-0.7968 z",
                    // The outer ring.
                    "M 8.0004462,12.0034 a 4.0000001,3.999963 0 0 0 3.9999998,-4 4.0000001,3.999963 0 0 0 -3.9570308,-3.998 c -0.01629,0.059 -0.02759,0.122 -0.04297,0.1816 l 0,0.8164 a 3.0000003,2.9999238 0 0 1 2.9999998,3 3.0000003,2.9999238 0 0 1 -2.9999998,3 l 0,1 z",
                    // The inner ring.
                    "M 8.0004462,11.0034 l 0,-1 a 2.0000001,1.999963 0 0 1 -2,-2 2.0000001,1.999963 0 0 1 2,-2 l 0,-1 a 3.0000001,2.9999629 0 0 0 -3,3 3.0000001,2.9999629 0 0 0 3,3 z",
                    // The core.
                    "M 8.0004462,6.0034 l 0,4 a 2.0000001,1.999963 0 0 0 1.9999998,-2 2.0000001,1.999963 0 0 0 -1.9999998,-2 z",
                ]
                delegate: Shape {
                    required property string modelData
                    required property int index
                    width: 16
                    height: 16
                    // The site's transform-origin: the middle of the 16.
                    scale: splash._scales[index]
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: "transparent"
                        fillColor: mono.fill
                        PathSvg { path: modelData }
                    }
                }
            }
        }
    }

    // What Android showed, over the white one until the color goes out of it.
    Image {
        id: colorLogo
        x: splash._left
        y: splash._top
        width: splash._side
        height: splash._side
        source: "qrc:/icons/logo.svg"
        sourceSize.width: Math.ceil(splash._side)
        sourceSize.height: Math.ceil(splash._side)
        smooth: true
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: splash._top + splash._side + Theme.dp(28)
        text: splash._again ? splash.reconnectingText : splash.startingText
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        opacity: splash._again || splash._waitedLong ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 400 } }
    }
}
