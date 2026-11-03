import QtQuick
import QtQuick.Window
import QtQuick.Controls.impl

// A track's cover with rounded corners, or a note on a surface when it has
// none. The corners are in the image itself: see CoverImageProvider.
//
// Nothing blinks. A new picture (another cover, or the cover coming to replace
// the note) loads behind the scenes and fades in over what's there, which goes
// only then. While the size changes, what's there stretches; once it has
// settled, a picture of the new size comes the same way.
Rectangle {
    id: cover

    property string source
    // With no cover: generated art from this, as on the desktop, if set.
    property string seed
    // An accent line around it, for the one that's active.
    property bool outlined: false

    radius: Theme.dp(6)
    color: _showing ? "transparent" : Theme.surface300

    // What's to be shown, drawn at the size it is shown at: its corners are
    // right then, and stretching the picture keeps them in proportion.
    property int _side: 0
    readonly property string _wanted: {
        if (_side <= 0 || (source === "" && seed === ""))
            return ""
        const prefix = "image://cover/" + Math.round(radius * Screen.devicePixelRatio) + "/"
        return prefix + (source !== "" ? "f" + encodeURIComponent(source) : "s" + encodeURIComponent(seed))
    }
    // The picture on screen, and whether there is one.
    readonly property string shownSource: _front.source.toString()
    readonly property bool _showing: _front.status === Image.Ready

    property Image _front: first
    property Image _back: second

    function _pixels() { return Math.round(width * Screen.devicePixelRatio) }
    function _settleSize() {
        const pixels = _pixels()
        if (pixels > 0 && Math.abs(pixels - _side) > 2)
            _side = pixels
    }
    onWidthChanged: {
        // The first size goes at once, a change waits for the last of it.
        if (_side === 0)
            firstSize.restart()
        else
            settling.restart()
    }
    Component.onCompleted: _settleSize()
    // Next turn of the event loop, once a layout has had its say. Timers, not
    // Qt.callLater(): they go with the cover if a list drops it meanwhile.
    Timer {
        id: firstSize
        interval: 0
        onTriggered: cover._settleSize()
    }
    Timer {
        id: settling
        interval: 300
        onTriggered: cover._settleSize()
    }
    Timer {
        id: loading
        interval: 0
        onTriggered: cover._load()
    }

    on_WantedChanged: loading.restart()

    function _load() {
        fade.stop()
        if (_back.opacity > 0)
            _swap() // one still fading in: it's the one showing now
        if (_wanted === "") {
            _front.source = ""
            _back.source = ""
            return
        }
        if (_front.source.toString() === _wanted && _side === _front.sourceSize.width)
            return
        _back.source = ""
        _back.sourceSize = Qt.size(_side, _side)
        _back.source = _wanted
        // At hand already (the cache): no need to make a show of it.
        if (_back.status === Image.Ready)
            _reveal(false)
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
        duration: 250
        easing.type: Easing.InOutQuad
        onFinished: cover._swap()
    }

    IconImage {
        anchors.centerIn: parent
        visible: !cover._showing
        source: "qrc:/icons/music_note.svg"
        sourceSize.width: Math.round(cover.width * 0.45)
        sourceSize.height: Math.round(cover.width * 0.45)
        color: Theme.inkTertiary
    }

    Image {
        id: first
        anchors.fill: parent
        asynchronous: true
        opacity: 0
        visible: opacity > 0
        onStatusChanged: cover._loaded(first)
    }
    Image {
        id: second
        anchors.fill: parent
        asynchronous: true
        opacity: 0
        visible: opacity > 0
        onStatusChanged: cover._loaded(second)
    }

    Rectangle {
        anchors.fill: parent
        z: 2
        visible: cover.outlined
        radius: cover.radius
        color: "transparent"
        border.width: Theme.dp(3)
        border.color: Theme.accent
    }
}
