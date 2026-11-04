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
    // The size to draw it at, if not its own: a size that changes then only
    // stretches what's there, with nothing generated anew.
    property size artSize: Qt.size(0, 0)

    color: Theme.surface0
    clip: true

    // Drawn anew at a new size only once that has settled: while it animates,
    // what's there stretches, as on the desktop's hero.
    property size _pixels: Qt.size(0, 0)
    function _settle() {
        const w = artSize.width > 0 ? artSize.width : width
        const h = artSize.height > 0 ? artSize.height : height
        if (w <= 0 || h <= 0 || !visible)
            return
        const pixels = Qt.size(Math.round(w * Screen.devicePixelRatio), Math.round(h * Screen.devicePixelRatio))
        if (pixels.width !== _pixels.width || pixels.height !== _pixels.height)
            _pixels = pixels
    }
    function _sizeChanged() {
        if (_pixels.width === 0)
            _settle()
        else
            settling.restart()
    }
    onArtSizeChanged: _sizeChanged()
    onVisibleChanged: _sizeChanged()
    onWidthChanged: if (artSize.width <= 0) _sizeChanged()
    onHeightChanged: if (artSize.height <= 0) _sizeChanged()
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
