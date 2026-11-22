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
    // How much of the filled look it has, for one that turns plain on the way
    // somewhere (Now Playing's play button folding into the mini player's).
    property real fillAmount: filled ? 1 : 0
    property int size: Theme.touch
    property int iconSize: Math.round(size * 0.5)
    // A request it made is under way: a spinner in place of the icon, and
    // further presses do nothing until it's done.
    property bool busy: false

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
    readonly property color _plainInk: enabled ? color : Theme.inkTertiary
    icon.color: fillAmount >= 1 ? Theme.inkOnAccent : fillAmount <= 0 ? _plainInk
        : Qt.tint(_plainInk, Qt.rgba(Theme.inkOnAccent.r, Theme.inkOnAccent.g, Theme.inkOnAccent.b, fillAmount))

    background: Rectangle {
        // A flat highlight only where the ripple can't be drawn.
        readonly property bool _down: control.down && ripple.flat
        radius: width / 2
        readonly property color _solid: _down ? Theme.accentPressed : Theme.accent
        // The ink itself, faint: what's behind (the generated background)
        // shows through.
        readonly property color _plain: Qt.rgba(control.color.r, control.color.g, control.color.b,
                                                _down ? 0.22 : (control.hovered ? 0.12 : 0))
        readonly property real _fill: control.fillAmount
        color: _fill >= 1 ? _solid : _fill <= 0 ? _plain
            : Qt.rgba(_plain.r + (_solid.r - _plain.r) * _fill, _plain.g + (_solid.g - _plain.g) * _fill,
                      _plain.b + (_solid.b - _plain.b) * _fill, _plain.a + (_solid.a - _plain.a) * _fill)
        // Not while it's moving between the two: it'd lag behind.
        Behavior on color {
            enabled: control.fillAmount <= 0 || control.fillAmount >= 1
            ColorAnimation { duration: Theme.durationFast }
        }
        Ripple {
            id: ripple
            pressed: control.pressed
            pressX: control.pressX
            pressY: control.pressY
            radius: parent.radius
            color: control.fillAmount > 0.5 ? Theme.inkOnAccent : control.color
        }
    }

    contentItem: Item {
        IconLabel {
            anchors.centerIn: parent
            icon: control.icon
            display: AbstractButton.IconOnly
            visible: !control.busy
        }
        Spinner {
            anchors.centerIn: parent
            width: control.iconSize
            height: control.iconSize
            lineWidth: Math.max(Theme.dp(2), control.iconSize / 10)
            running: control.busy
            color: control.icon.color
        }
    }

    scale: down ? 0.92 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
}
