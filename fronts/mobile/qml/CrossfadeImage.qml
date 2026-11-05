import QtQuick
import QtQuick.Window
import QtQuick.Effects

// A picture that never blinks: a new one (another address, or the same at
// another size) loads behind the scenes and fades in over what's there, which
// goes only then. One the cache has at hand is shown at once, no show made of
// it, so a page can open with its picture already in place if someone has
// loaded that beforehand, on a screen where there's nothing to replace.
Item {
    id: root

    // What's to be shown: empty for nothing.
    property string target
    property size pixels: Qt.size(0, 0)
    // What an Image is told: Qt asks a provider for its sourceSize multiplied by
    // the screen's density, so told pixels it made each picture that many times
    // too large, on a phone seven times the memory, with whatever was drawn into
    // it in pixels (a cover's corners) that much too small.
    readonly property size _sourceSize: Qt.size(Math.ceil(pixels.width / Screen.devicePixelRatio),
                                                Math.ceil(pixels.height / Screen.devicePixelRatio))
    property int fadeDuration: 250
    // Another picture coming as on the desktop's hero: the old one shrinks away
    // fading, the new one settles in from larger. The same at another size
    // always just fades in.
    property bool zoomOnChange: false
    // Corners to mask each picture with, live: on each, for a picture growing
    // past the bounds not to be cut off by them.
    property Item mask: null

    readonly property bool showing: _front.status === Image.Ready
    readonly property string shownSource: _front.source.toString()
    readonly property int shownWidth: Math.round(_front.sourceSize.width * Screen.devicePixelRatio)
    readonly property bool changing: change.running
    readonly property real enteringScale: _back.scale

    property Image _front: first
    property Image _back: second

    onTargetChanged: _request()
    onPixelsChanged: _request()
    Component.onCompleted: _load()

    function _request() {
        // Nothing to replace: at once, for it to be there on the first frame.
        // Else on the next turn of the event loop, the two changes in one.
        if (showing)
            loading.restart()
        else
            _load()
    }

    // A timer, not Qt.callLater(): it goes with this if a list drops it meanwhile.
    Timer {
        id: loading
        interval: 0
        onTriggered: root._load()
    }

    function _load() {
        const wasChanging = change.running
        change.stop()
        if (wasChanging)
            _swap() // one still coming in: it's the one showing now
        if (target === "" || pixels.width <= 0) {
            if (target === "") {
                _front.source = ""
                _front.opacity = 0
                _back.source = ""
            }
            return
        }
        if (_front.source.toString() === target && _front.sourceSize === _sourceSize)
            return
        _back.source = ""
        _back.sourceSize = _sourceSize
        _back.source = target
        // At hand already, and its status has told so on the way, so a change may
        // have begun: this is the one that counts. With nothing to replace it's
        // just there, for a page to open with it.
        if (_back.status === Image.Ready) {
            change.stop()
            _reveal(showing)
        }
    }

    function _reveal(animated) {
        _back.z = 1
        _front.z = 0
        if (!animated) {
            _back.opacity = 1
            _swap()
            return
        }
        const zoom = zoomOnChange && _front.source.toString() !== _back.source.toString()
        _back.opacity = 0
        _back.scale = zoom ? 1.5 : 1
        entering.target = _back
        enteringScale_.target = _back
        leaving.target = _front
        leavingScale.target = _front
        entering.duration = enteringScale_.duration = zoom ? 450 : fadeDuration
        entering.easing.type = enteringScale_.easing.type = zoom ? Easing.OutCubic : Easing.InOutQuad
        // The old one stays whole under a plain fade, for nothing to show through.
        leaving.to = zoom ? 0 : 1
        leavingScale.to = zoom ? 0.5 : 1
        leaving.duration = leavingScale.duration = zoom ? 300 : fadeDuration
        change.start()
    }

    // Away with what's shown, at once and with no fade: for a picture that isn't
    // the one before it changed into (a swipe turned the other way), which
    // mustn't be seen as the new one's. Not loaded again here: the target may
    // not have changed yet. On the next turn, the target as it is by then (the
    // same as before too: two tracks with one album's cover).
    function clear() {
        change.stop()
        _front.source = ""
        _front.opacity = 0
        _front.scale = 1
        _back.source = ""
        _back.opacity = 0
        _back.scale = 1
        loading.restart()
    }

    // What came in is what's shown; the other waits for the next.
    function _swap() {
        const old = _front
        _front = _back
        _back = old
        _front.opacity = 1
        _front.scale = 1
        _back.opacity = 0
        _back.scale = 1
        _back.source = ""
    }

    function _loaded(image) {
        if (image !== _back || image.source.toString() === "")
            return
        if (image.status === Image.Ready)
            _reveal(true)
        else if (image.status === Image.Error)
            image.source = ""
    }

    ParallelAnimation {
        id: change
        NumberAnimation { id: entering; property: "opacity"; to: 1 }
        NumberAnimation { id: enteringScale_; property: "scale"; to: 1 }
        NumberAnimation { id: leaving; property: "opacity"; easing.type: Easing.InCubic }
        NumberAnimation { id: leavingScale; property: "scale"; easing.type: Easing.InCubic }
        onFinished: root._swap()
    }

    Component {
        id: masking
        MultiEffect {
            maskEnabled: true
            maskSource: root.mask
            // A soft step at the edge: antialiased corners.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
    }

    Image {
        id: first
        anchors.fill: parent
        asynchronous: true
        opacity: 0
        visible: opacity > 0
        layer.enabled: root.mask !== null && visible
        layer.effect: masking
        onStatusChanged: root._loaded(first)
    }
    Image {
        id: second
        anchors.fill: parent
        asynchronous: true
        opacity: 0
        visible: opacity > 0
        layer.enabled: root.mask !== null && visible
        layer.effect: masking
        onStatusChanged: root._loaded(second)
    }
}
