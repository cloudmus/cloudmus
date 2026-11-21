import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// An entry of a DropMenu: an icon (from the icon set) and its label. When it
// isn't `available` it leaves no gap.
MenuItem {
    id: item

    property string iconName
    property bool available: true
    // 0 → 1 as it comes in; the menu starts it with appear().
    property real _shown: 1

    // Comes in after `delay` ms: fades in, sliding down into its place.
    function appear(delay) {
        appearing.stop()
        _shown = 0
        wait.duration = delay
        appearing.start()
    }

    visible: available
    height: available ? Theme.touch : 0
    opacity: _shown
    transform: Translate { y: -Theme.dp(8) * (1 - item._shown) }

    SequentialAnimation {
        id: appearing
        PauseAnimation { id: wait }
        NumberAnimation { target: item; property: "_shown"; to: 1; duration: 200; easing.type: Easing.OutCubic }
    }

    contentItem: RowLayout {
        spacing: Theme.dp(14)
        IconImage {
            Layout.leftMargin: Theme.dp(8)
            source: item.iconName ? "qrc:/icons/" + item.iconName + ".svg" : ""
            sourceSize.width: Theme.dp(22)
            sourceSize.height: Theme.dp(22)
            color: Theme.ink
        }
        Text {
            Layout.fillWidth: true
            text: item.text
            color: Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: item.down && ripple.flat ? Theme.surface400 : "transparent"
        Ripple {
            id: ripple
            pressed: item.pressed
            pressX: item.pressX
            pressY: item.pressY
            radius: parent.radius
        }
    }
}
