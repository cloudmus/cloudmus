import QtQuick
import QtQuick.Layouts

// What a source can do, as a row of tags that wraps; `model` is the ids
// from the core (LibraryScreen / SourceSettingsScreen `capabilities`).
Flow {
    id: tags

    property var model: []
    readonly property int count: repeater.count

    spacing: Theme.dp(8)

    function title(id) {
        switch (id) {
        case "playlists": return qsTr("Playlists")
        case "likedTracks": return qsTr("Liked tracks")
        case "radio": return qsTr("Radio")
        case "search": return qsTr("Search")
        case "editPlaylists": return qsTr("Playlist editing")
        case "like": return qsTr("Likes")
        case "dislike": return qsTr("Dislikes")
        case "stream": return qsTr("Streaming")
        case "seek": return qsTr("Seeking")
        case "download": return qsTr("Downloads")
        case "settings": return qsTr("Settings")
        }
        return id
    }

    Repeater {
        id: repeater
        model: tags.model
        Rectangle {
            required property string modelData
            implicitWidth: label.implicitWidth + Theme.dp(24)
            implicitHeight: label.implicitHeight + Theme.dp(12)
            radius: height / 2
            // Outlined as the desktop's, and a step off the card they're on.
            color: Theme.surface300
            border.width: Theme.dp(1)
            border.color: Theme.borderStrong
            Text {
                id: label
                anchors.centerIn: parent
                text: tags.title(parent.modelData)
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
