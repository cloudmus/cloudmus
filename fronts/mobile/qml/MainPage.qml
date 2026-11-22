import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The main screen: the active playlist and the track that plays. With width
// to spare (a head unit) both are in view, the track on the driver's side, and
// a button turns the screen over to the track: the list narrows to its covers,
// the source's bar folds into a button, the cover grows beside the rest. On a
// phone the list fills the screen over a mini player that grows into Now
// Playing (NowPlayingPage).
Page {
    id: page

    property QtObject backend
    property QtObject platform
    // Off under what's over the page, for a swipe there not to reach here.
    property bool gesturesEnabled: true
    // On a phone, how far Now Playing is up: 0 just the mini player (Main.qml).
    property real nowPlayingProgress: 0
    // The track's side of a head unit's screen goes on up under the status bar,
    // which is see-through.
    readonly property bool auraUnderStatusBar: wide
    readonly property bool auraSpansStatusBar: false
    signal nowPlayingRequested()
    signal nowPlayingCloseRequested()
    // A finger moving it: where it'd be (0 .. 1); let go, at what speed upwards,
    // and whether it had been open when taken.
    signal nowPlayingDragged(real progress)
    signal nowPlayingDragEnded(real velocity, bool fromOpen)
    signal settingsRequested()
    signal libraryRequested()
    signal trackMenuRequested()

    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property QtObject playlist: backend.playlist
    readonly property QtObject library: backend.library
    readonly property bool wide: width > height * 1.1
    readonly property bool hasTrack: nowPlaying && nowPlaying.hasTrack === true
    // A phone's, at the bottom.
    readonly property bool _miniShown: !wide && hasTrack
    // As on the desktop: the track's, or with none playing the playlist's.
    readonly property string backgroundSeed: nowPlaying && nowPlaying.backgroundSeed ? nowPlaying.backgroundSeed
        : playlist && playlist.title ? playlist.title : ""

    // 0 the normal screen .. 1 the track's: everything that moves follows this.
    property real screenProgress: wide && ui.screenExpanded ? 1 : 0
    // Moving only when the user turns it: an app that starts with the screen
    // turned (or a phone turned on its side) shows it so at once, not unfolding.
    property bool _animatingScreen: false
    Behavior on screenProgress {
        enabled: page._animatingScreen
        NumberAnimation {
            duration: Theme.durationPage * 1.5
            easing.type: Easing.InOutCubic
            onRunningChanged: if (!running) page._animatingScreen = false
        }
    }

    // The track's screen has no room for it.
    onScreenProgressChanged: if (screenProgress > 0) filterOpen = false

    function _part(from, to) { return Math.max(0, Math.min(1, (screenProgress - from) / (to - from))) }
    // The bar folds first and the panel rises into the room it leaves.
    readonly property real _barFolded: _part(0, 0.6)
    readonly property real _panelRisen: _part(0.4, 1)

    readonly property real _heroMargin: Theme.dp(12)
    readonly property real _railWidth: Theme.touchLarge + _heroMargin * 2
    // A row's cover and its margins (TrackRow).
    readonly property real _coversWidth: Theme.dp(52) + Theme.dp(10) * 2
    readonly property real _listFullWidth: width * 0.5

    // The list's filter. Not on the dev tracks.
    property bool filterOpen: false
    onFilterOpenChanged: {
        if (filterOpen) {
            filterField.forceActiveFocus()
        } else {
            filterField.text = ""
            filterField.focus = false
        }
    }
    // Playback goes on to another track: the list goes to it, unless a finger
    // has it, and then a finger may take it anywhere until the next one.
    property int _followRow: -1
    function _follow() {
        const row = _followRow
        if (row < 0 || row >= list.count)
            return // its row may be yet to come
        _followRow = -1
        if (list.moving)
            return
        const item = list.itemAtIndex(row)
        // Clear of the edges, where rows dissolve.
        const margin = Math.min(Theme.rowHeight / 2, list.height / 4)
        if (item && item.y >= list.contentY + margin && item.y + item.height <= list.contentY + list.height - margin)
            return
        following.stop()
        const from = list.contentY
        list.positionViewAtIndex(row, ListView.Center)
        following.to = list.contentY
        list.contentY = from
        following.start()
    }
    NumberAnimation {
        id: following
        target: list
        property: "contentY"
        duration: Theme.durationPage * 1.5
        easing.type: Easing.InOutCubic
    }
    Connections {
        target: page.backend.mainScreen
        ignoreUnknownSignals: true
        function onPlayingRowChanged(row) {
            page._followRow = row
            Qt.callLater(page._follow)
        }
    }

    // Another playlist has its own rows.
    Connections {
        target: page.playlist
        ignoreUnknownSignals: true
        function onTitleChanged() { page.filterOpen = false }
    }
    readonly property real _listWidth: _listFullWidth + (_coversWidth - _listFullWidth) * screenProgress

    background: Rectangle { color: Theme.surface0 }

    Settings {
        id: ui
        category: "ui"
        property bool screenExpanded: false
    }

    component SourceButton: AbstractButton {
        id: sourceButton
        implicitHeight: Theme.touchLarge
        focusPolicy: Qt.NoFocus
        enabled: page.library !== null
        onClicked: page.libraryRequested()
        background: Rectangle {
            radius: Theme.radius
            // No fill in the dark theme, where it only dims the background; the press still shows.
            color: sourceButton.down && ripple.flat ? Theme.surface300 : (Theme.dark ? "transparent" : Theme.surface200)
            Ripple {
                id: ripple
                pressed: sourceButton.pressed
                pressX: sourceButton.pressX
                pressY: sourceButton.pressY
                radius: parent.radius
            }
        }
        contentItem: RowLayout {
            spacing: Theme.dp(10)
            SourceLogo {
                Layout.leftMargin: Theme.dp(10)
                implicitWidth: Theme.dp(28)
                implicitHeight: Theme.dp(28)
                logo: page.library && page.library.activeSourceLogo ? page.library.activeSourceLogo : ""
                icon: page.library && page.library.activeSourceIcon ? page.library.activeSourceIcon : ""
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
        anchors.bottomMargin: page._miniShown ? Theme.miniPlayerHeight : 0
        spacing: 0
        // Receding as Now Playing comes up, and neither drawn nor pressed under
        // it: Now Playing's background lets presses through.
        scale: 1 - 0.06 * page.nowPlayingProgress
        opacity: 1 - 0.6 * page.nowPlayingProgress
        visible: page.nowPlayingProgress < 1

        // The track, on a head unit, over the generated background.
        Item {
            id: hero
            visible: page.wide
            // A cover swiped this way stays on this side, over the list it'd show
            // what's behind it through.
            clip: true
            Layout.fillHeight: true
            Layout.preferredWidth: page.width - page._listWidth

            // Not inside: the hero clips, and this goes on up under the status bar.
            AuraBackground {
                objectName: "heroAura"
                parent: page
                z: -0.5
                x: 0
                y: -Theme.insetTop
                width: hero.width
                height: page.height + Theme.insetTop
                visible: hero.visible
                seed: page.backgroundSeed
                // One picture for both modes, stretched: it's soft enough not to
                // show, and generating it is what costs.
                artSize: Qt.size(page.width - page._coversWidth, page.height + Theme.insetTop)
            }
            TrackPanel {
                objectName: "trackPanel"
                x: page._heroMargin + (page._railWidth - page._heroMargin) * page.screenProgress
                y: page._heroMargin + (sourceBar.height + Theme.dp(10)) * (1 - page._panelRisen)
                width: parent.width - x - page._heroMargin
                height: parent.height - y - page._heroMargin - Theme.insetBottom
                progress: page.screenProgress
                // A phone on its side.
                compact: page.height < Theme.dp(460)
                // As large as it gets, beside the rest.
                coverMaxSide: (page.height - page._heroMargin * 2 - Theme.insetBottom) * 0.8
                // In the row with the source's button, until that folds away.
                modesElsewhere: 1 - page._barFolded
                sideCoverShare: 1
                // With air around it; smaller on the track's screen, for the
                // title to have the room. The picture stays at the larger size.
                sideCoverHeightShare: 0.8 - 0.15 * page.screenProgress
                // Hidden on a phone, where a cover of this size would be decoded for nothing
                // at every change of track.
                nowPlaying: page.wide ? page.nowPlaying : null
                playlist: page.playlist
                overAura: true
                swipeEnabled: page.gesturesEnabled
                swipeSurface: hero
                neighbors: page.backend.mainScreen
                playlistTitle: page.playlist && page.playlist.title ? page.playlist.title : ""
                preloadFractions: page.backend.mainScreen ? page.backend.mainScreen.preloadFractions : []
                showMenu: page.backend.trackScreen !== null
                onMenuRequested: {
                    page.backend.trackScreen.showPlaying()
                    page.trackMenuRequested()
                }
            }
            SourceBar {
                id: sourceBar
                objectName: "sourceBar"
                x: page._heroMargin
                y: page._heroMargin
                fullWidth: hero.width - page._heroMargin * 2 - Theme.touch * 2 - Theme.dp(8)
                folded: page._barFolded
                library: page.library
                onClicked: page.libraryRequested()
            }
            RowLayout {
                objectName: "topModes"
                anchors.right: parent.right
                anchors.rightMargin: page._heroMargin
                y: page._heroMargin + (sourceBar.height - height) / 2
                spacing: 0
                opacity: 1 - page._barFolded
                visible: opacity > 0 && page.nowPlaying && page.nowPlaying.repeat !== undefined
                IconButton {
                    objectName: "topShuffle"
                    // A radio has nothing to shuffle: new tracks instead.
                    readonly property bool radio: page.nowPlaying !== null && page.nowPlaying.radio === true
                    enabled: page.nowPlaying && (!radio || page.playlist !== null)
                    iconName: radio ? "refresh" : "shuffle"
                    color: !radio && page.nowPlaying && page.nowPlaying.shuffle === true ? Theme.accent : "#FFFFFF"
                    // The new station is being asked for.
                    busy: radio && page.playlist !== null && page.playlist.startingRadio === true
                    onClicked: {
                        if (busy)
                            return
                        radio ? page.playlist.restartRadio() : page.nowPlaying.toggleShuffle()
                    }
                }
                IconButton {
                    objectName: "topRepeat"
                    iconName: page.nowPlaying && page.nowPlaying.repeat === 2 ? "repeat_one" : "repeat"
                    color: page.nowPlaying && page.nowPlaying.repeat > 0 ? Theme.accent : "#FFFFFF"
                    onClicked: page.nowPlaying.cycleRepeat()
                }
            }
            // In the screen's left column when it's the track's.
            IconButton {
                objectName: "railSettings"
                x: page._heroMargin + (sourceBar.height - width) / 2
                y: parent.height - height - page._heroMargin + Theme.dp(16) * (1 - page._panelRisen)
                iconName: "tune"
                color: "#FFFFFF"
                opacity: page._panelRisen
                visible: opacity > 0
                onClicked: page.settingsRequested()
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
                clip: true
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(8) * (1 - page.screenProgress)
                    // In the middle of the covers when there's only those.
                    anchors.rightMargin: Theme.dp(4) + ((page._coversWidth - Theme.touch) / 2 - Theme.dp(4)) * page.screenProgress
                    spacing: Theme.dp(8) * (1 - page.screenProgress)
                    // The filter, in the title's place. A phone's bar has the way
                    // to the sources there: on a phone it comes out under the bar.
                    Item {
                        id: filterInBar
                        visible: page.wide && page.filterOpen
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.dp(8)
                        implicitHeight: Theme.barHeight - Theme.dp(16)
                    }
                    SourceButton {
                        visible: !page.wide
                        Layout.fillWidth: true
                    }
                    // The playlist, and how many tracks right after its name.
                    Item {
                        objectName: "listTitleBlock"
                        visible: page.wide && !page.filterOpen
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.dp(8)
                        implicitHeight: listTitle.implicitHeight
                        opacity: Math.max(0, 1 - page.screenProgress * 2)
                        Text {
                            id: listTitle
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width - listCount.implicitWidth - Theme.dp(8))
                            text: page.playlist ? page.playlist.title : ""
                            color: Theme.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTitle
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            id: listCount
                            objectName: "playlistCount"
                            x: listTitle.width + Theme.dp(8)
                            anchors.baseline: listTitle.baseline
                            visible: text !== ""
                            text: page.playlist && page.playlist.title && page.playlist.count !== undefined
                                  ? String(page.playlist.count) : ""
                            color: Theme.inkSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }
                    }
                    OfflineBadge {
                        backend: page.backend
                        // Always: the text would take the whole bar on a head unit.
                        iconOnly: true
                        opacity: Math.max(0, 1 - page.screenProgress * 2)
                    }
                    // Settings says what it's about in full.
                    IconButton {
                        objectName: "spaceWarning"
                        visible: page.backend.mainScreen !== null && page.backend.mainScreen.spaceWarning !== ""
                        iconName: "warning"
                        color: Theme.danger
                        opacity: Math.max(0, 1 - page.screenProgress * 2)
                        onClicked: page.settingsRequested()
                    }
                    IconButton {
                        objectName: "filterButton"
                        visible: page.backend.mainScreen !== null && opacity > 0
                        Layout.preferredWidth: Theme.touch * (1 - page.screenProgress)
                        iconName: page.filterOpen ? "close" : "search"
                        opacity: 1 - page.screenProgress
                        onClicked: page.filterOpen = !page.filterOpen
                    }
                    IconButton {
                        objectName: "listSettings"
                        // Narrows away as it moves to the left column.
                        Layout.preferredWidth: Theme.touch * (1 - page.screenProgress)
                        iconName: "tune"
                        opacity: 1 - page.screenProgress
                        visible: opacity > 0
                        onClicked: page.settingsRequested()
                    }
                    IconButton {
                        objectName: "screenModeToggle"
                        visible: page.wide
                        iconName: "chevron_right"
                        rotation: 180 * page.screenProgress
                        onClicked: {
                            page._animatingScreen = true
                            ui.screenExpanded = !ui.screenExpanded
                        }
                    }
                }
            }

            Rectangle {
                id: filterUnderBar
                Layout.fillWidth: true
                implicitHeight: Theme.barHeight * filterHeight
                property real filterHeight: page.filterOpen && !page.wide ? 1 : 0
                Behavior on filterHeight { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                visible: implicitHeight > 0
                clip: true
                color: Theme.surface100
            }
            TextField {
                selectionColor: Theme.accent
                selectedTextColor: Theme.inkOnAccent
                cursorDelegate: TextCursor { }
                id: filterField
                objectName: "mainFilter"
                parent: page.wide ? filterInBar : filterUnderBar
                visible: page.filterOpen
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: page.wide ? 0 : Theme.dp(8)
                height: Theme.barHeight - Theme.dp(16)
                placeholderText: qsTr("Filter by title, artist or album")
                placeholderTextColor: Theme.inkTertiary
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                inputMethodHints: Qt.ImhNoPredictiveText
                background: Rectangle { radius: Theme.radius; color: Theme.surface200 }
                onTextChanged: if (page.backend.mainScreen) page.backend.mainScreen.setFilter(text)
            }

            // Going down to its covers it's only cut off: the list keeps its
            // width, and nothing in it is laid out anew each frame.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ScrollList {
                    id: list
                    objectName: "mainList"
                    onMovementStarted: {
                        following.stop()
                        page._followRow = -1
                    }
                    onCountChanged: if (page._followRow >= 0) Qt.callLater(page._follow)
                    width: page.wide ? page._listFullWidth : parent.width
                    height: parent.height
                    // With the mini player below, it's that keeps clear of the bar.
                    bottomMargin: page._miniShown ? 0 : Theme.insetBottom
                    model: page.backend.tracks
                    delegate: TrackRow {
                        playing: page.nowPlaying && page.nowPlaying.playing === true
                        showMenu: page.backend.trackScreen !== null
                        layoutWidth: page.wide ? page._listFullWidth : 0
                        onClicked: {
                            if (page.backend.mainScreen)
                                page.backend.mainScreen.playRow(index)
                            else
                                page.playlist.playRow(index)
                        }
                        onMenuRequested: {
                            page.backend.trackScreen.showActiveRow(index)
                            page.trackMenuRequested()
                        }
                    }

                    // Nothing active yet: the way to the Library.
                    Column {
                        parent: list
                        anchors.centerIn: parent
                        visible: list.count === 0 && page.library !== null && page.screenProgress < 0.5
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
    }

    // Not made on a head unit: its cover would be decoded for nothing at every
    // change of track.
    Loader {
        anchors.fill: parent
        active: !page.wide
        visible: page.hasTrack || page.nowPlayingProgress > 0
        sourceComponent: NowPlayingPage {
            objectName: "nowPlaying"
            backend: page.backend
            platform: page.platform
            progress: page.nowPlayingProgress
            swipeEnabled: page.gesturesEnabled
            onOpenRequested: page.nowPlayingRequested()
            onCloseRequested: page.nowPlayingCloseRequested()
            onTrackMenuRequested: page.trackMenuRequested()
            onDragged: (share) => page.nowPlayingDragged(share)
            onDragEnded: (velocity, fromOpen) => page.nowPlayingDragEnded(velocity, fromOpen)
        }
    }
}
