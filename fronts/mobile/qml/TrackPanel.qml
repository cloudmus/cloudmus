import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The track that plays, big: cover, title, position and the transport. With
// room to the side (`sideBySide`) the cover is left of the rest.
GridLayout {
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

    flow: sideBySide ? GridLayout.LeftToRight : GridLayout.TopToBottom
    columns: sideBySide ? 2 : 1
    columnSpacing: Theme.padding * 2
    rowSpacing: Theme.padding

    Item {
        Layout.fillWidth: !panel.sideBySide
        Layout.fillHeight: true
        Layout.preferredWidth: panel.sideBySide ? Math.min(panel.height, panel.width * 0.48) : -1
        Layout.minimumHeight: Theme.dp(120)

        Cover {
            id: cover
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height)
            height: width
            source: panel.nowPlaying ? panel.nowPlaying.coverUrl : ""
            seed: panel.seed
            radius: Theme.dp(16)
            // A slight breath while playing.
            scale: panel.nowPlaying && panel.nowPlaying.playing ? 1.0 : 0.92
            Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: panel.sideBySide
        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.dp(8)

        Item { Layout.fillHeight: true; visible: panel.sideBySide }

        Item {
            Layout.fillWidth: true
            implicitHeight: titleRow.implicitHeight

            // A soft shade under the title and artist, for them to stand out
            // of whatever the art has there.
            Rectangle {
                visible: panel.overAura
                anchors.fill: parent
                anchors.topMargin: -Theme.dp(14)
                anchors.bottomMargin: -Theme.dp(14)
                // To the panel's edges, no further.
                anchors.leftMargin: -Theme.dp(12)
                anchors.rightMargin: -Theme.dp(12)
                radius: Theme.dp(12)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 0.3; color: "#59000000" }
                    GradientStop { position: 0.7; color: "#59000000" }
                    GradientStop { position: 1.0; color: "#00000000" }
                }
            }

            RowLayout {
                id: titleRow
                anchors.fill: parent
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)
                    Text {
                        Layout.fillWidth: true
                        text: panel.nowPlaying ? panel.nowPlaying.title : ""
                        color: panel.inkColor
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle + Theme.dp(3)
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: panel.nowPlaying ? panel.nowPlaying.artist : ""
                        color: panel.inkSecondaryColor
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
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

        Item { Layout.fillHeight: true; visible: panel.sideBySide }
    }
}
