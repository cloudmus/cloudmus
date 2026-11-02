import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

Page {
    id: page

    property QtObject backend
    signal nowPlayingRequested()
    signal settingsRequested()

    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property QtObject playlist: backend.playlist

    background: Rectangle { color: Theme.surface0 }

    header: Rectangle {
        implicitHeight: Theme.barHeight + Theme.dp(8)
        color: Theme.surface100

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.padding
            anchors.rightMargin: Theme.dp(4)
            spacing: Theme.dp(10)

            Image {
                source: "qrc:/icons/logo.svg"
                sourceSize.width: Theme.dp(30)
                sourceSize.height: Theme.dp(30)
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: "Cloudmus"
                    color: Theme.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: page.playlist ? page.playlist.title + " · " + page.playlist.count : ""
                    color: Theme.inkSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }
            }
            IconButton {
                iconName: "tune"
                onClicked: page.settingsRequested()
            }
        }
    }

    ScrollList {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        clip: true
        model: page.backend.tracks

        delegate: ItemDelegate {
            id: row
            // `var`: a replica's rows arrive empty until fetched.
            required property int index
            required property var title
            required property var artist
            required property var durationMs
            required property var coverUrl
            required property var isCurrent
            readonly property bool current: isCurrent === true

            width: ListView.view.width
            height: Theme.rowHeight + Theme.dp(8)
            onClicked: page.playlist.play(index)

            background: Rectangle {
                color: row.down ? Theme.surface300 : (row.current ? Theme.surface200 : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                Rectangle {
                    visible: row.current
                    width: Theme.dp(3)
                    height: parent.height - Theme.dp(16)
                    anchors.verticalCenter: parent.verticalCenter
                    radius: width / 2
                    color: Theme.accent
                }
            }

            contentItem: RowLayout {
                spacing: Theme.dp(14)
                Cover {
                    Layout.preferredWidth: Theme.dp(52)
                    Layout.preferredHeight: Theme.dp(52)
                    source: row.coverUrl || ""

                    // What plays now, over its cover.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        visible: row.current && page.nowPlaying && page.nowPlaying.playing
                        color: "#80000000"
                        IconImage {
                            anchors.centerIn: parent
                            source: "qrc:/icons/volume_up.svg"
                            sourceSize.width: Theme.dp(24)
                            sourceSize.height: Theme.dp(24)
                            color: Theme.accent
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)
                    Text {
                        Layout.fillWidth: true
                        text: row.title || ""
                        color: row.current ? Theme.accent : Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.artist || ""
                        color: Theme.inkSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }
                Text {
                    text: Theme.formatTime(row.durationMs)
                    color: Theme.inkTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.features: { "tnum": 1 }
                }
            }
        }
    }

    MiniPlayer {
        id: mini
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        nowPlaying: page.nowPlaying
        onOpenRequested: page.nowPlayingRequested()
    }
}
