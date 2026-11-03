import QtQuick

// A picture that never blinks: a new one (another address, or the same at
// another size) loads behind the scenes and fades in over what's there, which
// goes only then. One the cache has at hand is shown at once, no show made of
// it, so a page can open with its picture already in place if someone has
// loaded that beforehand.
Item {
    id: root

    // What's to be shown: empty for nothing.
    property string target
    property size pixels: Qt.size(0, 0)
    property int fadeDuration: 250

    readonly property bool showing: _front.status === Image.Ready
    readonly property string shownSource: _front.source.toString()
    readonly property int shownWidth: _front.sourceSize.width

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
        fade.stop()
        if (_back.opacity > 0)
            _swap() // one still fading in: it's the one showing now
        if (target === "" || pixels.width <= 0) {
            if (target === "") {
                _front.source = ""
                _front.opacity = 0
                _back.source = ""
            }
            return
        }
        if (_front.source.toString() === target && _front.sourceSize === pixels)
            return
        _back.source = ""
        _back.sourceSize = pixels
        _back.source = target
        // Its status has told so on the way, so a fade may have begun: it mustn't
        // go on to swap the two a second time, leaving the empty one in front.
        if (_back.status === Image.Ready) {
            fade.stop()
            _reveal(false)
        }
    }

    function _reveal(animated) {
        _back.z = 1
        _front.z = 0
        if (animated) {
            _back.opacity = 0
            fade.target = _back
            fade.start()
        } else {
            _back.opacity = 1
            _swap()
        }
    }

    // What came in is what's shown; the other waits for the next.
    function _swap() {
        const old = _front
        _front = _back
        _back = old
        _front.opacity = 1
        _back.opacity = 0
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

    NumberAnimation {
        id: fade
        property: "opacity"
        to: 1
        duration: root.fadeDuration
        easing.type: Easing.InOutQuad
        onFinished: root._swap()
    }

    Image {
        id: first
        anchors.fill: parent
        asynchronous: true
        opacity: 0
        visible: opacity > 0
        onStatusChanged: root._loaded(first)
    }
    Image {
        id: second
        anchors.fill: parent
        asynchronous: true
        opacity: 0
        visible: opacity > 0
        onStatusChanged: root._loaded(second)
    }
}
