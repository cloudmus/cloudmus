import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The track that plays, big: cover, title, position and the transport. With
// room to the side (`sideBySide`, or `progress` on the way there) the cover is
// left of the rest.
Item {
    id: panel

    property QtObject nowPlaying
    property bool sideBySide: false
    // Over the generated background, which is dark in either theme.
    property bool overAura: false
    property bool showMenu: false
    signal menuRequested()
    property color inkColor: overAura ? "#FFFFFF" : Theme.ink
    property color inkSecondaryColor: overAura ? "#D9FFFFFF" : Theme.inkSecondary
    readonly property string seed: nowPlaying && nowPlaying.backgroundSeed ? nowPlaying.backgroundSeed : ""

    // 0 the cover above the rest .. 1 beside it: animating this moves them from
    // one place to the other, which a layout can't.
    property real progress: sideBySide ? 1 : 0
    // Of the width, what the cover may take when beside the rest.
    property real sideCoverShare: 0.48
    // Of the height, likewise.
    property real sideCoverHeightShare: 1

    readonly property real _gap: Theme.padding
    readonly property real _detailsHeight: details.implicitHeight
    readonly property real _aboveSide: Math.max(Theme.dp(120), Math.min(width, height - _detailsHeight - _gap))
    // As large as there's room for, leaving the rest the width it needs.
    readonly property real _besideSide: Math.max(Theme.dp(120),
        Math.min(height * sideCoverHeightShare, width * sideCoverShare, width - _gap * 2 - details.implicitWidth))
    readonly property real _besideDetailsX: _besideSide + _gap * 2

    readonly property int _textDirection: nowPlaying && nowPlaying.trackDirection === -1 ? -1 : 1

    function _mix(from, to) { return from + (to - from) * progress }

    Cover {
        id: cover
        objectName: "trackCover"
        x: panel._mix((panel.width - panel._aboveSide) / 2, 0)
        y: panel._mix((panel.height - panel._detailsHeight - panel._gap - panel._aboveSide) / 2,
                      (panel.height - panel._besideSide) / 2)
        width: panel._mix(panel._aboveSide, panel._besideSide)
        height: width
        source: panel.nowPlaying ? panel.nowPlaying.coverUrl : ""
        seed: panel.seed
        radius: Theme.dp(16)
        liveCorners: true
        zoomOnChange: true
        // A slight breath while playing.
        scale: panel.nowPlaying && panel.nowPlaying.playing ? 1.0 : 0.92
        Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }
    }

    ColumnLayout {
        id: details
        objectName: "trackDetails"
        x: panel._mix(0, panel._besideDetailsX)
        y: panel._mix(panel.height - panel._detailsHeight, (panel.height - panel._detailsHeight) / 2)
        width: panel._mix(panel.width, panel.width - panel._besideDetailsX)
        spacing: Theme.dp(8)

        Item {
            Layout.fillWidth: true
            implicitHeight: titleRow.implicitHeight

            RowLayout {
                id: titleRow
                anchors.fill: parent
                TrackTitleBlock {
                    objectName: "trackTitle"
                    Layout.fillWidth: true
                    title: panel.nowPlaying ? panel.nowPlaying.title : ""
                    artist: panel.nowPlaying ? panel.nowPlaying.artist : ""
                    direction: panel._textDirection
                    titleColor: panel.inkColor
                    artistColor: panel.inkSecondaryColor
                    // Larger with the room there is beside the cover.
                    titleSize: Math.round(panel._mix(Theme.fontTitle + Theme.dp(3), Theme.dp(28)))
                    artistSize: Math.round(panel._mix(Theme.fontBody, Theme.dp(19)))
                }
                // What a queue does: not on the dev tracks, which don't have them.
                IconButton {
                    objectName: "shuffleButton"
                    visible: panel.nowPlaying && panel.nowPlaying.repeat !== undefined
                    enabled: panel.nowPlaying && panel.nowPlaying.radio !== true
                    iconName: "shuffle"
                    color: panel.nowPlaying && panel.nowPlaying.shuffle === true ? Theme.accent : panel.inkColor
                    onClicked: panel.nowPlaying.toggleShuffle()
                }
                IconButton {
                    objectName: "repeatButton"
                    visible: panel.nowPlaying && panel.nowPlaying.repeat !== undefined
                    iconName: panel.nowPlaying && panel.nowPlaying.repeat === 2 ? "repeat_one" : "repeat"
                    color: panel.nowPlaying && panel.nowPlaying.repeat > 0 ? Theme.accent : panel.inkColor
                    onClicked: panel.nowPlaying.cycleRepeat()
                }
                IconButton {
                    visible: panel.showMenu && panel.nowPlaying && panel.nowPlaying.hasTrack === true
                    iconName: "more_vert"
                    color: panel.inkColor
                    onClicked: panel.menuRequested()
                }
            }
        }

        SeekBar {
            Layout.fillWidth: true
            positionMs: panel.nowPlaying ? panel.nowPlaying.positionMs : 0
            durationMs: panel.nowPlaying ? panel.nowPlaying.durationMs : 0
            bufferedMs: panel.nowPlaying && panel.nowPlaying.bufferedMs !== undefined ? panel.nowPlaying.bufferedMs : -1
            inkColor: panel.inkColor
            inkSecondaryColor: panel.inkSecondaryColor
            overAura: panel.overAura
            troughColor: panel.overAura ? "#40FFFFFF" : Theme.surface400
            onSeekRequested: (positionMs) => panel.nowPlaying.seek(positionMs)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.dp(10)
            IconButton {
                visible: panel.nowPlaying && panel.nowPlaying.dislikeSupported === true
                iconName: panel.nowPlaying && panel.nowPlaying.disliked ? "heart_off" : "heart_off_outline"
                color: panel.inkColor
                onClicked: panel.nowPlaying.setDisliked(!panel.nowPlaying.disliked)
            }
            IconButton {
                iconName: "skip_previous"
                color: panel.inkColor
                size: Theme.touchLarge
                iconSize: Theme.dp(34)
                onClicked: panel.nowPlaying.previous()
            }
            IconButton {
                iconName: panel.nowPlaying && panel.nowPlaying.playing ? "pause" : "play_arrow"
                filled: true
                size: Theme.dp(72)
                iconSize: Theme.dp(40)
                onClicked: panel.nowPlaying.togglePause()

                Spinner {
                    anchors.fill: parent
                    anchors.margins: -Theme.dp(4)
                    running: panel.nowPlaying && panel.nowPlaying.loading === true
                    // Around the accent button, in what stands out from it.
                    color: panel.inkColor
                }
            }
            IconButton {
                iconName: "skip_next"
                color: panel.inkColor
                size: Theme.touchLarge
                iconSize: Theme.dp(34)
                onClicked: panel.nowPlaying.next()
            }
            IconButton {
                visible: panel.nowPlaying && panel.nowPlaying.likeSupported === true
                iconName: panel.nowPlaying && panel.nowPlaying.liked ? "favorite" : "favorite_border"
                color: panel.nowPlaying && panel.nowPlaying.liked ? Theme.accent : panel.inkColor
                onClicked: panel.nowPlaying.setLiked(!panel.nowPlaying.liked)
            }
        }
    }
}
