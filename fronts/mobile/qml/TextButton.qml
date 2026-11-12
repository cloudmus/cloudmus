import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A labelled button in the theme's colors.
AbstractButton {
    id: control

    Layout.fillWidth: false
    implicitHeight: Theme.touch
    implicitWidth: label.implicitWidth + Theme.dp(32)
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: height / 2
        color: !control.enabled ? Theme.surface200 : (control.down ? Theme.surface400 : Theme.surface300)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    contentItem: Text {
        id: label
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? Theme.ink : Theme.inkTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
    }
}
