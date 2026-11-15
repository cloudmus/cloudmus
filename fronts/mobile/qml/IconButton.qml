import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// A round icon button with a touch target that grows with Theme.scale.
AbstractButton {
    id: control

    property string iconName
    property color color: Theme.ink
    property bool filled: false
    property int size: Theme.touch
    property int iconSize: Math.round(size * 0.5)

    // Controls fill a layout's width by default since Qt 6.5; an icon
    // button keeps its size.
    Layout.fillWidth: false
    implicitWidth: size
    implicitHeight: size
    // A touch leaves the button "hovered" after the finger lifts, which
    // reads as a stuck highlight; hover only matters with a mouse.
    hoverEnabled: Qt.platform.os !== "android"
    focusPolicy: Qt.NoFocus

    icon.source: iconName ? "qrc:/icons/" + iconName + ".svg" : ""
    icon.width: iconSize
    icon.height: iconSize
    icon.color: filled ? Theme.inkOnAccent : (enabled ? color : Theme.inkTertiary)

    background: Rectangle {
        // A flat highlight only where the ripple can't be drawn.
        readonly property bool _down: control.down && ripple.flat
        radius: width / 2
        color: control.filled
            ? (_down ? Theme.accentPressed : Theme.accent)
            // The ink itself, faint: what's behind (the generated background)
            // shows through.
            : Qt.rgba(control.color.r, control.color.g, control.color.b,
                      _down ? 0.22 : (control.hovered ? 0.12 : 0))
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Ripple {
            id: ripple
            pressed: control.pressed
            pressX: control.pressX
            pressY: control.pressY
            radius: parent.radius
            color: control.filled ? Theme.inkOnAccent : control.color
        }
    }

    contentItem: Item {
        IconLabel {
            anchors.centerIn: parent
            icon: control.icon
            display: AbstractButton.IconOnly
        }
    }

    scale: down ? 0.92 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
}
