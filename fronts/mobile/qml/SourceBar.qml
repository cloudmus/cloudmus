import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// The current source, on a head unit or a phone on its side: its icon and name
// in the normal mode of the screen, folding down to the icon as `folded` goes
// to 1. Acts like the other round buttons either way, and opens the Library.
AbstractButton {
    id: bar

    property QtObject library
    // The most it may take.
    property real fullWidth: 0
    property real folded: 0

    readonly property real _openWidth: Math.min(fullWidth, height + name.implicitWidth + Theme.dp(16))
    width: height + (_openWidth - height) * (1 - folded)
    height: Theme.touchLarge
    focusPolicy: Qt.NoFocus
    enabled: library !== null

    hoverEnabled: true
    scale: down ? 0.92 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

    // The light IconButton has, faint: the generated background shows through.
    background: Rectangle {
        radius: height / 2
        color: "#FFFFFF"
        opacity: bar.down ? 0.22 : (bar.hovered ? 0.12 : 0)
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    padding: 0
    contentItem: Item {
        clip: true
        SourceLogo {
            x: (bar.height - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.dp(28)
            height: Theme.dp(28)
            logo: bar.library && bar.library.activeSourceLogo ? bar.library.activeSourceLogo : ""
            icon: bar.library && bar.library.activeSourceIcon ? bar.library.activeSourceIcon : ""
        }
        // Keeps its full width, so that the text isn't laid out anew each frame
        // as the button folds over it.
        Text {
            id: name
            x: bar.height
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, bar._openWidth - bar.height - Theme.dp(16))
            opacity: Math.max(0, 1 - bar.folded * 2)
            visible: opacity > 0
            text: bar.library && bar.library.activeSourceName ? bar.library.activeSourceName : "CloudMus"
            color: "#FFFFFF"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
    }
}
