import QtQuick
import QtQuick.Controls.Basic

// A small toggle in a filter row.
AbstractButton {
    id: chip
    checkable: true
    implicitHeight: Theme.dp(40)
    implicitWidth: label.implicitWidth + Theme.dp(28)
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: height / 2
        color: chip.checked ? Theme.accent : (chip.down ? Theme.surface400 : Theme.surface300)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    contentItem: Text {
        id: label
        text: chip.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: chip.checked ? Theme.onAccent : Theme.ink
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        font.weight: Font.DemiBold
    }
}
