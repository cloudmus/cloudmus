import QtQuick
import QtQuick.Effects
import QtQuick.Window

// The track's title and artist over the generated background, as one block that
// changes like a wipe in Star Wars: a soft-edged front crosses it and leaves the
// new text behind, left to right for what comes next (`direction` 1), right to
// left for what came before (-1). Only as far as the text goes.
//
// The layers and their masks stay on at rest, the mask wide open, the old text
// behind it. Layers switched on for a wipe come a frame late, and for that frame
// the new text shows whole. Without a GPU, where a mask doesn't work, one block
// fades into the other.
Item {
    id: root

    property string title
    property string artist
    property int direction: 1
    property color titleColor
    property color artistColor
    property int titleSize: Theme.fontTitle + Theme.dp(3)
    property int artistSize: Theme.fontBody
    property int duration: 600

    readonly property bool wiping: wipe.running
    readonly property string currentTitle: incomingTitle.text
    readonly property string currentArtist: incomingArtist.text
    readonly property bool _masked: GraphicsInfo.api !== GraphicsInfo.Software

    // Its width is the layout's to give: a long title mustn't ask for more.
    implicitWidth: 0
    implicitHeight: incoming.implicitHeight

    Component.onCompleted: _showAtOnce()
    onTitleChanged: gathering.restart()
    onArtistChanged: gathering.restart()

    // A title and an artist come one after the other, over a process boundary
    // on Android: they go in one wipe.
    Timer {
        id: gathering
        interval: 30
        onTriggered: root._change()
    }

    function _showAtOnce() {
        outgoingTitle.text = incomingTitle.text = title
        outgoingArtist.text = incomingArtist.text = artist
        _startScrolling()
    }

    function _change() {
        if (title === incomingTitle.text && artist === incomingArtist.text)
            return
        wipe.stop()
        _progress = 1
        // Nothing to wipe away, or nothing to wipe to.
        if ((incomingTitle.text === "" && incomingArtist.text === "") || (title === "" && artist === "")) {
            _showAtOnce()
            return
        }
        // Whatever was coming in is what the front now crosses. All in one go,
        // with the mask shut, between two frames.
        _direction = direction
        _progress = 0
        // Where it had got to, for the old text not to jump as it goes.
        outgoingTitle.text = incomingTitle.text
        outgoingTitle.phase = scrollPhase
        outgoingArtist.text = incomingArtist.text
        outgoingArtist.phase = scrollPhase
        incomingTitle.text = title
        incomingArtist.text = artist
        wipe.start()
        _startScrolling()
    }

    // Lines that don't fit go back and forth as one: a single course for the two,
    // as long as the longer one's way takes, each going its own way along it. On
    // courses of their own, of different lengths, they'd drift apart.
    property int pauseMs: 1800
    property real speed: Theme.dp(36) // per second, on the longer way
    property real scrollPhase: 0
    readonly property real _longestWay: Math.max(incomingTitle.maxOffset, incomingArtist.maxOffset)
    readonly property int _wayDuration: Math.max(1, _longestWay / speed * 1000)
    // Settling first: the width changes every frame as the screen changes mode.
    on_LongestWayChanged: settling.restart()
    onVisibleChanged: _startScrolling()
    Timer {
        id: settling
        interval: 300
        onTriggered: root._startScrolling()
    }
    function _startScrolling() {
        scrolling.stop()
        scrollPhase = 0
        if (_longestWay > 0 && visible)
            scrolling.start()
    }
    SequentialAnimation {
        id: scrolling
        loops: Animation.Infinite
        PauseAnimation { duration: root.pauseMs }
        NumberAnimation {
            target: root
            property: "scrollPhase"
            to: 1
            duration: root._wayDuration
            easing.type: Easing.InOutSine
        }
        PauseAnimation { duration: root.pauseMs }
        NumberAnimation {
            target: root
            property: "scrollPhase"
            to: 0
            duration: root._wayDuration
            easing.type: Easing.InOutSine
        }
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
    }

    // Room around the text for its shadow, which the mask would cut off.
    readonly property real _pad: Theme.dp(6)
    readonly property real _maskWidth: Math.max(1, width + _pad * 2)
    readonly property real _span: Math.min(width, Math.max(outgoingTitle.contentWidth, outgoingArtist.contentWidth,
                                                           incomingTitle.contentWidth, incomingArtist.contentWidth))
                                  + _pad * 2
    readonly property real _soft: Math.max(Theme.dp(32), _span * 0.3)
    // Where the front has got to, in the mask: from its soft edge to its hard one
    // in the direction it's going, in from outside the text and out past it.
    readonly property real _front: _direction > 0
        ? _progress * (_span + _soft) - _soft
        : _span - _progress * (_span + _soft)

    // A solid part and a soft edge that move along, rather than a gradient with
    // moving stops: those misdraw when they meet at an end, as they do at the
    // very start of a wipe, and then the new text shows whole.
    Item {
        id: mask
        width: root._maskWidth
        height: root.height + root._pad * 2
        visible: false
        layer.enabled: root._masked
        // The new text shows where this is opaque, the old one where it isn't.
        // At rest that's everywhere the text is.
        Rectangle {
            color: "white"
            height: parent.height
            x: root._direction > 0 ? 0 : root._front + root._soft
            width: root._direction > 0 ? Math.max(0, root._front) : Math.max(0, parent.width - x)
        }
        Rectangle {
            x: root._front
            width: root._soft
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: root._direction > 0 ? "#FFFFFFFF" : "#00FFFFFF" }
                GradientStop { position: 1; color: root._direction > 0 ? "#00FFFFFF" : "#FFFFFFFF" }
            }
        }
    }

    Item {
        objectName: "outgoingLayer"
        x: -root._pad
        y: -root._pad
        width: root._maskWidth
        height: mask.height
        opacity: root._masked ? 1 : 1 - root._progress
        layer.enabled: root._masked
        layer.effect: MultiEffect {
            maskEnabled: true
            maskInverted: true
            maskSource: mask
            // Follows the mask's whole soft edge rather than cutting it.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        Column {
            x: root._pad
            y: root._pad
            width: root.width
            spacing: Theme.dp(2)
            MarqueeText {
                id: outgoingTitle
                width: parent.width
                color: root.titleColor
                font: incomingTitle.font
            }
            MarqueeText {
                id: outgoingArtist
                width: parent.width
                color: root.artistColor
                font: incomingArtist.font
            }
        }
    }
    Item {
        x: -root._pad
        y: -root._pad
        width: root._maskWidth
        height: mask.height
        opacity: root._masked ? 1 : root._progress
        layer.enabled: root._masked
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        Column {
            id: incoming
            x: root._pad
            y: root._pad
            width: root.width
            spacing: Theme.dp(2)
            MarqueeText {
                id: incomingTitle
                objectName: "incomingTitle"
                phase: root.scrollPhase
                width: parent.width
                color: root.titleColor
                font.family: Theme.fontFamily
                font.pixelSize: root.titleSize
                font.weight: Font.Bold
            }
            MarqueeText {
                id: incomingArtist
                objectName: "incomingArtist"
                phase: root.scrollPhase
                width: parent.width
                color: root.artistColor
                font.family: Theme.fontFamily
                font.pixelSize: root.artistSize
            }
        }
    }
}
