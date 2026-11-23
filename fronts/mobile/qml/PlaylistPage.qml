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
    signal downloadsRequested()
    // A subfolder, opened at the next depth: Main pushes its page.
    signal folderRequested(int depth)

    // Where it is in the stack of Playlist pages: 0 the one the Library opens,
    // each subfolder one more. Each depth has a screen of its own, so a page
    // under another keeps its rows and its place in them.
    property int depth: 0
    readonly property QtObject screen: {
        backend.levelsRevision
        return backend.playlistScreenAt(page.depth)
    }
    readonly property QtObject entriesModel: {
        backend.levelsRevision
        return backend.openEntriesAt(page.depth)
    }
    // How many of its first rows are subfolders; the tracks follow.
    readonly property int folderCount: page.screen !== null && page.screen.folderCount !== undefined
        ? page.screen.folderCount : 0
    // The next depth's, asked for ahead, so a subfolder's page has it at once.
    Component.onCompleted: backend.prepareLevel(page.depth + 1)
    // Off the stack, however it left (its back button, the system's Back, the
    // stack cleared): its screen lets go of the rows. After the transition,
    // so it doesn't empty on its way out.
    StackView.onRemoved: if (backend.playlistScreens) backend.playlistScreens.close(page.depth)

    // Back lets go of picked rows first; true if it did (the page stays).
    // Main's stack asks before it pops.
    function backInside() {
        if (page.selecting) {
            page.selection = []
            return true
        }
        return false
    }
    // Its source went away, or it was deleted: nothing left to show.
    Connections {
        target: page.screen
        ignoreUnknownSignals: true
        function onClosed() { page.closeRequested() }
    }
    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property bool wide: width > height * 1.1

    background: Rectangle { color: Theme.surface0 }
    // The header's picture goes on up under the status bar, which is see-through.
    readonly property bool auraUnderStatusBar: true
    readonly property bool auraSpansStatusBar: true

    // The star and Save all.
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
        confirm.ask(qsTr("Delete %n track(s)?", "", rows.length), "", qsTr("Delete"))
    }
    function confirmDeleteFolder() {
        confirm.onYes = () => page.screen.deletePlaylist()
        confirm.ask(qsTr("Delete \"%1\" with %n track(s)?", "", page.screen.count).arg(page.screen.title),
            "", qsTr("Delete"))
    }

    readonly property bool loading: page.screen !== null && page.screen.loading === true
    readonly property bool canStar: page.screen !== null && page.screen.isHistory === false
    readonly property bool canDownload: page.screen !== null && page.screen.canDownload === true
    readonly property bool downloading: page.screen !== null && page.screen.downloading === true
    // Without any of them (History), none of the room they'd take.
    readonly property bool canDeleteFolder: page.screen !== null && page.screen.canDeletePlaylist === true
    readonly property bool hasHeaderActions: canStar || canDownload || downloading || canDeleteFolder

    component HeaderActions: RowLayout {
        property int buttonSize: Theme.touch
        spacing: 0
        IconButton {
            visible: page.canStar
            size: parent.buttonSize
            iconName: page.screen && page.screen.isFavorite ? "star" : "star_border"
            color: "#FFFFFF"
            onClicked: page.screen.toggleFavorite()
        }
        // A tap saves it; while it's being saved the button rings with its
        // progress, and a tap goes to the Downloads page, where it can be
        // stopped.
        DownloadButton {
            objectName: "downloadPlaylist"
            visible: page.canDownload || page.downloading
            size: parent.buttonSize
            color: "#FFFFFF"
            ringColor: "#FFFFFF"
            downloading: page.downloading
            progress: page.screen !== null ? page.screen.downloadProgress : -1
            onClicked: {
                if (page.downloading) {
                    page.downloadsRequested()
                    return
                }
                page.screen.downloadAll()
                page.platform.requestNotificationPermission()
            }
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
                    icon: page.screen ? page.screen.coverIcon : ""
                    seed: page.screen ? page.screen.title : ""
                    // Loading shows on the cover: the buttons keep their places
                    // and Play stays readable as Play.
                    Rectangle {
                        objectName: "loadingCover"
                        anchors.fill: parent
                        radius: parent.radius
                        z: 3
                        visible: page.loading
                        color: "#33000000"
                        Spinner {
                            anchors.centerIn: parent
                            width: Math.round(parent.width * 0.8)
                            height: width
                            running: page.loading
                            lineWidth: Theme.dp(3)
                            color: "#FFFFFF"
                        }
                    }
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
                            for (let row = 0; row < list.count - page.folderCount; ++row)
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
            // The subfolders (each with everything under it), then the tracks:
            // one lazy list, so a folder with hundreds of subfolders builds
            // only the rows in view.
            model: page.entriesModel
            cacheBuffer: Theme.rowHeight * 6
            delegate: Item {
                id: entry
                required property int index
                required property var model
                // By place, not by a role: a replica's row is empty until it
                // is fetched, and a choice made on that would stay.
                readonly property bool isFolder: entry.index < page.folderCount
                // The row among the tracks.
                readonly property int trackIndex: entry.index - page.folderCount
                width: ListView.view ? ListView.view.width : 0
                height: rowLoader.item ? rowLoader.item.height : Theme.rowHeight
                Loader {
                    id: rowLoader
                    width: parent.width
                    sourceComponent: entry.isFolder ? folderRow : trackRow
                }
                Component {
                    id: folderRow
                    ItemDelegate {
                        id: folder
                        objectName: "folder_" + (entry.model.playlistId || "")
                        height: Theme.rowHeight
                        topPadding: 0
                        bottomPadding: 0
                        leftPadding: Theme.padding
                        rightPadding: Theme.dp(8)
                        onClicked: {
                            page.backend.playlistScreens.openFolder(page.depth, entry.model.playlistId)
                            page.folderRequested(page.depth + 1)
                        }
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
                                source: entry.model.coverUrl || ""
                                icon: entry.model.coverIcon || ""
                                seed: entry.model.title || ""
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.dp(2)
                                Text {
                                    Layout.fillWidth: true
                                    text: entry.model.title || ""
                                    color: Theme.ink
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 tracks").arg(entry.model.trackCount)
                                        + (entry.model.sizeText ? " · " + entry.model.sizeText : "")
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
                Component {
                    id: trackRow
                    TrackRow {
                        index: entry.trackIndex
                        title: entry.model.title
                        artist: entry.model.artist
                        durationMs: entry.model.durationMs
                        coverUrl: entry.model.coverUrl
                        isCurrent: entry.model.isCurrent
                        cached: entry.model.cached
                        downloadProgress: entry.model.downloadProgress
                        liked: entry.model.liked
                        disliked: entry.model.disliked
                        unavailable: entry.model.unavailable
                        playedAt: entry.model.playedAt
                        playing: page.nowPlaying && page.nowPlaying.playing === true
                        showMenu: true
                        selectable: page.screen && page.screen.canDeleteTracks === true
                        selected: page.selection.indexOf(entry.trackIndex) >= 0
                        onSelectRequested: page.toggleRow(entry.trackIndex)
                        onClicked: {
                            if (page.selecting)
                                page.toggleRow(entry.trackIndex)
                            else
                                page.screen.playRow(entry.trackIndex)
                        }
                        onMenuRequested: {
                            page.backend.trackScreen.showOpenRowAt(page.depth, entry.trackIndex)
                            page.trackMenuRequested()
                        }
                    }
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
