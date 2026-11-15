import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The Library: the sources in a column on the left; for the one picked, its
// favorites as tiles (a tap plays and goes back to the main screen) and its
// other playlists in a list, where a star makes one a favorite.
Page {
    id: page
    bottomPadding: Theme.insetBottom

    property QtObject backend
    property QtObject platform
    signal closeRequested()
    signal settingsRequested()
    signal sourceSettingsRequested()
    signal playlistRequested()
    signal downloadsRequested()

    readonly property QtObject library: backend.library
    readonly property bool wide: width > height * 1.1

    background: Rectangle { color: Theme.surface0 }

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
                        readonly property bool troubled: needsSignIn === true || hasError === true
                        width: ListView.view.width
                        height: Theme.touchLarge + Theme.dp(4)
                        onClicked: page.library.selectSource(sourceRow.sourceId)
                        background: Rectangle {
                            color: sourceRow.isCurrent === true ? Theme.surface300
                                 : sourceRow.down && sourceRipple.flat ? Theme.surface200 : "transparent"
                            Ripple {
                                id: sourceRipple
                                pressed: sourceRow.pressed
                                pressX: sourceRow.pressX
                                pressY: sourceRow.pressY
                            }
                            Rectangle {
                                visible: sourceRow.isCurrent === true
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
                                    color: sourceRow.isCurrent === true ? Theme.accent : Theme.ink
                                }
                                // A source with no icon: its first letter.
                                Text {
                                    anchors.centerIn: parent
                                    visible: !sourceRow.iconUrl
                                    text: (sourceRow.name || "?").charAt(0)
                                    color: sourceRow.isCurrent === true ? Theme.accent : Theme.ink
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
                                font.weight: sourceRow.isCurrent === true ? Font.Bold : Font.Medium
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
                IconButton {
                    objectName: "downloadsButton"
                    Layout.alignment: Qt.AlignHCenter
                    iconName: "file_download"
                    onClicked: page.downloadsRequested()
                }
                IconButton {
                    objectName: "history"
                    Layout.alignment: Qt.AlignHCenter
                    iconName: "history"
                    onClicked: {
                        page.backend.playlistScreen.openHistory()
                        page.playlistRequested()
                    }
                }
                IconButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.margins: Theme.dp(8)
                    iconName: "tune"
                    onClicked: page.settingsRequested()
                }
            }
        }

        // The source picked.
        ScrollArea {
            id: content
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: column.implicitHeight + Theme.padding * 2

            ColumnLayout {
                id: column
                x: Theme.padding
                y: Theme.padding
                width: content.width - Theme.padding * 2
                spacing: Theme.padding

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(10)
                    SourceLogo {
                        id: currentLogo
                        objectName: "currentSourceLogo"
                        visible: logo !== ""
                        implicitWidth: Theme.dp(36)
                        implicitHeight: Theme.dp(36)
                        logo: page.library && page.library.currentSourceLogo ? page.library.currentSourceLogo : ""
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

                // Signing in, when the source asks for it.
                Rectangle {
                    Layout.fillWidth: true
                    visible: page.library && page.library.authNeeded === true
                    implicitHeight: authColumn.implicitHeight + Theme.padding * 2
                    radius: Theme.radius
                    color: Theme.surface200
                    GridLayout {
                        id: authColumn
                        anchors.fill: parent
                        anchors.margins: Theme.padding
                        columns: page.wide ? 2 : 1
                        columnSpacing: Theme.padding
                        rowSpacing: Theme.padding
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Theme.dp(8)
                            BodyText {
                                text: qsTr("Sign in to see the playlists")
                                font.weight: Font.DemiBold
                            }
                            // Not signed in yet: what there'll be to use.
                            SectionHeading {
                                visible: capabilityTags.count > 0
                                text: qsTr("Capabilities")
                            }
                            CapabilityTags {
                                id: capabilityTags
                                objectName: "capabilities"
                                Layout.fillWidth: true
                                model: page.library ? page.library.capabilities : []
                            }
                            BodyText {
                                visible: text !== ""
                                text: page.library && page.library.authError ? page.library.authError : ""
                                color: Theme.danger
                            }
                            BodyText {
                                objectName: "authLink"
                                visible: text !== ""
                                textFormat: Text.StyledText
                                linkColor: Theme.accent
                                text: page.library && page.library.authUrl
                                      ? qsTr("Open %1 on any device and enter this code:")
                                            .arg("<a href=\"" + page.library.authUrl + "\">" + page.library.authUrl + "</a>")
                                      : ""
                                onLinkActivated: (link) => Qt.openUrlExternally(link)
                            }
                            RowLayout {
                                visible: authCode.text !== ""
                                spacing: Theme.dp(8)
                                Text {
                                    id: authCode
                                    objectName: "authCode"
                                    text: page.library && page.library.authCode ? page.library.authCode : ""
                                    color: Theme.accent
                                    font.family: Theme.monoFamily
                                    font.pixelSize: Theme.fontHeadline
                                    font.weight: Font.Bold
                                    TapHandler { onTapped: copyCode.clicked() }
                                }
                                IconButton {
                                    id: copyCode
                                    objectName: "copyAuthCode"
                                    iconName: "content_copy"
                                    onClicked: {
                                        page.platform.copyText(authCode.text)
                                        copied.visible = true
                                        copiedTimeout.restart()
                                    }
                                }
                                Text {
                                    id: copied
                                    visible: false
                                    text: qsTr("Copied")
                                    color: Theme.inkSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    Timer {
                                        id: copiedTimeout
                                        interval: 2000
                                        onTriggered: copied.visible = false
                                    }
                                }
                            }
                            // While a code is shown the source is waiting for
                            // it: asking again would only change the code.
                            TextButton {
                                visible: authCode.text === ""
                                text: qsTr("Sign in")
                                onClicked: page.library.signIn()
                            }
                        }
                        Image {
                            objectName: "authQr"
                            readonly property string url: page.library && page.library.authUrl
                                                          ? page.library.authUrl : ""
                            visible: url !== ""
                            Layout.preferredWidth: Theme.dp(120)
                            Layout.preferredHeight: Theme.dp(120)
                            Layout.alignment: page.wide ? Qt.AlignTop : Qt.AlignHCenter
                            sourceSize: Qt.size(width, height)
                            source: url !== "" ? "image://qr/" + encodeURIComponent(url) : ""
                            smooth: false
                        }
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
                        required property var isActive
                        required property var isStarting
                        width: tiles.cellWidth
                        height: tiles.cellHeight
                        focusPolicy: Qt.NoFocus
                        onClicked: page.library.playPlaylist(tile.playlistId)
                        // To see its tracks; its star there takes it out of the favorites.
                        onPressAndHold: page.openPlaylist(tile.playlistId)
                        contentItem: ColumnLayout {
                            spacing: Theme.dp(4)
                            Cover {
                                Layout.fillWidth: true
                                Layout.rightMargin: Theme.dp(10)
                                Layout.preferredHeight: width
                                source: tile.coverUrl || ""
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
                        required property var kind
                        required property var coverUrl
                        required property var isActive
                        required property var isStarting
                        width: ListView.view.width
                        height: Theme.rowHeight
                        // The style's vertical padding plus the star's height is more than the
                        // row: the content overflowed downwards, off the highlight's middle.
                        topPadding: 0
                        bottomPadding: 0
                        onClicked: page.openPlaylist(other.playlistId)
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
                                    text: other.kind === "radioStation" ? qsTr("Radio") : qsTr("%1 tracks").arg(other.trackCount)
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
                BodyText {
                    visible: page.library && !page.library.loading && !page.library.authNeeded
                             && tiles.count === 0 && others.count === 0
                    text: page.library && page.library.currentSourceId ? qsTr("No playlists") : qsTr("No sources")
                }
            }
        }
    }
}
