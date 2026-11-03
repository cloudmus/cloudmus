import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The current source and playlist, on a head unit: a bar across the track's
// panel in the normal mode of the screen, folding into a button with just the
// source's icon as `folded` goes to 1. Either opens the Library.
AbstractButton {
    id: bar

    property QtObject library
    property QtObject playlist
    property real fullWidth: 0
    property real folded: 0

    width: height + (fullWidth - height) * (1 - folded)
    height: Theme.touchLarge
    focusPolicy: Qt.NoFocus
    enabled: library !== null
    clip: true

    background: Rectangle {
        radius: Theme.radius
        color: bar.down ? Theme.surface300 : Theme.surface200
        // Folded it's only the icon, with the panel behind it; pressed, it shows.
        opacity: bar.down ? 0.82 : 0.82 * (1 - bar.folded)
    }

    contentItem: Item {
        Item {
            x: (bar.height - width) / 2
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
        // Keeps its full width, so that the text isn't laid out anew each frame
        // as the bar folds over it.
        RowLayout {
            x: bar.height
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, bar.fullWidth - bar.height - Theme.dp(6))
            spacing: Theme.dp(10)
            opacity: Math.max(0, 1 - bar.folded * 2)
            visible: opacity > 0
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
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
            IconImage {
                visible: bar.library !== null
                source: "qrc:/icons/expand_more.svg"
                sourceSize.width: Theme.dp(24)
                sourceSize.height: Theme.dp(24)
                color: Theme.inkSecondary
            }
        }
    }
}
