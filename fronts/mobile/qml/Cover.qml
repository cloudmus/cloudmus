import QtQuick
import QtQuick.Controls.impl

// A track's cover, or a note on a surface when it has none.
Rectangle {
    id: cover

    property string source

    color: Theme.surface300
    radius: Theme.dp(6)
    clip: true

    Image {
        id: image
        anchors.fill: parent
        source: cover.source
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Math.max(64, cover.width * Screen.devicePixelRatio)
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
