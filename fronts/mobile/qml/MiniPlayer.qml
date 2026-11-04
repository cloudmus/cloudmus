import QtQuick
import QtQuick.Layouts

// The bar at the bottom of the playlist: what plays, play/pause and next;
// a tap anywhere else opens Now Playing.
Rectangle {
    id: mini

    property QtObject nowPlaying
    // What the generated background behind it comes from.
    property string seed
    signal openRequested()

    implicitHeight: Theme.dp(72)
    color: Theme.surface200
    clip: true
    visible: nowPlaying && nowPlaying.hasTrack

    AuraBackground {
        anchors.fill: parent
        seed: mini.seed
        edgeFade: 0
    }

    // The track's progress along the top edge.
    Rectangle {
        anchors.top: parent.top
        height: Theme.dp(2)
        width: mini.nowPlaying && mini.nowPlaying.durationMs > 0
            ? parent.width * mini.nowPlaying.positionMs / mini.nowPlaying.durationMs : 0
        color: Theme.accent
    }

    TapHandler { onTapped: mini.openRequested() }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.dp(8)
        spacing: Theme.dp(12)

        Cover {
            id: cover
            Layout.preferredWidth: Theme.dp(48)
            Layout.preferredHeight: Theme.dp(48)
            objectName: "miniCover"
            source: mini.nowPlaying ? mini.nowPlaying.coverUrl : ""
            seed: mini.seed
            zoomOnChange: true
        }

        TrackTitleBlock {
            objectName: "miniTitle"
            Layout.fillWidth: true
            title: mini.nowPlaying ? mini.nowPlaying.title : ""
            artist: mini.nowPlaying ? mini.nowPlaying.artist : ""
            direction: mini.nowPlaying && mini.nowPlaying.trackDirection === -1 ? -1 : 1
            titleColor: "#FFFFFF"
            artistColor: "#D9FFFFFF"
            titleSize: Theme.fontBody
            titleWeight: Font.DemiBold
            artistSize: Theme.fontSmall
        }

        IconButton {
            iconName: mini.nowPlaying && mini.nowPlaying.playing ? "pause" : "play_arrow"
            size: Theme.touchLarge
            color: "#FFFFFF"
            onClicked: mini.nowPlaying.togglePause()
        }
        IconButton {
            iconName: "skip_next"
            color: "#FFFFFF"
            onClicked: mini.nowPlaying.next()
        }
    }
}
