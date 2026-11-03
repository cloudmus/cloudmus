import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The current source, on a head unit, where height is short. Collapsed it's a
// two-part button floating over the panel, taking up no row: the source's icon
// (the Library) and an arrow. The arrow opens it out to the full `fullWidth`,
// with the source's and playlist's names, and turns round to fold it back.
Item {
    id: bar

    property QtObject library
    property QtObject playlist
    property real fullWidth: 0
    property bool expanded: false
    signal libraryRequested()

    // 0 folded .. 1 open: what everything that moves follows, the panel under
    // it too.
    property real progress: expanded ? 1 : 0
    Behavior on progress { NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic } }

    readonly property real _foldedWidth: Theme.touchLarge + Theme.touch
    width: _foldedWidth + (fullWidth - _foldedWidth) * progress
    height: Theme.touchLarge

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.surface200
        opacity: 0.82
    }

    AbstractButton {
        id: sourcePart
        objectName: "sourceBarSource"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: arrow.left
        focusPolicy: Qt.NoFocus
        enabled: bar.library !== null
        onClicked: bar.libraryRequested()
        background: Rectangle {
            radius: Theme.radius
            color: sourcePart.down ? "#26FFFFFF" : "transparent"
        }
        contentItem: Item {
            clip: true
            Item {
                id: icon
                x: (Theme.touchLarge - width) / 2
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(28)
                height: Theme.dp(28)
                IconImage {
                    anchors.fill: parent
                    visible: bar.library && bar.library.activeSourceIcon !== ""
                    source: bar.library ? bar.library.activeSourceIcon : ""
                    sourceSize.width: width
                    sourceSize.height: height
                    color: Theme.accent
                }
                Image {
                    anchors.fill: parent
                    visible: !bar.library || bar.library.activeSourceIcon === ""
                    source: "qrc:/icons/logo.svg"
                    sourceSize.width: width
                    sourceSize.height: height
                }
            }
            ColumnLayout {
                x: Theme.touchLarge
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, bar.fullWidth - bar._foldedWidth - Theme.dp(8))
                spacing: 0
                opacity: Math.max(0, bar.progress * 2 - 1)
                visible: opacity > 0
                Text {
                    Layout.fillWidth: true
                    text: bar.library && bar.library.activeSourceName ? bar.library.activeSourceName : "CloudMus"
                    color: Theme.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: bar.playlist && bar.playlist.title ? bar.playlist.title + " · " + bar.playlist.count : ""
                    color: Theme.inkSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
            }
        }
    }

    // Between the two parts, while they read as two.
    Rectangle {
        anchors.right: arrow.left
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.dp(1)
        height: parent.height * 0.5
        color: Theme.borderStrong
        opacity: 1 - bar.progress
    }

    AbstractButton {
        id: arrow
        objectName: "sourceBarToggle"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.touch
        focusPolicy: Qt.NoFocus
        onClicked: bar.expanded = !bar.expanded
        background: Rectangle {
            radius: Theme.radius
            color: arrow.down ? "#26FFFFFF" : "transparent"
        }
        contentItem: Item {
            IconImage {
                anchors.centerIn: parent
                source: "qrc:/icons/chevron_right.svg"
                sourceSize.width: Theme.dp(26)
                sourceSize.height: Theme.dp(26)
                color: Theme.inkSecondary
                rotation: 180 * bar.progress
            }
        }
    }
}
