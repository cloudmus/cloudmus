import QtQuick
import QtQuick.Window

// The desktop's Mesh Aura behind what plays: its colors come from `seed` (an
// album's or a track's title, a playlist's) and today's date. As on the
// desktop's hero, its edges dissolve into the window's color and a vignette
// keeps the white text over it legible. It's opaque from the start: the window's
// color until the picture is in, which Main.qml has ready for a page that opens.
Rectangle {
    id: aura

    property string seed
    // How far in the edges dissolve: 0 for a strip too thin for it.
    property real edgeFade: Theme.dp(45)
    readonly property bool ready: picture.showing

    color: Theme.surface0
    clip: true

    // Drawn anew at a new size only once that has settled: while it animates,
    // what's there stretches, as on the desktop's hero.
    property size _pixels: Qt.size(0, 0)
    function _settle() {
        if (width <= 0 || height <= 0)
            return
        const pixels = Qt.size(Math.round(width * Screen.devicePixelRatio),
                               Math.round(height * Screen.devicePixelRatio))
        if (pixels.width !== _pixels.width || pixels.height !== _pixels.height)
            _pixels = pixels
    }
    function _sizeChanged() {
        if (_pixels.width === 0)
            _settle()
        else
            settling.restart()
    }
    onWidthChanged: _sizeChanged()
    onHeightChanged: _sizeChanged()
    Component.onCompleted: _sizeChanged()
    Timer {
        id: settling
        interval: 400
        onTriggered: aura._settle()
    }

    CrossfadeImage {
        id: picture
        anchors.fill: parent
        fadeDuration: 500
        pixels: aura._pixels
        target: aura._pixels.width > 0
            ? Theme.auraUrl(aura.seed, Math.round(aura.edgeFade * Screen.devicePixelRatio)) : ""
    }
}
