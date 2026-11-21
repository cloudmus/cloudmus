import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A playlist opened from the Library, or History: its tracks, playing from
// them, saving it, a star for the favorites, a filter.
Page {
    id: page
    bottomPadding: Theme.insetBottom

    property QtObject backend
    property QtObject platform
    signal closeRequested()
    signal trackMenuRequested()

    readonly property QtObject screen: backend.playlistScreen
    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property bool wide: width > height * 1.1

    background: Rectangle { color: Theme.surface0 }
    // The header's picture goes on up under the status bar, which is see-through.
    readonly property bool auraUnderStatusBar: true
    readonly property bool auraSpansStatusBar: true

    // The loading spinner, the star and Save all.
    readonly property bool loading: page.screen !== null && page.screen.loading === true
    readonly property bool canStar: page.screen !== null && page.screen.isHistory === false
    readonly property bool canDownload: page.screen !== null && page.screen.canDownload === true
    // Without any of them (History), none of the room they'd take.
    readonly property bool hasHeaderActions: loading || canStar || canDownload

    component HeaderActions: RowLayout {
        property int buttonSize: Theme.touch
        spacing: 0
        Spinner {
            Layout.rightMargin: Theme.dp(6)
            running: page.loading
            color: "#FFFFFF"
        }
        IconButton {
            visible: page.canStar
            size: parent.buttonSize
            iconName: page.screen && page.screen.isFavorite ? "star" : "star_border"
            color: "#FFFFFF"
            onClicked: page.screen.toggleFavorite()
        }
        IconButton {
            visible: page.canDownload
            size: parent.buttonSize
            iconName: "file_download"
            color: "#FFFFFF"
            onClicked: page.screen.downloadAll()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The header, over art from its title as on the desktop.
        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.dp(page.wide ? 112 : 136)
            AuraBackground {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: -Theme.insetTop
                seed: page.screen ? page.screen.title : ""
                edgeFade: Theme.dp(16)
            }
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dp(8)
                spacing: Theme.dp(10)
                IconButton {
                    Layout.alignment: Qt.AlignTop
                    iconName: "arrow_back"
                    color: "#FFFFFF"
                    onClicked: page.closeRequested()
                }
                Cover {
                    Layout.preferredWidth: Theme.dp(page.wide ? 88 : 80)
                    Layout.preferredHeight: Theme.dp(page.wide ? 88 : 80)
                    source: page.screen ? page.screen.coverUrl : ""
                    seed: page.screen ? page.screen.title : ""
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)
                    ShadowText {
                        Layout.fillWidth: true
                        text: page.screen ? page.screen.title : ""
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle + Theme.dp(3)
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }
                    ShadowText {
                        Layout.fillWidth: true
                        visible: page.screen && !page.screen.isRadio
                        text: page.screen ? qsTr("%1 tracks").arg(page.screen.count) : ""
                        color: "#D9FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                    }
                    // On a phone held upright the row has no room for them
                    // beside the title: they go under it.
                    HeaderActions {
                        visible: !page.wide && page.hasHeaderActions
                        Layout.leftMargin: -Theme.dp(8)
                        buttonSize: Theme.dp(40)
                    }
                }
                HeaderActions {
                    visible: page.wide && page.hasHeaderActions
                }
                IconButton {
                    iconName: "play_arrow"
                    filled: true
                    size: Theme.touchLarge
                    enabled: page.screen && (page.screen.isRadio || page.screen.count > 0)
                    onClicked: page.screen.playAll()
                }
            }
        }

        // The filter.
        Rectangle {
            Layout.fillWidth: true
            visible: page.screen && !page.screen.isRadio
            implicitHeight: Theme.barHeight
            color: Theme.surface100
            TextField {
                selectionColor: Theme.accent
                selectedTextColor: Theme.inkOnAccent
                cursorDelegate: TextCursor { }
                id: filter
                objectName: "filter"
                anchors.fill: parent
                anchors.margins: Theme.dp(8)
                placeholderText: qsTr("Filter by title, artist or album")
                placeholderTextColor: Theme.inkTertiary
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                inputMethodHints: Qt.ImhNoPredictiveText
                background: Rectangle { radius: Theme.radius; color: Theme.surface200 }
                onTextChanged: page.screen.setFilter(text)
            }
        }

        ScrollList {
            id: list
            objectName: "playlistTracks"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.backend.openTracks
            delegate: TrackRow {
                playing: page.nowPlaying && page.nowPlaying.playing === true
                showMenu: true
                onClicked: page.screen.playRow(index)
                onMenuRequested: {
                    page.backend.trackScreen.showOpenRow(index)
                    page.trackMenuRequested()
                }
            }
            BodyText {
                parent: list
                anchors.centerIn: parent
                width: implicitWidth
                visible: list.count === 0 && page.screen && !page.screen.loading
                text: page.screen && page.screen.isRadio ? qsTr("Press play to start the station") : qsTr("No tracks")
            }
        }
    }
}
