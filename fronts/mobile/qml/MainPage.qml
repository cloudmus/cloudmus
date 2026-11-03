import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The main screen: the active playlist and the track that plays. With width
// to spare (a head unit) both are in view, the track on the driver's side; on
// a phone the list fills the screen over a mini player that opens Now Playing.
Page {
    id: page

    property QtObject backend
    signal nowPlayingRequested()
    signal settingsRequested()
    signal libraryRequested()
    signal trackMenuRequested()

    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property QtObject playlist: backend.playlist
    readonly property QtObject library: backend.library
    readonly property bool wide: width > height * 1.1
    readonly property bool hasTrack: nowPlaying && nowPlaying.hasTrack === true
    // As on the desktop: the track's, or with none playing the playlist's.
    readonly property string backgroundSeed: nowPlaying && nowPlaying.backgroundSeed ? nowPlaying.backgroundSeed
        : playlist && playlist.title ? playlist.title : ""

    background: Rectangle { color: Theme.surface0 }

    component SourceButton: AbstractButton {
        id: sourceButton
        implicitHeight: Theme.touchLarge
        focusPolicy: Qt.NoFocus
        enabled: page.library !== null
        onClicked: page.libraryRequested()
        background: Rectangle {
            radius: Theme.radius
            color: sourceButton.down ? Theme.surface300 : Theme.surface200
            opacity: page.wide ? 0.82 : 1
        }
        contentItem: RowLayout {
            spacing: Theme.dp(10)
            Item {
                Layout.leftMargin: Theme.dp(10)
                implicitWidth: Theme.dp(28)
                implicitHeight: Theme.dp(28)
                IconImage {
                    anchors.fill: parent
                    visible: page.library && page.library.activeSourceIcon !== ""
                    source: page.library ? page.library.activeSourceIcon : ""
                    sourceSize.width: width
                    sourceSize.height: height
                    color: Theme.accent
                }
                Image {
                    anchors.fill: parent
                    visible: !page.library || page.library.activeSourceIcon === ""
                    source: "qrc:/icons/logo.svg"
                    sourceSize.width: width
                    sourceSize.height: height
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: page.library && page.library.activeSourceName ? page.library.activeSourceName : "CloudMus"
                    color: Theme.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: page.playlist && page.playlist.title ? page.playlist.title + " · " + page.playlist.count : ""
                    color: Theme.inkSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
            }
            IconImage {
                Layout.rightMargin: Theme.dp(6)
                visible: page.library !== null
                source: "qrc:/icons/expand_more.svg"
                sourceSize.width: Theme.dp(24)
                sourceSize.height: Theme.dp(24)
                color: Theme.inkSecondary
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        spacing: 0

        // The track, on a head unit, over the generated background.
        Item {
            visible: page.wide
            Layout.fillHeight: true
            Layout.preferredWidth: page.width * 0.42

            AuraBackground {
                anchors.fill: parent
                visible: parent.visible
                seed: page.backgroundSeed
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.dp(12)
                spacing: Theme.dp(10)

                SourceButton {
                    Layout.fillWidth: true
                }
                TrackPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    nowPlaying: page.nowPlaying
                    overAura: true
                    showMenu: page.backend.trackScreen !== null
                    onMenuRequested: {
                        page.backend.trackScreen.showPlaying()
                        page.trackMenuRequested()
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // The bar over the list.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.barHeight + Theme.dp(16)
                color: Theme.surface100
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(8)
                    anchors.rightMargin: Theme.dp(4)
                    spacing: Theme.dp(8)
                    SourceButton {
                        visible: !page.wide
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: page.wide
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.dp(8)
                        text: page.playlist ? page.playlist.title : ""
                        color: Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    IconButton {
                        iconName: "tune"
                        onClicked: page.settingsRequested()
                    }
                }
            }

            ScrollList {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: page.backend.tracks
                delegate: TrackRow {
                    playing: page.nowPlaying && page.nowPlaying.playing === true
                    showMenu: page.backend.trackScreen !== null
                    onClicked: page.playlist.playRow(index)
                    onMenuRequested: {
                        page.backend.trackScreen.showActiveRow(index)
                        page.trackMenuRequested()
                    }
                }

                // Nothing active yet: the way to the Library.
                Column {
                    anchors.centerIn: parent
                    visible: list.count === 0 && page.library !== null
                    spacing: Theme.padding
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Nothing is playing")
                        color: Theme.inkSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }
                    TextButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Open the Library")
                        onClicked: page.libraryRequested()
                    }
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
        seed: page.backgroundSeed
        visible: !page.wide && page.hasTrack
        onOpenRequested: page.nowPlayingRequested()
    }
}
