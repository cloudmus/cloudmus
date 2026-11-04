import QtQuick
import QtQuick.Effects
import QtQuick.Window

// A line of text over the generated background that, when it doesn't fit, goes
// slowly back and forth rather than ending in dots, dissolving at an edge only
// while there's more of it cut off there. Without a GPU, where a mask doesn't
// work, it's just cut off.
Item {
    id: root

    property alias text: label.text
    property alias color: label.color
    property alias font: label.font
    readonly property real contentWidth: label.implicitWidth
    // Off: stays where it is (scrollOffset), as a line on its way out does.
    property bool active: true
    property int pauseMs: 1800
    property real speed: Theme.dp(36) // per second

    readonly property bool overflowing: label.implicitWidth > width + 0.5 && width > 0
    readonly property real maxOffset: overflowing ? label.implicitWidth - width : 0
    property real scrollOffset: 0
    readonly property real _offset: Math.min(scrollOffset, maxOffset)

    // How much of each edge is dissolved, 0..1: none with nothing cut off there.
    readonly property real _fade: Theme.dp(24)
    readonly property real leftFade: Math.min(1, _offset / _fade)
    readonly property real rightFade: Math.min(1, (maxOffset - _offset) / _fade)
    readonly property bool _masked: GraphicsInfo.api !== GraphicsInfo.Software

    // Its width is the layout's to give: a long title mustn't ask for more.
    implicitWidth: 0
    implicitHeight: label.implicitHeight

    onTextChanged: _start()
    onActiveChanged: _start()
    onVisibleChanged: _start()
    // Settling first: the width changes every frame as the screen changes mode.
    onMaxOffsetChanged: settling.restart()
    Component.onCompleted: _start()
    Timer {
        id: settling
        interval: 300
        onTriggered: root._start()
    }

    function _start() {
        scroll.stop()
        if (!active)
            return
        scrollOffset = 0
        if (overflowing && visible)
            scroll.start()
    }

    SequentialAnimation {
        id: scroll
        loops: Animation.Infinite
        PauseAnimation { duration: root.pauseMs }
        NumberAnimation {
            target: root
            property: "scrollOffset"
            to: root.maxOffset
            duration: Math.max(1, root.maxOffset / root.speed * 1000)
            easing.type: Easing.InOutSine
        }
        PauseAnimation { duration: root.pauseMs }
        NumberAnimation {
            target: root
            property: "scrollOffset"
            to: 0
            duration: Math.max(1, root.maxOffset / root.speed * 1000)
            easing.type: Easing.InOutSine
        }
    }

    // Room above, below and to the sides for the shadow, which the layer would
    // cut off where nothing dissolves.
    readonly property real _pad: Theme.dp(6)

    Item {
        id: mask
        width: content.width
        height: content.height
        visible: false
        layer.enabled: content.layer.enabled
        Rectangle {
            x: root._pad + root._fade
            width: Math.max(0, parent.width - x * 2)
            height: parent.height
            color: "white"
        }
        Rectangle {
            width: root._pad + root._fade
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 1 - root.leftFade) }
                GradientStop { position: 1; color: "white" }
            }
        }
        Rectangle {
            x: parent.width - width
            width: root._pad + root._fade
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "white" }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 1 - root.rightFade) }
            }
        }
    }

    Item {
        id: content
        x: -root._pad
        y: -root._pad
        width: root.width + root._pad * 2
        height: root.height + root._pad * 2
        clip: root.overflowing && !root._masked
        layer.enabled: root.overflowing && root._masked
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            // Follows the mask's whole soft edge rather than cutting it.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        ShadowText {
            id: label
            x: root._pad - root._offset
            y: root._pad
        }
    }
}
