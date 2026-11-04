import QtQuick
import QtQuick.Layouts

// The bar at the bottom of the playlist: what plays, and over it the track's
// progress with what's downloaded ahead; below, the transport with like and
// dislike. A tap anywhere else opens Now Playing, a swipe up pulls it out.
Rectangle {
    id: mini

    property QtObject nowPlaying
    // What the generated background behind it comes from.
    property string seed
    // How far the tracks after this one have got (PreloadCells).
    property var preloadFractions: []
    signal openRequested()
    // A finger pulling it up, by so much; let go, at that speed upwards.
    signal pulled(real distance)
    signal pullEnded(real velocity)

    implicitHeight: Theme.dp(124) + Theme.insetBottom
    color: Theme.surface200
    clip: true
    visible: nowPlaying && nowPlaying.hasTrack

    AuraBackground {
        id: aura
        anchors.fill: parent
        seed: mini.seed
        edgeFade: 0
    }

    readonly property color _ink: aura.ready ? "#FFFFFF" : Theme.ink

    // The track's progress along the top edge, then the tracks after it.
    Row {
        id: progressLine
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.dp(3)
        Rectangle {
            width: parent.width - (cells.visible ? cells.width + parent.spacing : 0)
            height: Theme.dp(4)
            color: Qt.rgba(1, 1, 1, 0.25)
            Rectangle {
                objectName: "miniBuffered"
                visible: mini.nowPlaying && mini.nowPlaying.bufferedMs > 0 && mini.nowPlaying.durationMs > 0
                height: parent.height
                width: visible ? parent.width * Math.min(1, mini.nowPlaying.bufferedMs / mini.nowPlaying.durationMs) : 0
                color: Qt.rgba(1, 1, 1, 0.5)
            }
            Rectangle {
                objectName: "miniProgress"
                height: parent.height
                width: mini.nowPlaying && mini.nowPlaying.durationMs > 0
                    ? parent.width * mini.nowPlaying.positionMs / mini.nowPlaying.durationMs : 0
                color: Theme.accentOnAura
            }
        }
        PreloadCells {
            id: cells
            objectName: "miniPreload"
            fractions: mini.preloadFractions
            troughColor: Qt.rgba(1, 1, 1, 0.25)
            fillColor: Qt.rgba(1, 1, 1, 0.6)
        }
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

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.dp(10)
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.padding
        anchors.bottomMargin: Theme.insetBottom
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
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
                titleColor: mini._ink
                artistColor: aura.ready ? "#D9FFFFFF" : Theme.inkSecondary
                titleSize: Theme.fontBody
                titleWeight: Font.DemiBold
                artistSize: Theme.fontSmall
            }
        }

        // Spread evenly, as far apart as the width allows.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            IconButton {
                objectName: "miniDislike"
                enabled: mini.nowPlaying && mini.nowPlaying.dislikeSupported === true
                iconName: mini.nowPlaying && mini.nowPlaying.disliked ? "heart_off" : "heart_off_outline"
                color: mini.nowPlaying && mini.nowPlaying.disliked ? Theme.accentOnAura : mini._ink
                onClicked: mini.nowPlaying.setDisliked(!mini.nowPlaying.disliked)
            }
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "miniPrevious"
                iconName: "skip_previous"
                color: mini._ink
                onClicked: mini.nowPlaying.previous()
            }
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "miniPlay"
                iconName: mini.nowPlaying && mini.nowPlaying.playing ? "pause" : "play_arrow"
                size: Theme.touchLarge
                color: mini._ink
                onClicked: mini.nowPlaying.togglePause()
            }
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "miniNext"
                iconName: "skip_next"
                color: mini._ink
                onClicked: mini.nowPlaying.next()
            }
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "miniLike"
                enabled: mini.nowPlaying && mini.nowPlaying.likeSupported === true
                iconName: mini.nowPlaying && mini.nowPlaying.liked ? "favorite" : "favorite_border"
                color: mini.nowPlaying && mini.nowPlaying.liked ? Theme.accentOnAura : mini._ink
                onClicked: mini.nowPlaying.setLiked(!mini.nowPlaying.liked)
            }
        }
    }
}
