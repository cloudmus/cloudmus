import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The Library: the sources in a column on the left; for the one picked, its
// favorites as tiles (a tap plays and goes back to the main screen) and its
// other playlists in a list, where a star makes one a favorite.
Page {
    id: page

    property QtObject backend
    property QtObject platform
    signal closeRequested()
    signal settingsRequested()
    signal playlistRequested()

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
                        width: ListView.view.width
                        height: Theme.touchLarge + Theme.dp(4)
                        onClicked: page.library.selectSource(sourceRow.sourceId)
                        background: Rectangle {
                            color: sourceRow.isCurrent === true ? Theme.surface300
                                 : sourceRow.down ? Theme.surface200 : "transparent"
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
                                visible: sourceRow.needsSignIn === true || sourceRow.hasError === true
                                source: "qrc:/icons/warning.svg"
                                sourceSize.width: Theme.dp(18)
                                sourceSize.height: Theme.dp(18)
                                color: Theme.warning
                            }
                        }
                    }
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
        Flickable {
            id: content
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: column.implicitHeight + Theme.padding * 2
            boundsBehavior: Flickable.StopAtBounds
            maximumFlickVelocity: Theme.flickMaxVelocity
            flickDeceleration: Theme.flickDeceleration

            ColumnLayout {
                id: column
                x: Theme.padding
                y: Theme.padding
                width: content.width - Theme.padding * 2
                spacing: Theme.padding

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: page.library ? page.library.currentSourceName : ""
                        color: Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeadline
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Spinner {
                        running: page.library && page.library.loading === true
                    }
                    IconButton {
                        iconName: "refresh"
                        onClicked: page.library.refresh()
                    }
                }

                // Signing in, when the source asks for it.
                Rectangle {
                    Layout.fillWidth: true
                    visible: page.library && page.library.authNeeded === true
                    implicitHeight: authColumn.implicitHeight + Theme.padding * 2
                    radius: Theme.radius
                    color: Theme.surface200
                    ColumnLayout {
                        id: authColumn
                        anchors.fill: parent
                        anchors.margins: Theme.padding
                        spacing: Theme.dp(8)
                        Text {
                            text: qsTr("Sign in to see the playlists")
                            color: Theme.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                        }
                        BodyText {
                            visible: text !== ""
                            text: page.library && page.library.authError ? page.library.authError : ""
                            color: Theme.danger
                        }
                        BodyText {
                            visible: text !== ""
                            text: page.library && page.library.authUrl
                                  ? qsTr("Open %1 on any device").arg(page.library.authUrl) : ""
                        }
                        Text {
                            visible: text !== ""
                            text: page.library ? page.library.authCode : ""
                            color: Theme.accent
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontHeadline
                            font.weight: Font.Bold
                        }
                        TextButton {
                            text: qsTr("Sign in")
                            onClicked: page.library.signIn()
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
                                radius: Theme.dp(8)
                                opacity: tile.down ? 0.7 : 1
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
                        onClicked: page.openPlaylist(other.playlistId)
                        background: Rectangle { color: other.down ? Theme.surface200 : "transparent" }
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
