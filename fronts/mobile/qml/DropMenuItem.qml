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
    // Slides up into its place rather than down: in a menu opening upwards.
    property bool _rising: false

    // Comes in after `delay` ms: fades in, sliding down (`rising`: up) into
    // its place.
    function appear(delay, rising) {
        appearing.stop()
        _rising = rising === true
        _shown = 0
        wait.duration = delay
        appearing.start()
    }

    visible: available
    // All of its label in view: what the menu's width goes by. Added up from
    // the label's own width, not the row's: a RowLayout works its implicit
    // width out only once it's laid out on screen, so a menu opening for the
    // first time took the row as narrower than it is and cut its labels.
    readonly property real naturalWidth: leftPadding + _iconMargin + (iconName ? _iconSize : 0) + _spacing
        + Math.ceil(label.implicitWidth) + _labelMargin + rightPadding
    readonly property real _iconSize: Theme.dp(22)
    readonly property real _iconMargin: Theme.dp(8)
    readonly property real _labelMargin: Theme.dp(8)
    readonly property real _spacing: Theme.dp(14)
    height: available ? Theme.touch : 0
    opacity: _shown * (enabled ? 1 : 0.4)
    transform: Translate { y: (item._rising ? 1 : -1) * Theme.dp(8) * (1 - item._shown) }

    SequentialAnimation {
        id: appearing
        PauseAnimation { id: wait }
        NumberAnimation { target: item; property: "_shown"; to: 1; duration: 200; easing.type: Easing.OutCubic }
    }

    contentItem: RowLayout {
        spacing: item._spacing
        IconImage {
            Layout.leftMargin: item._iconMargin
            // Set, not taken from the picture: at a fractional screen density
            // that comes out a hair over, the layout rounds it up, and the
            // label is left a point short.
            Layout.preferredWidth: item._iconSize
            Layout.preferredHeight: item._iconSize
            source: item.iconName ? "qrc:/icons/" + item.iconName + ".svg" : ""
            sourceSize.width: item._iconSize
            sourceSize.height: item._iconSize
            color: Theme.ink
        }
        Text {
            id: label
            Layout.fillWidth: true
            // As much room right of the label as left of the icon.
            Layout.rightMargin: item._labelMargin
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
