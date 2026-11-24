import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A slider in the theme's colors. With `bubbleText`, a bubble over the
// handle says it while it's dragged.
Slider {
    id: control

    property string bubbleText

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

    ToolTip {
        parent: control.handle
        visible: control.pressed && control.bubbleText !== ""
        text: control.bubbleText
        y: -height - Theme.dp(8)
        x: (parent.width - width) / 2
        padding: Theme.dp(8)
        leftPadding: Theme.dp(12)
        rightPadding: Theme.dp(12)
        delay: 0
        timeout: -1
        contentItem: Text {
            text: control.bubbleText
            color: Theme.inkOnAccent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
        }
        background: Rectangle {
            radius: height / 2
            color: Theme.accent
        }
        enter: null
        exit: null
    }
}
