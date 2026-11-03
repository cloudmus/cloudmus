import QtQuick
import QtQuick.Window
import QtQuick.Controls.impl

// A track's cover with rounded corners, or a note on a surface when it has
// none. The corners are in the image itself: see CoverImageProvider.
Rectangle {
    id: cover

    property string source
    // With no cover: generated art from this, as on the desktop, if set.
    property string seed
    // An accent line around it, for the one that's active.
    property bool outlined: false

    color: image.status === Image.Ready ? "transparent" : Theme.surface300
    radius: Theme.dp(6)

    // In pixels, and whole, for the corners to come out the same in every
    // cover the same size.
    readonly property int _side: Math.max(64, Math.round(width * Screen.devicePixelRatio))
    readonly property int _radius: Math.round(radius * Screen.devicePixelRatio)

    Image {
        id: image
        anchors.fill: parent
        source: {
            const prefix = "image://cover/" + cover._radius + "/"
            if (cover.source !== "")
                return prefix + "f" + encodeURIComponent(cover.source)
            return cover.seed !== "" ? prefix + "s" + encodeURIComponent(cover.seed) : ""
        }
        asynchronous: true
        sourceSize.width: cover._side
        sourceSize.height: cover._side
        visible: status === Image.Ready
    }

    IconImage {
        anchors.centerIn: parent
        visible: image.status !== Image.Ready
        source: "qrc:/icons/music_note.svg"
        sourceSize.width: Math.round(cover.width * 0.45)
        sourceSize.height: Math.round(cover.width * 0.45)
        color: Theme.inkTertiary
    }

    Rectangle {
        anchors.fill: parent
        visible: cover.outlined
        radius: cover.radius
        color: "transparent"
        border.width: Theme.dp(3)
        border.color: Theme.accent
    }
}
