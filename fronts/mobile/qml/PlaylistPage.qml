import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
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

    // Back from a folder opened inside another goes to that one; true if it
    // did (the page stays). Main's stack asks before it pops.
    function backInside() {
        if (page.selecting) {
            page.selection = []
            return true
        }
        if (!page.screen || page.screen.canGoBack !== true)
            return false
        page.screen.goBack()
        return true
    }
    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property bool wide: width > height * 1.1

    background: Rectangle { color: Theme.surface0 }
    // The header's picture goes on up under the status bar, which is see-through.
    readonly property bool auraUnderStatusBar: true
    readonly property bool auraSpansStatusBar: true

    // The loading spinner, the star and Save all.
    // Rows picked with a long press (of the filtered rows), to delete them
    // together; any change to the rows lets go of them.
    property var selection: []
    readonly property bool selecting: selection.length > 0
    function toggleRow(row) {
        const picked = page.selection.slice()
        const at = picked.indexOf(row)
        if (at >= 0)
            picked.splice(at, 1)
        else
            picked.push(row)
        page.selection = picked
    }
    Connections {
        target: page.screen
        function onCountChanged() { page.selection = [] }
    }

    ConfirmDialog {
        id: confirm
        objectName: "deleteConfirm"
        property var onYes: null
        onAccepted: if (onYes) onYes()
    }
    function confirmDeleteRows() {
        const rows = page.selection.slice()
        confirm.onYes = () => { page.screen.deleteRows(rows); page.selection = [] }
        confirm.ask(qsTr("Delete %n track(s)?", "", rows.length),
            qsTr("The files are deleted from the device and can't be brought back."), qsTr("Delete"))
    }
    function confirmDeleteFolder() {
        confirm.onYes = () => page.screen.deletePlaylist()
        confirm.ask(qsTr("Delete \"%1\" and everything in it?").arg(page.screen.title),
            qsTr("Its %n track(s) are deleted from the device and can't be brought back.", "", page.screen.count),
            qsTr("Delete"))
    }

    readonly property bool loading: page.screen !== null && page.screen.loading === true
    readonly property bool canStar: page.screen !== null && page.screen.isHistory === false
    readonly property bool canDownload: page.screen !== null && page.screen.canDownload === true
    // Without any of them (History), none of the room they'd take.
    readonly property bool canDeleteFolder: page.screen !== null && page.screen.canDeletePlaylist === true
    readonly property bool hasHeaderActions: loading || canStar || canDownload || canDeleteFolder

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
        IconButton {
            objectName: "deleteFolder"
            visible: page.canDeleteFolder
            size: parent.buttonSize
            iconName: "delete"
            color: "#FFFFFF"
            onClicked: page.confirmDeleteFolder()
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
                    onClicked: {
                        if (!page.backInside())
                            page.closeRequested()
                    }
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
                        text: page.screen ? qsTr("%1 tracks").arg(page.screen.count)
                              + (page.screen.sizeText ? " · " + page.screen.sizeText : "") : ""
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
            // Over it while rows are picked: how many, and what to do with them.
            Rectangle {
                objectName: "selectionBar"
                anchors.fill: parent
                anchors.topMargin: -Theme.insetTop
                visible: page.selecting
                color: Theme.surface200
                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: Theme.insetTop
                    anchors.margins: Theme.dp(8)
                    spacing: Theme.dp(8)
                    IconButton {
                        iconName: "close"
                        onClicked: page.selection = []
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%n selected", "", page.selection.length)
                        color: Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    TextButton {
                        objectName: "selectAll"
                        text: qsTr("All")
                        onClicked: {
                            const all = []
                            for (let row = 0; row < list.count; ++row)
                                all.push(row)
                            page.selection = all
                        }
                    }
                    IconButton {
                        objectName: "deleteSelected"
                        iconName: "delete"
                        color: Theme.danger
                        onClicked: page.confirmDeleteRows()
                    }
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
            // The playlists inside this one: a folder's subfolders, each with
            // everything under it.
            header: Column {
                width: ListView.view ? ListView.view.width : 0
                Repeater {
                    model: page.backend.openFolders
                    delegate: ItemDelegate {
                        id: folder
                        required property var playlistId
                        required property var title
                        required property var trackCount
                        required property var sizeText
                        required property var coverUrl
                        objectName: "folder_" + folder.playlistId
                        width: parent ? parent.width : 0
                        height: Theme.rowHeight
                        topPadding: 0
                        bottomPadding: 0
                        leftPadding: Theme.padding
                        rightPadding: Theme.dp(8)
                        onClicked: page.screen.openFolder(folder.playlistId)
                        background: Rectangle {
                            color: folder.down && folderRipple.flat ? Theme.surface200 : "transparent"
                            Ripple {
                                id: folderRipple
                                pressed: folder.pressed
                                pressX: folder.pressX
                                pressY: folder.pressY
                            }
                        }
                        contentItem: RowLayout {
                            spacing: Theme.dp(12)
                            Cover {
                                Layout.preferredWidth: Theme.dp(48)
                                Layout.preferredHeight: Theme.dp(48)
                                source: folder.coverUrl || ""
                                seed: folder.title || ""
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.dp(2)
                                Text {
                                    Layout.fillWidth: true
                                    text: folder.title || ""
                                    color: Theme.ink
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 tracks").arg(folder.trackCount) + (folder.sizeText ? " · " + folder.sizeText : "")
                                    color: Theme.inkSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                            IconImage {
                                source: "qrc:/icons/chevron_right.svg"
                                sourceSize: Qt.size(Theme.dp(22), Theme.dp(22))
                                color: Theme.inkTertiary
                            }
                        }
                    }
                }
            }
            delegate: TrackRow {
                playing: page.nowPlaying && page.nowPlaying.playing === true
                showMenu: true
                selectable: page.screen && page.screen.canDeleteTracks === true
                selected: page.selection.indexOf(index) >= 0
                onSelectRequested: page.toggleRow(index)
                onClicked: {
                    if (page.selecting)
                        page.toggleRow(index)
                    else
                        page.screen.playRow(index)
                }
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
