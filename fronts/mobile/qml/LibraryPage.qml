import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The Library: the sources in a column on the left; for the one picked, its
// favorites as tiles (a tap plays and goes back to the main screen) and its
// other playlists in a list, where a star makes one a favorite.
Page {
    id: page
    bottomPadding: Theme.pageInsetBottom

    property QtObject backend
    property QtObject platform
    signal closeRequested()
    signal settingsRequested()
    signal sourceSettingsRequested()
    signal playlistRequested()
    signal downloadsRequested()
    signal aboutRequested()

    readonly property QtObject library: backend.library
    // The sign-in on screen, for the core (LibraryScreen::watchSignIn): while
    // this page is the one shown — it stays so behind a browser.
    readonly property string _watched: StackView.status === StackView.Active && library
                                       && library.authNeeded === true && library.currentSourceId
                                       ? library.currentSourceId : ""
    on_WatchedChanged: if (library) library.watchSignIn(_watched)
    Component.onDestruction: if (library) library.watchSignIn("")
    readonly property bool wide: width > height * 1.1

    background: Rectangle { color: Theme.surface0 }

    // Another source picked: its page goes off up or down, the way the column
    // goes, and the new one comes in from the other side. The data changes in
    // between, while nothing shows: with the core in its own process (Android)
    // it comes a moment after it's asked for, and a picture of the old page
    // taken then would already have the new one.
    property string _pendingSourceId: ""
    property int _pendingRow: -1
    property int _currentRow: -1
    property int _direction: 1
    property bool _awaitingSource: false
    readonly property real _paneShift: Theme.dp(24)

    function switchSource(sourceId, row) {
        if (_pendingSourceId === "" && sourceId === library.currentSourceId) {
            library.selectSource(sourceId) // wakes it, if it was left unstarted
            return
        }
        if (sourceId === _pendingSourceId)
            return
        const from = _pendingSourceId !== "" ? _pendingRow : _currentRow
        _direction = row >= from ? 1 : -1
        _pendingSourceId = sourceId
        _pendingRow = row
        // Going already: it goes on, to this one instead.
        if (paneOut.running)
            return
        paneIn.stop()
        arrival.stop()
        _awaitingSource = false
        paneOut.start()
    }
    function _goTo() {
        _awaitingSource = true
        arrival.restart()
        library.selectSource(_pendingSourceId)
        // Shown already (picked back while the other went): no change to wait for.
        if (_awaitingSource && library.currentSourceId === _pendingSourceId)
            _comeIn()
    }
    function _comeIn() {
        _awaitingSource = false
        arrival.stop()
        paneShift.y = _direction * _paneShift
        paneIn.start()
    }
    ParallelAnimation {
        id: paneOut
        NumberAnimation {
            target: sourcePane; property: "opacity"; to: 0
            duration: Theme.durationFast; easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: paneShift; property: "y"; to: -page._direction * page._paneShift
            duration: Theme.durationFast; easing.type: Easing.InCubic
        }
        onFinished: page._goTo()
    }
    ParallelAnimation {
        id: paneIn
        NumberAnimation {
            target: sourcePane; property: "opacity"; to: 1
            duration: Theme.durationPage; easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: paneShift; property: "y"; to: 0
            duration: Theme.durationPage; easing.type: Easing.OutCubic
        }
        onFinished: page._pendingSourceId = ""
    }
    // The source asked for may never come (gone meanwhile, another taken in
    // its place): the page isn't left empty for it.
    Timer {
        id: arrival
        interval: 500
        onTriggered: if (page._awaitingSource) page._comeIn()
    }

    // A playlist tapped: one that asks first (Playlist.offer) asks; the answer
    // changes the list, and nothing opens. Otherwise a station plays, a
    // playlist opens to see its tracks (`open`: a station's page too).
    function pick(row, open) {
        if (row.offerQuestion !== "") {
            offerDialog.playlistId = row.playlistId
            offerDialog.cancelText = row.offerDecline
            offerDialog.ask(row.title, row.offerQuestion, row.offerAccept)
        } else if (row.kind === "radioStation" && !open) {
            library.playPlaylist(row.playlistId)
        } else {
            openPlaylist(row.playlistId)
        }
    }
    ConfirmDialog {
        id: offerDialog
        objectName: "offerDialog"
        property string playlistId
        danger: false
        onAccepted: page.library.answerOffer(playlistId, true)
        onDeclined: page.library.answerOffer(playlistId, false)
    }

    function openPlaylist(playlistId) {
        backend.playlistScreen.openPlaylist(library.currentSourceId, playlistId)
        playlistRequested()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The sources: with names where there's room, icons on a phone.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: page.wide ? Theme.dp(190) : Theme.dp(64)
            color: Theme.surface100

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                IconButton {
                    Layout.margins: Theme.dp(8)
                    iconName: "arrow_back"
                    onClicked: page.closeRequested()
                }
                ScrollList {
                    id: sourceList
                    objectName: "sources"
                    fadeColor: Theme.surface100
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: page.backend.sources
                    delegate: ItemDelegate {
                        id: sourceRow
                        required property var sourceId
                        required property var name
                        required property var iconUrl
                        required property var needsSignIn
                        required property var isLoading
                        required property var hasError
                        required property var isCurrent
                        required property int index
                        readonly property bool troubled: needsSignIn === true || hasError === true
                        width: ListView.view.width
                        height: Theme.touchLarge + Theme.dp(4)
                        // The one being gone to shows as picked at once, not
                        // once the page shown has gone.
                        readonly property bool shownCurrent: page._pendingSourceId !== ""
                            ? sourceRow.sourceId === page._pendingSourceId : sourceRow.isCurrent === true
                        // Where the page shown is in the column: which way the next goes.
                        onIsCurrentChanged: if (sourceRow.isCurrent === true) page._currentRow = index
                        Component.onCompleted: if (sourceRow.isCurrent === true) page._currentRow = index
                        onClicked: page.switchSource(sourceRow.sourceId, index)
                        background: Rectangle {
                            color: sourceRow.shownCurrent ? Theme.surface300
                                 : sourceRow.down && sourceRipple.flat ? Theme.surface200 : "transparent"
                            Ripple {
                                id: sourceRipple
                                pressed: sourceRow.pressed
                                pressX: sourceRow.pressX
                                pressY: sourceRow.pressY
                            }
                            Rectangle {
                                visible: sourceRow.shownCurrent
                                width: Theme.dp(3)
                                height: parent.height
                                color: Theme.accent
                            }
                        }
                        contentItem: RowLayout {
                            spacing: Theme.dp(12)
                            Item {
                                Layout.alignment: page.wide ? Qt.AlignVCenter : Qt.AlignCenter
                                implicitWidth: Theme.dp(26)
                                implicitHeight: Theme.dp(26)
                                IconImage {
                                    anchors.fill: parent
                                    visible: !!sourceRow.iconUrl
                                    source: sourceRow.iconUrl || ""
                                    sourceSize.width: width
                                    sourceSize.height: height
                                    color: sourceRow.shownCurrent ? Theme.accent : Theme.ink
                                }
                                // A source with no icon: its first letter.
                                Text {
                                    anchors.centerIn: parent
                                    visible: !sourceRow.iconUrl
                                    text: (sourceRow.name || "?").charAt(0)
                                    color: sourceRow.shownCurrent ? Theme.accent : Theme.ink
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontTitle
                                    font.weight: Font.Bold
                                }
                                // No room beside it in the narrow column: on its corner.
                                IconImage {
                                    objectName: "sourceWarningBadge"
                                    visible: !page.wide && sourceRow.troubled
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: -Theme.dp(6)
                                    anchors.bottomMargin: -Theme.dp(4)
                                    source: "qrc:/icons/warning.svg"
                                    sourceSize.width: Theme.dp(16)
                                    sourceSize.height: Theme.dp(16)
                                    color: Theme.warning
                                }
                            }
                            Text {
                                visible: page.wide
                                Layout.fillWidth: true
                                text: sourceRow.name || ""
                                color: Theme.ink
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.weight: sourceRow.shownCurrent ? Font.Bold : Font.Medium
                                elide: Text.ElideRight
                            }
                            IconImage {
                                visible: page.wide && sourceRow.troubled
                                source: "qrc:/icons/warning.svg"
                                sourceSize.width: Theme.dp(18)
                                sourceSize.height: Theme.dp(18)
                                color: Theme.warning
                            }
                        }
                    }
                }
                // As many to a row as the column takes: one in a phone's, side by
                // side in a head unit's, whose screen has no height to spare.
                Flow {
                    objectName: "libraryTools"
                    Layout.fillWidth: true
                    Layout.margins: Theme.dp(8)
                    spacing: page.wide ? Theme.dp(4) : 0
                    // What's being saved: rings while anything is.
                    DownloadButton {
                        objectName: "downloadsButton"
                        readonly property QtObject saving: page.backend.downloadsScreen
                        visible: saving !== null && saving.enabled === true
                        downloading: saving !== null && saving.active === true
                        progress: saving !== null ? saving.progress : -1
                        onClicked: page.downloadsRequested()
                    }
                    IconButton {
                        objectName: "history"
                        iconName: "history"
                        onClicked: {
                            page.backend.playlistScreen.openHistory()
                            page.playlistRequested()
                        }
                    }
                    IconButton {
                        objectName: "librarySettings"
                        iconName: "tune"
                        onClicked: page.settingsRequested()
                    }
                    IconButton {
                        objectName: "aboutButton"
                        iconName: "cloudmus"
                        onClicked: page.aboutRequested()
                    }
                }
            }
        }

        // The source picked.
        ColumnLayout {
            id: sourcePane
            objectName: "sourcePane"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // Moved by a transform: the layout has the position.
            transform: Translate { id: paneShift }

            // The source's name and menu, staying put over what scrolls.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.padding
                Layout.rightMargin: Theme.padding
                Layout.topMargin: Theme.padding
                spacing: Theme.dp(10)
                SourceLogo {
                    id: currentLogo
                    objectName: "currentSourceLogo"
                    visible: logo !== "" || icon !== ""
                    implicitWidth: Theme.dp(36)
                    implicitHeight: Theme.dp(36)
                    logo: page.library && page.library.currentSourceLogo ? page.library.currentSourceLogo : ""
                    icon: page.library && page.library.currentSourceIcon ? page.library.currentSourceIcon : ""
                    // Loading its playlists: round the logo, as round Play.
                    Spinner {
                        objectName: "sourceSpinner"
                        anchors.fill: parent
                        anchors.margins: -Theme.dp(4)
                        lineWidth: Theme.dp(2.5)
                        running: page.library && page.library.loading === true
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: page.library ? page.library.currentSourceName : ""
                    color: Theme.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeadline
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                OfflineBadge {
                    backend: page.backend
                    iconOnly: !page.wide
                }
                IconButton {
                    id: moreButton
                    objectName: "sourceMenuButton"
                    iconName: "more_vert"
                    onClicked: sourceMenu.open()
                    DropMenu {
                        id: sourceMenu
                        objectName: "sourceMenu"
                        DropMenuItem {
                            objectName: "refreshEntry"
                            iconName: "refresh"
                            text: qsTr("Refresh")
                            onTriggered: page.library.refresh()
                        }
                        DropMenuItem {
                            objectName: "sourceSettingsEntry"
                            iconName: "tune"
                            text: qsTr("Settings")
                            onTriggered: page.sourceSettingsRequested()
                        }
                        DropMenuItem {
                            id: signOut
                            objectName: "signOut"
                            iconName: "logout"
                            available: page.library && page.library.canSignOut === true
                            text: qsTr("Sign out")
                            onTriggered: page.library.signOut()
                        }
                    }
                }
            }

            ScrollArea {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: column.implicitHeight + Theme.padding * 2
                // Another source: its playlists from the top.
                Connections {
                    target: page.library
                    ignoreUnknownSignals: true
                    function onCurrentSourceIdChanged() {
                        content.contentY = 0
                        if (page._awaitingSource && page.library.currentSourceId === page._pendingSourceId)
                            page._comeIn()
                    }
                }

                ColumnLayout {
                    id: column
                    x: Theme.padding
                    y: Theme.padding
                    width: content.width - Theme.padding * 2
                    spacing: Theme.padding

                    // Signing in, when the source asks for it.
                    SignInCard {
                        id: signInCard
                        shown: !!page.library && page.library.authNeeded === true
                        // Not as the page goes over to another source.
                        animated: page._pendingSourceId === ""
                        sourceId: page.library && page.library.currentSourceId ? page.library.currentSourceId : ""
                        kinds: page.library && page.library.authKinds ? page.library.authKinds : []
                        url: page.library && page.library.authUrl ? page.library.authUrl : ""
                        code: page.library && page.library.authCode ? page.library.authCode : ""
                        browser: page.library && page.library.authBrowser ? page.library.authBrowser : ""
                        browserNeedsProxy: !!page.library && page.library.authBrowserNeedsProxy === true
                        fields: page.library && page.library.authFields ? page.library.authFields : []
                        formMessage: page.library && page.library.authFormMessage ? page.library.authFormMessage : ""
                        busy: !!page.library && page.library.authBusy === true
                        error: page.library && page.library.authError ? page.library.authError : ""
                        preparing: !!page.library && page.library.authPreparing === true
                        starting: !!page.library && page.library.sourceStarting === true
                        platform: page.platform
                        helper: page.backend ? page.backend.signInHelper : null
                        wide: page.wide
                        onSignInRequested: page.library.signIn()
                        onPrepareRequested: page.library.prepareSignIn()
                        onHelperRequested: page.platform.signInWithHelper(
                            page.library.currentSourceId, browser, page.library.currentSourceName)
                        onBrowserRequested: page.platform.signInWithBrowser(
                            page.library.currentSourceId, browser, page.library.currentSourceName)
                        sourceName: page.library && page.library.currentSourceName ? page.library.currentSourceName : ""
                        onFormSubmitted: (values) => page.library.submitSignInForm(values)
                    }

                    // Not signed in yet: what there'll be to use.
                    Panel {
                        shown: !!page.library && page.library.authNeeded === true && capabilityTags.count > 0
                        animated: page._pendingSourceId === ""
                        spacing: Theme.dp(12)
                        SectionHeading {
                            text: qsTr("Capabilities")
                        }
                        CapabilityTags {
                            id: capabilityTags
                            objectName: "capabilities"
                            Layout.fillWidth: true
                            model: page.library ? page.library.capabilities : []
                        }
                    }

                    SectionHeading {
                        visible: tiles.count > 0
                        text: qsTr("Favorites")
                    }
                    GridView {
                        id: tiles
                        objectName: "tiles"
                        Layout.fillWidth: true
                        readonly property int columns: Math.max(2, Math.floor(width / Theme.dp(150)))
                        cellWidth: width / columns
                        cellHeight: cellWidth + Theme.dp(44)
                        implicitHeight: Math.ceil(count / columns) * cellHeight
                        interactive: false
                        model: page.backend.favorites
                        delegate: AbstractButton {
                            id: tile
                            required property var playlistId
                            required property var title
                            required property var trackCount
                            required property var kind
                            required property var coverUrl
                            required property var coverIcon
                            required property var isActive
                            required property var isStarting
                            required property var path
                            required property var offerQuestion
                            required property var offerAccept
                            required property var offerDecline
                            width: tiles.cellWidth
                            height: tiles.cellHeight
                            focusPolicy: Qt.NoFocus
                            // A station has no tracks to look at: it plays. A playlist
                            // opens, to see its tracks.
                            onClicked: page.pick(tile, false)
                            // Its page, a station's too: its star there takes it out of the favorites.
                            onPressAndHold: page.pick(tile, true)
                            contentItem: ColumnLayout {
                                spacing: Theme.dp(4)
                                Cover {
                                    Layout.fillWidth: true
                                    Layout.rightMargin: Theme.dp(10)
                                    Layout.preferredHeight: width
                                    source: tile.coverUrl || ""
                                    icon: tile.coverIcon || ""
                                    seed: tile.title || ""
                                    outlined: tile.isActive === true
                                    opacity: tile.down && tileRipple.flat ? 0.7 : 1
                                    Ripple {
                                        id: tileRipple
                                        // The tile's press, where it is on the cover.
                                        readonly property point _at: parent.mapFromItem(tile, tile.pressX, tile.pressY)
                                        pressed: tile.pressed
                                        pressX: _at.x
                                        pressY: _at.y
                                        radius: parent.radius
                                        color: "#FFFFFF"
                                    }
                                    Spinner {
                                        anchors.centerIn: parent
                                        running: tile.isStarting === true
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.rightMargin: Theme.dp(10)
                                    text: tile.title || ""
                                    color: tile.isActive === true ? Theme.accent : Theme.ink
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                // A folder from inside others: where it is, the
                                // nearest of them kept when it doesn't fit.
                                Text {
                                    objectName: "tilePath"
                                    Layout.fillWidth: true
                                    Layout.rightMargin: Theme.dp(10)
                                    Layout.topMargin: -Theme.dp(4)
                                    visible: text !== ""
                                    text: tile.path || ""
                                    color: Theme.inkTertiary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideLeft
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }

                    SectionHeading {
                        visible: others.count > 0
                        text: qsTr("All playlists and stations")
                    }
                    ListView {
                        id: others
                        objectName: "others"
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        interactive: false
                        model: page.backend.otherPlaylists
                        delegate: ItemDelegate {
                            id: other
                            required property var playlistId
                            required property var title
                            required property var trackCount
                            required property var sizeText
                            required property var kind
                            required property var coverUrl
                            required property var coverIcon
                            required property var isActive
                            required property var offerQuestion
                            required property var offerAccept
                            required property var offerDecline
                            required property var isStarting
                            width: ListView.view.width
                            height: Theme.rowHeight
                            // The style's vertical padding plus the star's height is more than the
                            // row: the content overflowed downwards, off the highlight's middle.
                            topPadding: 0
                            bottomPadding: 0
                            onClicked: page.pick(other, false)
                            onPressAndHold: page.pick(other, true)
                            background: Rectangle {
                                radius: Theme.radius
                                color: other.down && otherRipple.flat ? Theme.surface200 : "transparent"
                                Ripple {
                                    id: otherRipple
                                    radius: parent.radius
                                    pressed: other.pressed
                                    pressX: other.pressX
                                    pressY: other.pressY
                                }
                            }
                            contentItem: RowLayout {
                                spacing: Theme.dp(12)
                                Cover {
                                    Layout.preferredWidth: Theme.dp(48)
                                    Layout.preferredHeight: Theme.dp(48)
                                    source: other.coverUrl || ""
                                    icon: other.coverIcon || ""
                                    seed: other.title || ""
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.dp(2)
                                    Text {
                                        Layout.fillWidth: true
                                        text: other.title || ""
                                        color: other.isActive === true ? Theme.accent : Theme.ink
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: other.kind === "radioStation" ? qsTr("Radio") : qsTr("%1 tracks").arg(other.trackCount) + (other.sizeText ? " · " + other.sizeText : "")
                                        color: Theme.inkSecondary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }
                                Spinner {
                                    running: other.isStarting === true
                                }
                                IconButton {
                                    iconName: "star_border"
                                    onClicked: page.library.toggleFavorite(other.playlistId)
                                }
                            }
                        }
                    }
                    // Nothing to list yet: the source is on its way.
                    Panel {
                        id: waiting
                        objectName: "sourceWaiting"
                        readonly property bool starting: !!page.library && page.library.sourceStarting === true
                        shown: !!page.library && (starting || page.library.loading === true)
                               && !page.library.authNeeded && tiles.count === 0 && others.count === 0
                        animated: page._pendingSourceId === ""
                        color: "transparent"
                        spacing: Theme.padding
                        Spinner {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: Theme.dp(48)
                            implicitWidth: Theme.dp(48)
                            implicitHeight: Theme.dp(48)
                            running: waiting.shown
                        }
                        BodyText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.inkSecondary
                            text: waiting.starting
                                  ? qsTr("%1 is starting…").arg(page.library.currentSourceName)
                                  : qsTr("Loading playlists…")
                        }
                    }
                    BodyText {
                        visible: page.library && !page.library.loading && !page.library.sourceStarting
                                 && !page.library.authNeeded && tiles.count === 0 && others.count === 0
                        text: page.library && page.library.currentSourceId ? qsTr("No playlists") : qsTr("No sources")
                    }
                }
            }
        }
    }
}
