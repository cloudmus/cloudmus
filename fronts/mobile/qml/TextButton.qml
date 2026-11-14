import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A labelled button in the theme's colors.
AbstractButton {
    id: control

    Layout.fillWidth: false
    // A spinner in place of the label while something's under way; `tint`
    // paints the button with an outcome (Theme.success / Theme.danger).
    property bool busy: false
    property color tint: "transparent"
    implicitHeight: Theme.touch
    implicitWidth: label.implicitWidth + Theme.dp(32)
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: height / 2
        color: control.tint.a > 0 ? control.tint : !control.enabled ? Theme.surface200 : (control.down ? Theme.surface400 : Theme.surface300)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    contentItem: Item {
        Spinner {
            anchors.centerIn: parent
            width: Theme.dp(22)
            height: Theme.dp(22)
            running: control.busy
            color: Theme.ink
        }
        Text {
            id: label
            anchors.fill: parent
            visible: !control.busy
            text: control.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: control.tint.a > 0 ? "#FFFFFF" : control.enabled ? Theme.ink : Theme.inkTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
        }
    }
}
