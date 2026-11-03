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

    readonly property size _pixels: Qt.size(Math.max(1, Math.round(width * Screen.devicePixelRatio)),
                                            Math.max(1, Math.round(height * Screen.devicePixelRatio)))

    function _url(text) {
        return "image://aura/h" + Theme.surface0.toString().substring(1) + "/"
            + Math.round(edgeFade * Screen.devicePixelRatio) + "/" + encodeURIComponent(text)
    }

    clip: true

    // The one showing; the new one loads on top and takes over once it's in.
    Image {
        id: below
        anchors.fill: parent
        sourceSize: aura._pixels
        cache: false
    }
    Image {
        id: above
        anchors.fill: parent
        asynchronous: true
        sourceSize: aura._pixels
        source: aura.width > 0 && aura.height > 0 ? aura._url(aura.seed) : ""
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
            onFinished: below.source = above.source
        }
    }
}
