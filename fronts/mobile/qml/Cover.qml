import QtQuick
import QtQuick.Window
import QtQuick.Controls.impl

// A track's cover with rounded corners, or a note on a surface when it has
// none. The corners are in the image itself: see CoverImageProvider.
//
// Nothing blinks: see CrossfadeImage. While the size changes, what's there
// stretches; once it has settled, a picture of the new size comes that way.
Rectangle {
    id: cover

    property string source
    // With no cover: generated art from this, as on the desktop, if set.
    property string seed
    // An accent line around it, for the one that's active.
    property bool outlined: false
    // For one that animates its size: corners masked live, the same at any size
    // along the way. In the picture itself they'd stretch with it, and a list's
    // worth of masks wouldn't scroll for free. Needs a GPU; else as in a list.
    property bool liveCorners: false
    // Another cover comes as on the desktop's hero: see CrossfadeImage.
    property bool zoomOnChange: false
    readonly property bool changing: picture.changing
    readonly property real enteringScale: picture.enteringScale
    readonly property bool _masked: liveCorners && GraphicsInfo.api !== GraphicsInfo.Software

    radius: Theme.dp(6)
    color: _showing ? "transparent" : Theme.surface300

    // What's to be shown, drawn at the size it is shown at: its corners are
    // right then, and stretching the picture keeps them in proportion.
    property int _side: 0
    readonly property string _wanted: {
        if (_side <= 0 || (source === "" && seed === ""))
            return ""
        const prefix = "image://cover/" + (_masked ? 0 : Math.round(radius * Screen.devicePixelRatio)) + "/"
        return prefix + (source !== "" ? "f" + encodeURIComponent(source) : "s" + encodeURIComponent(seed))
    }
    // The picture on screen, and whether there is one.
    readonly property string shownSource: picture.shownSource
    readonly property int shownSide: picture.shownWidth
    readonly property bool _showing: picture.showing

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
    // Next turn of the event loop, once a layout has had its say. A timer, not
    // Qt.callLater(): it goes with the cover if a list drops it meanwhile.
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
    IconImage {
        anchors.centerIn: parent
        visible: !cover._showing
        source: "qrc:/icons/music_note.svg"
        sourceSize.width: Math.round(cover.width * 0.45)
        sourceSize.height: Math.round(cover.width * 0.45)
        color: Theme.inkTertiary
    }

    CrossfadeImage {
        id: picture
        anchors.fill: parent
        target: cover._wanted
        pixels: Qt.size(cover._side, cover._side)
        mask: cover._masked ? maskShape : null
        zoomOnChange: cover.zoomOnChange
    }
    Rectangle {
        id: maskShape
        anchors.fill: parent
        radius: cover.radius
        visible: false
        layer.enabled: cover._masked
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
