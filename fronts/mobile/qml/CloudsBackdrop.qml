import QtQuick
import QtQuick.Controls.impl
import QtQuick.Shapes

// Faint clouds, in the logo's shape, flying out of the logo toward the viewer
// behind a page's content. They swell with the music's loudness, rush forward
// on its hits, and the vanishing point leans toward a finger on the screen.
// A port of the desktop's Ui::CloudsBackdrop (fronts/desktop/src/Ui/CloudsBackdrop.cpp),
// its numbers kept. Moves only while `running`.
Item {
    id: backdrop
    // Clouds fly out past the edges: not over the status bar or the page's bar.
    clip: true

    // What the clouds fly out of: its center is the vanishing point, and where
    // the glow sits. Anywhere in the scene.
    property Item origin: null
    // Ui::Backend's pulse (ViewModel::PulseScreen); null: they just drift.
    property QtObject pulse: null
    property bool running: visible
    // Where a finger is, in this item's coordinates; undefined with none.
    property var lean

    // The music's, or the logo's sound's while it plays.
    readonly property real level: Math.max(pulse && pulse.level !== undefined ? pulse.level : 0,
                                           pulse && pulse.logoLevel !== undefined ? pulse.logoLevel : 0)
    // Each step of the clouds, `dtMs` after the last: for what moves with them
    // (PulsingLogo) to step too, so the window is drawn once for both.
    signal stepped(real dtMs)

    readonly property int _count: 14
    readonly property real _nearestDepth: 0.15 // past this a cloud has left the frame
    readonly property real _driftPerSecond: 0.12 // of the depth range: ~7 s to fly past
    readonly property real _rushBoost: 5.0 // extra speed, in multiples of the drift, at a full beat
    readonly property real _rushFallMs: 400
    readonly property real _flashFallMs: 300
    // The logo's last strike, its strongest: the handpan rings on for seconds,
    // and the flash dies away with it.
    readonly property real _lastStrikeFallMs: 1800
    property real _flashFall: _flashFallMs
    readonly property real _maxOpacity: 0.055 // "barely visible"
    readonly property real _levelOpacity: 0.7 // how much loudness adds on top
    readonly property real _levelSwell: 0.12
    // A beat swells the clouds and lights them up for a moment, on top.
    readonly property real _beatSwell: 0.15
    readonly property real _beatOpacity: 1.0
    readonly property real _leanShare: 0.04 // of the item's size, at the edge
    readonly property real _leanFollowMs: 250
    // cloud.svg's proportions.
    readonly property real _spriteAspect: 5 / 8

    property real _time: 0
    property real _rush: 0 // 0..1 after a beat, falling
    property real _flash: 0 // the glow's flash, same
    property real _leanX: 0 // smoothed finger offset, -1..1
    property real _leanY: 0
    property var _clouds: []

    function _respawn(cloud, anywhere) {
        // Not straight at the logo, so a cloud's path doesn't hide behind it.
        const angle = Math.random() * 2 * Math.PI
        const radius = 0.25 + Math.random() * 0.75
        cloud.dx = Math.cos(angle) * radius
        cloud.dy = Math.sin(angle) * radius
        cloud.depth = anywhere ? _nearestDepth + Math.random() * (1 - _nearestDepth) : 1.0
        cloud.flipped = Math.random() < 0.5
        cloud.fresh = true // its item takes its look and place in the order
    }

    Component.onCompleted: {
        const clouds = []
        for (let i = 0; i < _count; ++i) {
            const cloud = {}
            _respawn(cloud, true)
            clouds.push(cloud)
        }
        _clouds = clouds
        _tick(0)
    }

    Connections {
        target: backdrop.pulse
        ignoreUnknownSignals: true
        function onBeat(strength) {
            backdrop._rush = Math.max(backdrop._rush, strength)
            backdrop._flash = Math.max(backdrop._flash, strength)
            backdrop._flashFall = backdrop._flashFallMs
        }
        // The logo's own sound: its strikes send the clouds harder than a beat.
        function onLogoHit(strength) {
            backdrop._rush = Math.max(backdrop._rush, strength * 1.6)
            backdrop._flash = Math.max(backdrop._flash, strength * 1.4)
            // The strongest is the last (LogoSound's table).
            backdrop._flashFall = strength >= 1 ? backdrop._lastStrikeFallMs : backdrop._flashFallMs
        }
    }

    function _clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }

    function _tick(dt) {
        const dtMs = dt * 1000
        _time += dt
        _rush *= Math.exp(-dtMs / _rushFallMs)
        _flash *= Math.exp(-dtMs / _flashFall)

        const speed = _driftPerSecond * (1 + _rushBoost * _rush)
        for (const cloud of _clouds) {
            cloud.depth -= speed * dt
            if (cloud.depth < _nearestDepth)
                _respawn(cloud, false)
        }

        // The finger: where it is relative to the center, as a share of half
        // the size; with none, back to the middle.
        const halfW = width / 2
        const halfH = height / 2
        let targetX = 0
        let targetY = 0
        if (lean !== undefined && lean !== null && halfW > 0 && halfH > 0) {
            targetX = _clamp((lean.x - halfW) / halfW, -1, 1)
            targetY = _clamp((lean.y - halfH) / halfH, -1, 1)
        }
        const follow = 1 - Math.exp(-dtMs / _leanFollowMs)
        _leanX += (targetX - _leanX) * follow
        _leanY += (targetY - _leanY) * follow

        let vanishX = halfW
        let vanishY = halfH
        if (origin) {
            const center = origin.mapToItem(backdrop, origin.width / 2, origin.height / 2)
            vanishX = center.x
            vanishY = center.y
        }
        vanishX += _leanX * width * _leanShare
        vanishY += _leanY * height * _leanShare

        // The glow behind the logo: breathes while it's quiet, follows the
        // loudness, flashes on a hit.
        const breath = 0.5 + 0.5 * Math.sin(_time * 1.3)
        glow.opacity = Math.min(1, 0.07 + 0.04 * breath + 0.3 * level + 0.35 * _flash)
        glow.scale = (0.28 + 0.14 * level + 0.10 * _flash) / glow.share
        glow.x = vanishX - glow.width / 2
        glow.y = vanishY - glow.height / 2

        const half = Math.max(width, height) / 2
        const base = Math.min(width, height) * 0.5
        const swell = 1 + _levelSwell * level + _beatSwell * _flash
        const brightness = _maxOpacity * (1 + _levelOpacity * level + _beatOpacity * _flash)
        for (let i = 0; i < _clouds.length; ++i) {
            const item = clouds.itemAt(i)
            if (!item)
                continue
            const cloud = _clouds[i]
            const nearness = 1 / cloud.depth - 1 // 0 at the vanishing point, growing as it comes
            const w = base / cloud.depth * 0.3 * swell
            const h = w * _spriteAspect
            // In from the distance, out as it passes.
            const fadeIn = _clamp((1 - cloud.depth) / 0.15, 0, 1)
            const fadeOut = _clamp((cloud.depth - _nearestDepth) / 0.15, 0, 1)
            const opacity = brightness * fadeIn * fadeOut
            item.visible = opacity >= 0.002
            if (!item.visible)
                continue
            item.opacity = opacity
            item.width = w
            item.height = h
            item.x = vanishX + cloud.dx * half * nearness * 0.35 - w / 2
            item.y = vanishY + cloud.dy * half * nearness * 0.35 - h / 2
            // Only when it comes anew: they all fly at one speed, so the order
            // changes only then, and a change of order rebuilds the scene.
            if (cloud.fresh) {
                cloud.fresh = false
                item.mirror = cloud.flipped
                // Far ones under the near ones.
                item.z = -cloud.depth
            }
        }
    }

    // At the screen's rate: the page is seldom open, and only while it's seen.
    FrameAnimation {
        running: backdrop.running && backdrop.width > 0
        onTriggered: {
            // A long gap (the app was away) isn't flown through at once.
            const dt = Math.min(frameTime, 0.1)
            backdrop._tick(dt)
            backdrop.stepped(dt * 1000)
        }
    }

    // Its size the largest it gets; scaled down to how large it is now.
    Shape {
        id: glow
        readonly property real share: 0.52 // of the shorter side, at the most
        width: Math.min(backdrop.width, backdrop.height) * share * 2
        height: width
        z: -2
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.width / 2
                centerY: glow.height / 2
                centerRadius: glow.width / 2
                focalX: centerX
                focalY: centerY
                // The logo's gradient, lit.
                GradientStop { position: 0; color: "#FF9A5C" }
                GradientStop { position: 1; color: "#00FF9A5C" }
            }
            startX: 0
            startY: 0
            PathLine { x: glow.width; y: 0 }
            PathLine { x: glow.width; y: glow.height }
            PathLine { x: 0; y: glow.height }
            PathLine { x: 0; y: 0 }
        }
    }

    Repeater {
        id: clouds
        model: backdrop._count
        // White in a dark theme; in a light one white would vanish, so the
        // brand color. A ColorImage, not an IconImage: that one draws its SVG
        // over again on every change of size, and these change on every frame.
        delegate: ColorImage {
            visible: false
            source: "qrc:/icons/cloud.svg"
            color: Theme.dark ? "#FFFFFF" : Theme.accent
            // Faint and soft: no need for every pixel of the nearest one. Set,
            // so the picture isn't made again for each size.
            sourceSize.width: 192
            sourceSize.height: 120
            fillMode: Image.Stretch
            smooth: true
        }
    }
}
