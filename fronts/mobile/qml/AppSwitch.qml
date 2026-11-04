import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A switch in the theme's colors, with room for a finger.
Switch {
    id: control

    Layout.fillWidth: false
    focusPolicy: Qt.NoFocus
    padding: 0
    spacing: 0
    implicitWidth: Theme.dp(52)
    implicitHeight: Theme.touch

    indicator: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: Theme.dp(32)
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.surface300
        border.width: control.checked ? 0 : Theme.dp(2)
        border.color: Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            readonly property int side: control.checked || control.down ? Theme.dp(24) : Theme.dp(16)
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - Theme.dp(4) : Theme.dp(8) - (side - Theme.dp(16)) / 2
            width: side
            height: side
            radius: side / 2
            color: control.checked ? Theme.onAccent : Theme.inkSecondary
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
            Behavior on height { NumberAnimation { duration: Theme.durationFast } }
        }
    }
    contentItem: Item { }
}
