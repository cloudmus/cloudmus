import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A slider in the theme's colors.
Slider {
    id: control

    Layout.fillWidth: true
    implicitHeight: Theme.touch
    focusPolicy: Qt.NoFocus

    background: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: Theme.dp(6)
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.surface300
        }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        width: Theme.dp(22)
        height: Theme.dp(22)
        radius: width / 2
        color: Theme.accent
    }
}
