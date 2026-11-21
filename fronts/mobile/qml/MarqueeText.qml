import QtQuick
import QtQuick.Effects
import QtQuick.Window

// A line of text over the generated background that, when it doesn't fit, can be
// moved along (`phase`) rather than ending in dots, dissolving at an edge only
// while there's more of it cut off there. Without a GPU, where a mask doesn't
// work, it's just cut off.
Item {
    id: root

    property alias text: label.text
    property alias color: label.color
    property alias font: label.font
    readonly property real contentWidth: label.implicitWidth
    // How far along what's cut off it has been moved, 0..1: lines moved together
    // share this, and arrive together however long each is. See TrackTitleBlock.
    property real phase: 0

    readonly property bool overflowing: label.implicitWidth > width + 0.5 && width > 0
    readonly property real maxOffset: overflowing ? label.implicitWidth - width : 0
    readonly property real _offset: phase * maxOffset

    // How far in each edge dissolves: as far as it's moved past that edge, up to
    // the full width. Always down to nothing at the edge itself, so that the cut
    // never shows, however little of the text is past it.
    readonly property real _fade: Theme.dp(24)
    readonly property real leftFadeWidth: Math.min(_fade, _offset)
    readonly property real rightFadeWidth: Math.min(_fade, maxOffset - _offset)
    // The same, as a share of the full width.
    readonly property real leftFade: leftFadeWidth / _fade
    readonly property real rightFade: rightFadeWidth / _fade
    readonly property bool _masked: GraphicsInfo.api !== GraphicsInfo.Software

    // Its width is the layout's to give: a long title mustn't ask for more.
    implicitWidth: 0
    implicitHeight: label.implicitHeight

    // Room above, below and to the sides for the shadow, which the layer would
    // cut off where nothing dissolves.
    readonly property real _pad: Theme.dp(6)

    Item {
        id: mask
        width: content.width
        height: content.height
        visible: false
        layer.enabled: content.layer.enabled
        // Between the two soft edges, all of it.
        Rectangle {
            x: root._pad + root.leftFadeWidth
            width: Math.max(0, parent.width - x - root._pad - root.rightFadeWidth)
            height: parent.height
            color: "white"
        }
        // The soft edges, of a fixed size and scaled to how far they reach: a
        // gradient's Rectangle that starts out with no width stays undrawn.
        Rectangle {
            x: root._pad
            width: root._fade
            height: parent.height
            transform: Scale { xScale: root.leftFadeWidth / root._fade }
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 1; color: "white" }
            }
        }
        Rectangle {
            x: parent.width - root._pad - root._fade
            width: root._fade
            height: parent.height
            transform: Scale { origin.x: root._fade; xScale: root.rightFadeWidth / root._fade }
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "white" }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        // Past an edge with nothing cut off there: the room for the shadow.
        Rectangle {
            width: root._pad
            height: parent.height
            color: "white"
            visible: root.leftFadeWidth <= 0
        }
        Rectangle {
            x: parent.width - width
            width: root._pad
            height: parent.height
            color: "white"
            visible: root.rightFadeWidth <= 0
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
