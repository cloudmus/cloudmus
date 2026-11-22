import QtQuick
import QtQuick.Layouts

// A phone's player: the mini player at the bottom of the main screen, and Now
// Playing, which grows out of it. `progress` goes from the one (0) to the other
// (1): the background reaches up from the bar to the top, and the bar's cover,
// title and transport grow into Now Playing's. Main.qml moves it, following a
// finger up from the bar or down from the top; a tap on the bar opens it, the
// arrow or Back closes it.
//
// It takes the whole screen all along, the pages under it showing through
// above the bar; what's above isn't pressed, as it's clipped away.
Item {
    id: page

    property QtObject backend
    property QtObject platform
    readonly property QtObject nowPlaying: backend.nowPlaying
    property real progress: 0
    property bool swipeEnabled: true
    signal openRequested()
    signal closeRequested()
    signal trackMenuRequested()
    // A finger moving it: where it'd be now (0 .. 1); let go, at what speed
    // upwards, and whether it had been open when it was taken.
    signal dragged(real progress)
    signal dragEnded(real velocity, bool fromOpen)

    // How far the top goes, from the bar's top up to the page's.
    readonly property real travel: Math.max(1, height - Theme.miniPlayerHeight)
    readonly property string seed: nowPlaying && nowPlaying.backgroundSeed ? nowPlaying.backgroundSeed
        : backend.playlist ? backend.playlist.title : ""
    function _part(from, to) { return Math.max(0, Math.min(1, (progress - from) / (to - from))) }

    // What there is of it, on up under the status bar once open (it's see-through).
    Item {
        id: surface
        objectName: "nowPlayingSurface"
        y: page.travel * (1 - page.progress) - Theme.insetTop * page.progress
        width: page.width
        height: page.height - y
        clip: true

        // The mini player's own, stretching as it grows: it's made for a strip
        // with no room for edges dissolving.
        AuraBackground {
            id: miniAura
            anchors.fill: parent
            visible: fullAura.opacity < 1
            seed: page.seed
            edgeFade: 0
            artSize: Qt.size(page.width, Theme.miniPlayerHeight)
        }
        // Now Playing's, where it stays: the surface only uncovers it. Always
        // there, if see-through: drawn beforehand (Main.qml), it's ready when asked.
        AuraBackground {
            id: fullAura
            objectName: "nowPlayingAura"
            y: -surface.y - Theme.insetTop
            width: page.width
            height: page.height + Theme.insetTop
            opacity: page._part(0, 0.5)
            seed: page.seed
        }

        // In a mini player: the track's progress along its top edge, then the
        // tracks after it. Now Playing has a seek bar instead.
        Row {
            objectName: "miniProgressLine"
            width: parent.width
            spacing: Theme.dp(3)
            opacity: 1 - page._part(0, 0.25)
            visible: opacity > 0
            Rectangle {
                width: parent.width - (cells.visible ? cells.width + parent.spacing : 0)
                height: Theme.dp(4)
                color: Qt.rgba(1, 1, 1, 0.25)
                Rectangle {
                    objectName: "miniBuffered"
                    visible: page.nowPlaying && page.nowPlaying.bufferedMs > 0 && page.nowPlaying.durationMs > 0
                    height: parent.height
                    width: visible ? parent.width * Math.min(1, page.nowPlaying.bufferedMs / page.nowPlaying.durationMs) : 0
                    color: Qt.rgba(1, 1, 1, 0.5)
                }
                Rectangle {
                    objectName: "miniProgress"
                    height: parent.height
                    width: page.nowPlaying && page.nowPlaying.durationMs > 0
                        ? parent.width * page.nowPlaying.positionMs / page.nowPlaying.durationMs : 0
                    color: Theme.accentOnAura
                }
            }
            PreloadCells {
                id: cells
                objectName: "miniPreload"
                fractions: page.backend.mainScreen ? page.backend.mainScreen.preloadFractions : []
                troughColor: Qt.rgba(1, 1, 1, 0.25)
                fillColor: Qt.rgba(1, 1, 1, 0.6)
            }
        }

        // Closed, a tap anywhere but on a button opens it.
        TapHandler {
            id: tap
            enabled: page.progress <= 0
            onTapped: page.openRequested()
        }
        Ripple {
            objectName: "miniRipple"
            // Where it is on the page, not on the surface, whose top goes up: one
            // wave from where the finger came down, over the bar and on over Now
            // Playing as that uncovers more of it (a tap opens it), fading on its
            // own. Going as far as the screen does from the start: gone as far as
            // the bar only, it would come again over what grows, as a glow.
            anchors.fill: undefined
            y: -surface.y
            width: page.width
            height: page.height
            reach: Math.sqrt(page.width * page.width + page.height * page.height) * 1.3
            // Slower than a button's, for the way it has to go.
            spreadDuration: 3000
            releaseDuration: 2200
            fadeDelay: 200
            fadeDuration: 900
            pressed: tap.pressed
            pressX: tap.point.pressPosition.x
            // The handler's is on the surface.
            pressY: tap.point.pressPosition.y + surface.y
            color: "#FFFFFF"
        }
        // Up from the bar, down from the top: it follows the finger until let go.
        // As the finger lifts the handler zeroes its translation first, then says
        // it's over: that zero isn't a move, and the speed is the last one a move had.
        DragHandler {
            id: swipe
            // One way for a whole gesture, whichever it began as.
            enabled: page.swipeEnabled && !trackPanel.swipedSideways
            target: null
            xAxis.enabled: false
            property real from: 0
            property real velocity: 0
            property double movedAt: 0
            onActiveTranslationChanged: {
                if (!active || activeTranslation.y === 0)
                    return
                velocity = -centroid.velocity.y
                movedAt = Date.now()
                page.dragged(from - activeTranslation.y / page.travel)
            }
            onActiveChanged: {
                if (active) {
                    from = page.progress
                    return
                }
                // A finger that stopped before lifting hasn't thrown anything.
                page.dragEnded(Date.now() - movedAt < 100 ? velocity : 0, from >= 0.5)
            }
        }

        // What's on it, where it is on the page whatever of it is uncovered.
        Item {
            y: -surface.y
            width: page.width
            height: page.height

            // Coming down with the top as it opens.
            Item {
                objectName: "nowPlayingHeader"
                y: Theme.insetTop + surface.y + Theme.dp(24) * (1 - page._part(0.6, 1))
                width: parent.width
                height: Theme.barHeight
                opacity: page._part(0.6, 1)
                visible: opacity > 0
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(4)
                    anchors.rightMargin: Theme.padding
                    IconButton {
                        iconName: "expand_more"
                        iconSize: Theme.dp(32)
                        color: "#FFFFFF"
                        onClicked: page.closeRequested()
                    }
                    // What it plays from: that it's playing goes without saying here.
                    ShadowText {
                        objectName: "nowPlayingPlaylist"
                        Layout.fillWidth: true
                        text: page.backend.playlist && page.backend.playlist.title ? page.backend.playlist.title : qsTr("Now playing")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        color: "#E6FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                    // The track's menu, opposite the arrow: beside the title there's
                    // no width to spare. No "stop and close" here: next to "collapse"
                    // it read as closing the page, and a slip would cut the music
                    // (it's in the notification and in Settings).
                    IconButton {
                        objectName: "nowPlayingMenu"
                        opacity: page.backend.trackScreen !== null && page.nowPlaying && page.nowPlaying.hasTrack === true ? 1 : 0
                        enabled: opacity > 0
                        iconName: "more_vert"
                        color: "#FFFFFF"
                        onClicked: {
                            page.backend.trackScreen.showPlaying()
                            page.trackMenuRequested()
                        }
                    }
                }
            }

            TrackPanel {
                id: trackPanel
                anchors.fill: parent
                anchors.margins: Theme.padding * 1.5
                anchors.topMargin: Theme.barHeight + Theme.padding / 2
                anchors.bottomMargin: Theme.padding * 1.5 + Theme.insetBottom
                nowPlaying: page.nowPlaying
                playlist: page.backend.playlist
                collapse: 1 - page.progress
                // The bar, in the panel's coordinates: the page's width at its bottom.
                miniRect: Qt.rect(-trackPanel.x, page.height - Theme.miniPlayerHeight - trackPanel.y, page.width, Theme.miniPlayerContent)
                // The picture stays as large as it gets: scaled, not decoded anew
                // at every size on the way.
                coverMaxSide: Math.round(trackPanel._aboveSide)
                overAura: true
                // In the light theme there's only a light background until the
                // picture is in.
                inkColor: page.progress > 0 || miniAura.ready ? "#FFFFFF" : Theme.ink
                inkSecondaryColor: page.progress > 0 || miniAura.ready ? "#D9FFFFFF" : Theme.inkSecondary
                // Only when open, and not while a finger takes it down.
                swipeEnabled: page.swipeEnabled && page.progress >= 1 && !swipe.active
                neighbors: page.backend.mainScreen
                swipeSurface: surface
                preloadFractions: page.backend.mainScreen ? page.backend.mainScreen.preloadFractions : []
                // In the header, as are the modes under the seek bar: the title
                // has the width.
                showMenu: false
                modesUnderSeek: true
                // Larger than on a head unit: the title and the artist have the width.
                titleSizeAbove: Theme.dp(24)
                artistSizeAbove: Theme.dp(18)
            }
        }
    }
}
