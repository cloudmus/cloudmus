import QtQuick
import QtQuick.Window

// The desktop's Mesh Aura behind what plays: its colors come from `seed` (an
// album's or a track's title, a playlist's) and today's date. A new seed fades
// in over the old one. As on the desktop's hero, its edges dissolve into the
// window's color and a vignette keeps the white text over it legible.
Item {
    id: aura

    property string seed
    // How far in the edges dissolve: 0 for a strip too thin for it.
    property real edgeFade: Theme.dp(45)

    // Drawn anew at a new size only once that has settled: while it animates,
    // what's there stretches, as on the desktop's hero.
    property size _pixels: Qt.size(0, 0)
    property size _pixelsBelow: _pixels
    function _settle() {
        const pixels = Qt.size(Math.max(1, Math.round(width * Screen.devicePixelRatio)),
                               Math.max(1, Math.round(height * Screen.devicePixelRatio)))
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

    function _url(text) {
        return "image://aura/h" + Theme.surface0.toString().substring(1) + "/"
            + Math.round(edgeFade * Screen.devicePixelRatio) + "/" + encodeURIComponent(text)
    }

    clip: true

    // The one showing; the new one loads on top and takes over once it's in.
    Image {
        id: below
        anchors.fill: parent
        sourceSize: aura._pixelsBelow
        cache: false
    }
    Image {
        id: above
        anchors.fill: parent
        asynchronous: true
        sourceSize: aura._pixels
        source: aura._pixels.width > 1 && aura._pixels.height > 1 ? aura._url(aura.seed) : ""
        cache: false
        opacity: 0
        onStatusChanged: {
            if (status === Image.Ready) {
                fade.stop()
                opacity = 0
                fade.start()
            }
        }
        NumberAnimation {
            id: fade
            target: above
            property: "opacity"
            to: 1
            duration: 500
            onFinished: {
                aura._pixelsBelow = aura._pixels
                below.source = above.source
            }
        }
    }
}
