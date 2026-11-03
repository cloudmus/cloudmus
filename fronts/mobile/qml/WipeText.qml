import QtQuick
import QtQuick.Effects
import QtQuick.Window

// A line of text over the generated background that changes like a wipe in Star
// Wars: a soft-edged front crosses it and leaves the new text behind, left to
// right for what comes next (`direction` 1), right to left for what came before
// (-1). Only as long as the longer of the two lines, and only while it's under
// way does it cost anything more than a ShadowText. Without a GPU, where a mask
// doesn't work, one fades into the other.
Item {
    id: root

    property string text
    property int direction: 1
    property color color
    property alias font: incoming.font
    property int duration: 600

    readonly property bool wiping: wipe.running
    readonly property string currentText: incoming.text
    readonly property string previousText: outgoing.text
    readonly property bool _masked: GraphicsInfo.api !== GraphicsInfo.Software

    // Its width is the layout's to give: a long title mustn't ask for more.
    implicitWidth: 0
    implicitHeight: incoming.implicitHeight

    Component.onCompleted: incoming.text = text
    onTextChanged: {
        if (text === incoming.text)
            return
        wipe.stop()
        if (incoming.text === "") {
            incoming.text = text
            return
        }
        // Whatever was coming in is what the front now crosses.
        outgoing.text = incoming.text
        incoming.text = text
        _direction = direction
        _progress = 0
        wipe.start()
    }

    property int _direction: 1
    property real _progress: 1
    NumberAnimation {
        id: wipe
        target: root
        property: "_progress"
        from: 0
        to: 1
        duration: root.duration
        easing.type: Easing.InOutSine
        onFinished: outgoing.text = ""
    }

    // Room around the text for its shadow, which the mask would cut off.
    readonly property real _pad: Theme.dp(6)
    readonly property real _maskWidth: Math.max(1, width + _pad * 2)
    readonly property real _span: Math.min(width, Math.max(outgoing.contentWidth, incoming.contentWidth)) + _pad * 2
    readonly property real _soft: Math.max(Theme.dp(32), _span * 0.3)
    // Where the front has got to, in the mask: from its soft edge to its hard one
    // in the direction it's going, in from outside the text and out past it.
    readonly property real _front: _direction > 0
        ? _progress * (_span + _soft) - _soft
        : _span - _progress * (_span + _soft)
    function _stop(position) { return Math.max(0, Math.min(1, position / _maskWidth)) }

    Rectangle {
        id: mask
        width: root._maskWidth
        height: outgoingLayer.height
        visible: false
        layer.enabled: root._masked && root.wiping
        // The new text shows where this is opaque, the old one where it isn't.
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: root._direction > 0 ? "#FFFFFFFF" : "#00FFFFFF" }
            GradientStop { position: root._stop(root._front); color: root._direction > 0 ? "#FFFFFFFF" : "#00FFFFFF" }
            GradientStop { position: root._stop(root._front + root._soft); color: root._direction > 0 ? "#00FFFFFF" : "#FFFFFFFF" }
            GradientStop { position: 1; color: root._direction > 0 ? "#00FFFFFF" : "#FFFFFFFF" }
        }
    }

    Item {
        id: outgoingLayer
        x: -root._pad
        y: -root._pad
        width: root._maskWidth
        height: root.height + root._pad * 2
        visible: root.wiping
        opacity: root._masked ? 1 : 1 - root._progress
        layer.enabled: root._masked && root.wiping
        layer.effect: MultiEffect {
            maskEnabled: true
            maskInverted: true
            maskSource: mask
            // Follows the mask's whole soft edge rather than cutting it.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        ShadowText {
            id: outgoing
            x: root._pad
            y: root._pad
            width: root.width
            color: root.color
            font: incoming.font
            elide: Text.ElideRight
        }
    }
    Item {
        x: -root._pad
        y: -root._pad
        width: root._maskWidth
        height: outgoingLayer.height
        opacity: root._masked || !root.wiping ? 1 : root._progress
        layer.enabled: root._masked && root.wiping
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            // Follows the mask's whole soft edge rather than cutting it.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        ShadowText {
            id: incoming
            x: root._pad
            y: root._pad
            width: root.width
            color: root.color
            elide: Text.ElideRight
        }
    }
}
