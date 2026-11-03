import QtQuick
import QtQuick.Controls.impl

// A track's cover, or a note on a surface when it has none.
Rectangle {
    id: cover

    property string source
    // With no cover: generated art from this, as on the desktop, if set.
    property string seed

    color: Theme.surface300
    radius: Theme.dp(6)
    clip: true

    Image {
        id: image
        anchors.fill: parent
        source: cover.source !== "" ? cover.source
              : cover.seed !== "" ? "image://aura/s" + encodeURIComponent(cover.seed) : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Math.max(64, cover.width * Screen.devicePixelRatio)
        sourceSize.height: Math.max(64, cover.width * Screen.devicePixelRatio)
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
}
