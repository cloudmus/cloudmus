import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The track that plays, big: cover, title, position and the transport.
// Landscape (a head unit) puts the cover left of the controls, portrait (a
// phone) above them. Closed with the arrow, Back, or a swipe down.
Page {
    id: page

    property QtObject backend
    property QtObject platform
    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property bool wide: width > height * 1.1
    signal closeRequested()

    background: Rectangle {
        color: Theme.surface0

        // The cover, blown up and faded, behind everything.
        Image {
            anchors.fill: parent
            source: page.nowPlaying ? page.nowPlaying.coverUrl : ""
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 64
            opacity: 0.18
            smooth: true
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.surface0 }
            }
        }
    }

    header: Item {
        implicitHeight: Theme.barHeight
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(4)
            anchors.rightMargin: Theme.padding
            IconButton {
                iconName: "expand_more"
                iconSize: Theme.dp(32)
                onClicked: page.closeRequested()
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Now playing")
                horizontalAlignment: Text.AlignHCenter
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.dp(1)
            }
            // Balances the arrow, keeping the title centered. No "stop and
            // close" here: next to "collapse" it read as closing the page,
            // and in a car a slip would cut the music (it's in the
            // notification and in Settings).
            Item { implicitWidth: Theme.touch }
        }
    }

    // A swipe down closes the page, following the finger until let go.
    DragHandler {
        id: swipe
        target: null
        xAxis.enabled: false
        onActiveChanged: {
            if (!active && translation.y > page.height * 0.18)
                page.closeRequested()
        }
    }
    transform: Translate { y: swipe.active ? Math.max(0, swipe.translation.y) : 0 }

    GridLayout {
        anchors.fill: parent
        anchors.margins: Theme.padding * 1.5
        anchors.topMargin: Theme.padding / 2
        flow: page.wide ? GridLayout.LeftToRight : GridLayout.TopToBottom
        columns: page.wide ? 2 : 1
        columnSpacing: Theme.padding * 2
        rowSpacing: Theme.padding * 1.5

        Item {
            Layout.fillWidth: !page.wide
            Layout.fillHeight: true
            Layout.preferredWidth: page.wide ? Math.min(parent.height, parent.width * 0.48) : -1
            Layout.minimumHeight: Theme.dp(120)

            Cover {
                id: cover
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height)
                height: width
                source: page.nowPlaying ? page.nowPlaying.coverUrl : ""
                // A slight breath while playing.
                scale: page.nowPlaying && page.nowPlaying.playing ? 1.0 : 0.92
                Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: page.wide
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.padding

            Item { Layout.fillHeight: true; visible: page.wide }

            Text {
                Layout.fillWidth: true
                text: page.nowPlaying ? page.nowPlaying.title : ""
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeadline
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: -Theme.dp(10)
                text: page.nowPlaying ? page.nowPlaying.artist : ""
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                elide: Text.ElideRight
            }

            SeekBar {
                Layout.fillWidth: true
                positionMs: page.nowPlaying ? page.nowPlaying.positionMs : 0
                durationMs: page.nowPlaying ? page.nowPlaying.durationMs : 0
                onSeekRequested: (positionMs) => page.nowPlaying.seek(positionMs)
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.dp(28)
                IconButton {
                    iconName: "skip_previous"
                    size: Theme.touchLarge
                    iconSize: Theme.dp(34)
                    onClicked: page.nowPlaying.previous()
                }
                IconButton {
                    iconName: page.nowPlaying && page.nowPlaying.playing ? "pause" : "play_arrow"
                    filled: true
                    size: Theme.dp(80)
                    iconSize: Theme.dp(44)
                    onClicked: page.nowPlaying.togglePause()

                    BusyIndicator {
                        anchors.fill: parent
                        anchors.margins: -Theme.dp(4)
                        running: page.nowPlaying && page.nowPlaying.loading
                        visible: running
                    }
                }
                IconButton {
                    iconName: "skip_next"
                    size: Theme.touchLarge
                    iconSize: Theme.dp(34)
                    onClicked: page.nowPlaying.next()
                }
            }

            Item { Layout.fillHeight: true; visible: page.wide }
        }
    }
}
