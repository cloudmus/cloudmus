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
    // A finger pulling it up, by so much; let go, at that speed upwards.
    signal pulled(real distance)
    signal pullEnded(real velocity)

    implicitHeight: Theme.dp(72) + Theme.insetBottom
    color: Theme.surface200
    clip: true
    visible: nowPlaying && nowPlaying.hasTrack

    AuraBackground {
        id: aura
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
    // As the finger lifts the handler zeroes its translation first, then says it's
    // over: that zero isn't a move, and the speed is the last one a move had.
    DragHandler {
        target: null
        xAxis.enabled: false
        property real velocity: 0
        property double movedAt: 0
        onActiveTranslationChanged: {
            if (!active || activeTranslation.y === 0)
                return
            velocity = -centroid.velocity.y
            movedAt = Date.now()
            mini.pulled(Math.max(0, -activeTranslation.y))
        }
        // A finger that stopped before lifting hasn't thrown anything.
        onActiveChanged: if (!active) mini.pullEnded(Date.now() - movedAt < 100 ? velocity : 0)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.dp(8)
        anchors.bottomMargin: Theme.insetBottom
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
            // White is for the picture, which isn't always in yet, and in the
            // light theme there's only a light background until it is.
            titleColor: aura.ready ? "#FFFFFF" : Theme.ink
            artistColor: aura.ready ? "#D9FFFFFF" : Theme.inkSecondary
            titleSize: Theme.fontBody
            titleWeight: Font.DemiBold
            artistSize: Theme.fontSmall
        }

        IconButton {
            iconName: mini.nowPlaying && mini.nowPlaying.playing ? "pause" : "play_arrow"
            size: Theme.touchLarge
            color: aura.ready ? "#FFFFFF" : Theme.ink
            onClicked: mini.nowPlaying.togglePause()
        }
        IconButton {
            iconName: "skip_next"
            color: aura.ready ? "#FFFFFF" : Theme.ink
            onClicked: mini.nowPlaying.next()
        }
    }
}
