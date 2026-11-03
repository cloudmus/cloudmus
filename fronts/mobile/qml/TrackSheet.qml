import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// One track in detail, and what can be done with it, from any list: a sheet
// rising from the bottom on a phone, a panel from the right on a head unit,
// where a sheet wouldn't fit under its short screen.
Popup {
    id: sheet

    property QtObject backend
    readonly property QtObject track: backend ? backend.trackScreen : null
    readonly property bool wide: parent && parent.width > parent.height * 1.1
    // The list of playlists, in place of the actions.
    property bool choosingPlaylist: false

    parent: Overlay.overlay
    modal: true
    dim: true
    padding: 0
    width: wide ? Math.min(parent.width * 0.5, Theme.dp(460)) : parent.width
    height: wide ? parent.height : Math.min(parent.height * 0.86, content.implicitHeight + Theme.padding * 2)
    // Slid out by this share of its size. Not x and y themselves: an animation
    // would replace their bindings, and the sheet's height comes in later.
    property real slide: 1
    x: wide ? parent.width - width * (1 - slide) : 0
    y: wide ? 0 : parent.height - height * (1 - slide)
    onClosed: choosingPlaylist = false

    enter: Transition {
        NumberAnimation {
            property: "slide"
            from: 1
            to: 0
            duration: Theme.durationPage
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "slide"
            to: 1
            duration: Theme.durationPage
            easing.type: Easing.InCubic
        }
    }

    // What's behind goes black, and fades in and out with the sheet.
    Overlay.modal: Rectangle {
        color: "#80000000"
        Behavior on opacity { NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic } }
    }

    background: Rectangle {
        color: Theme.surface100
        radius: sheet.wide ? 0 : Theme.dp(16)

        // Its shadow on what's behind: from its open edge, fading out. A
        // gradient, not a blur: it's only a strip, and it costs nothing per frame.
        Rectangle {
            readonly property real depth: Theme.dp(28)
            // Turned so that the gradient runs away from the edge.
            width: sheet.wide ? parent.height : parent.width
            height: depth
            transformOrigin: Item.TopLeft
            rotation: sheet.wide ? -90 : 0
            x: sheet.wide ? -depth : 0
            y: sheet.wide ? parent.height : -depth
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.6; color: "#40000000" }
                GradientStop { position: 1.0; color: "#A6000000" }
            }
        }
    }

    component Action: ItemDelegate {
        id: action
        property string iconName
        property color tint: Theme.ink
        property bool keepsSheetOpen: false
        Layout.fillWidth: true
        implicitHeight: Theme.touch + Theme.dp(4)
        background: Rectangle { color: action.down ? Theme.surface300 : "transparent" }
        onClicked: if (!keepsSheetOpen) sheet.close()
        contentItem: RowLayout {
            spacing: Theme.dp(16)
            IconImage {
                source: "qrc:/icons/" + action.iconName + ".svg"
                sourceSize.width: Theme.dp(22)
                sourceSize.height: Theme.dp(22)
                color: action.tint
            }
            Text {
                Layout.fillWidth: true
                text: action.text
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
        }
    }

    component Fact: RowLayout {
        property alias label: name.text
        property alias value: fact.text
        visible: fact.text !== ""
        Layout.fillWidth: true
        Text {
            id: name
            color: Theme.inkTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
        }
        Text {
            id: fact
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            color: Theme.inkSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }

    contentItem: Flickable {
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.padding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            x: Theme.padding
            y: Theme.padding
            width: parent.width - Theme.padding * 2
            spacing: Theme.dp(4)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.dp(14)
                Cover {
                    Layout.preferredWidth: Theme.dp(96)
                    Layout.preferredHeight: Theme.dp(96)
                    radius: Theme.dp(8)
                    source: sheet.track ? sheet.track.coverUrl : ""
                    seed: sheet.track ? sheet.track.seed : ""
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)
                    Text {
                        Layout.fillWidth: true
                        text: sheet.track ? sheet.track.title : ""
                        color: Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: sheet.track ? sheet.track.artist : ""
                        color: Theme.inkSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: Theme.dp(6)
                        IconImage {
                            visible: sheet.track && sheet.track.liked === true
                            source: "qrc:/icons/favorite.svg"
                            sourceSize.width: Theme.dp(16)
                            sourceSize.height: Theme.dp(16)
                            color: Theme.accent
                        }
                        IconImage {
                            visible: sheet.track && sheet.track.disliked === true
                            source: "qrc:/icons/heart_off.svg"
                            sourceSize.width: Theme.dp(16)
                            sourceSize.height: Theme.dp(16)
                            color: Theme.inkTertiary
                        }
                        Text {
                            visible: sheet.track && sheet.track.isExplicit === true
                            text: qsTr("Explicit")
                            color: Theme.inkTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.dp(8)
                Layout.bottomMargin: Theme.dp(8)
                spacing: Theme.dp(2)
                Fact { label: qsTr("Album"); value: sheet.track ? sheet.track.album : "" }
                Fact {
                    label: qsTr("Duration")
                    value: sheet.track && sheet.track.durationMs > 0 ? Theme.formatTime(sheet.track.durationMs) : ""
                }
                Fact { label: qsTr("Quality"); value: sheet.track ? sheet.track.quality : "" }
                Fact { label: qsTr("Source"); value: sheet.track ? sheet.track.sourceName : "" }
                Fact {
                    label: qsTr("Last played")
                    value: sheet.track && Theme.formatPlayedAt(sheet.track.lastPlayedAt) || qsTr("Never")
                }
            }

            // What can be done with it.
            ColumnLayout {
                Layout.fillWidth: true
                visible: !sheet.choosingPlaylist
                spacing: 0
                Action {
                    visible: sheet.track && sheet.track.canPlay === true
                    iconName: "play_arrow"
                    text: qsTr("Play")
                    onClicked: sheet.track.play()
                }
                Action {
                    iconName: "playlist_play"
                    text: qsTr("Play next")
                    onClicked: sheet.track.playNext()
                }
                Action {
                    iconName: "playlist_add"
                    text: qsTr("Add to queue")
                    onClicked: sheet.track.addToQueue()
                }
                Action {
                    visible: sheet.track && sheet.track.canEditPlaylists === true
                    iconName: "playlist_add"
                    text: qsTr("Playlists…")
                    // The playlists come in the actions' place.
                    keepsSheetOpen: true
                    onClicked: {
                        sheet.choosingPlaylist = true
                        sheet.track.loadPlaylists()
                    }
                }
                Action {
                    visible: sheet.track && sheet.track.canLike === true
                    iconName: sheet.track && sheet.track.liked ? "favorite" : "favorite_border"
                    tint: sheet.track && sheet.track.liked ? Theme.accent : Theme.ink
                    text: sheet.track && sheet.track.liked ? qsTr("Unlike") : qsTr("Like")
                    onClicked: sheet.track.toggleLiked()
                }
                Action {
                    visible: sheet.track && sheet.track.canDislike === true
                    iconName: sheet.track && sheet.track.disliked ? "heart_off" : "heart_off_outline"
                    tint: sheet.track && sheet.track.disliked ? Theme.accent : Theme.ink
                    text: sheet.track && sheet.track.disliked ? qsTr("Remove dislike") : qsTr("Dislike")
                    onClicked: sheet.track.toggleDisliked()
                }
                Action {
                    visible: sheet.track && sheet.track.canStartRadio === true
                    iconName: "radio"
                    text: qsTr("Start radio from this track")
                    onClicked: sheet.track.startRadio(qsTr("Radio: %1").arg(sheet.track.title))
                }
                Action {
                    visible: sheet.track && sheet.track.webUrl !== ""
                    iconName: "open_in_new"
                    text: qsTr("Open track page")
                    onClicked: Qt.openUrlExternally(sheet.track.webUrl)
                }
                Action {
                    visible: sheet.track && sheet.track.canDownload === true
                    iconName: "file_download"
                    text: qsTr("Save to downloads")
                    onClicked: sheet.track.download()
                }
            }

            // The user's own playlists, which hold it or not.
            ColumnLayout {
                Layout.fillWidth: true
                visible: sheet.choosingPlaylist
                spacing: 0
                IconButton {
                    iconName: "arrow_back"
                    onClicked: sheet.choosingPlaylist = false
                }
                Spinner {
                    Layout.alignment: Qt.AlignHCenter
                    running: sheet.track && sheet.track.playlistsLoading === true
                }
                BodyText {
                    visible: sheet.track && sheet.track.playlistsFailed === true
                    text: qsTr("Couldn't load playlists")
                }
                Repeater {
                    model: sheet.backend ? sheet.backend.trackPlaylists : null
                    delegate: CheckDelegate {
                        id: choice
                        required property var playlistId
                        required property var title
                        required property var contains
                        required property var changing
                        Layout.fillWidth: true
                        text: choice.title || ""
                        checked: choice.contains === true
                        enabled: choice.changing !== true
                        onClicked: sheet.track.setInPlaylist(choice.playlistId, checked)
                        contentItem: Text {
                            text: choice.text
                            color: Theme.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
