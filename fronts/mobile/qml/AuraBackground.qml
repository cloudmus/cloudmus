import QtQuick

// The desktop's Mesh Aura behind what plays: its colors come from `seed` (an
// album's or a track's title, a playlist's) and today's date. A new seed fades
// in over the old one. Dimmed for the white text over it.
Item {
    id: aura

    property string seed
    property real dim: 0.28

    readonly property size _pixels: Qt.size(Math.max(1, Math.round(width)), Math.max(1, Math.round(height)))

    function _url(text) { return "image://aura/s" + encodeURIComponent(text) }

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
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: aura.dim
    }
}
