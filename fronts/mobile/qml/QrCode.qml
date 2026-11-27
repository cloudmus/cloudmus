import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl

// `text` (the button's own) as a QR code, a square as large as the item allows. A tap takes it
// over the whole screen for another device's camera: it grows out of its place
// and goes back into it, and while it's there the screen stays on, at full
// brightness. All of that is the component's own, wherever it's used.
AbstractButton {
    id: control

    // Ui::Platform, for the screen.
    property QtObject platform: null

    readonly property string _source: text !== "" ? "image://qr/" + encodeURIComponent(text) : ""
    readonly property real _side: Math.min(width, height)
    // One picture for the page and the whole screen, so it's drawn once: at the
    // largest it's shown at, whatever the layout does meanwhile. Qt multiplies
    // sourceSize by the screen's density itself.
    readonly property int _drawnSide: Math.min(Screen.width, Screen.height)
    readonly property string _drawn: _drawnSide > 0 ? _source : ""

    implicitWidth: Theme.dp(240)
    implicitHeight: Theme.dp(240)
    padding: 0
    focusPolicy: Qt.NoFocus
    onClicked: full.open()
    // Whatever it's part of has gone (signed in, say): so does the full screen.
    onVisibleChanged: if (!visible) full.close()
    onTextChanged: if (text === "") full.close()
    Component.onDestruction: full.keepAwake(false)

    // Where the code sits in its own place, in the overlay's coordinates.
    function _placeOnScreen() {
        const at = control.mapToItem(full.parent, (width - _side) / 2, (height - _side) / 2)
        return Qt.rect(at.x, at.y, _side, _side)
    }

    background: null
    contentItem: Item {
        Image {
            id: picture
            anchors.centerIn: parent
            width: control._side
            height: control._side
            // Coming in as it's drawn.
            property real _ready: status === Image.Ready ? 1 : 0
            Behavior on _ready { NumberAnimation { duration: Theme.durationFast } }
            // The one on the screen is this one, moved.
            opacity: full.visible ? 0 : _ready
            scale: control.down ? 0.97 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
            source: control._drawn
            sourceSize: Qt.size(control._drawnSide, control._drawnSide)
            // Off the UI thread: drawing it takes long enough to be felt.
            asynchronous: true
            // Smooth at its smaller size on the page too.
            mipmap: true

            // That a tap makes it larger.
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: -Theme.dp(8)
                // Small enough to stay in the code's blank margin.
                width: Theme.dp(32)
                height: width
                radius: width / 2
                color: Theme.accent
                IconImage {
                    anchors.centerIn: parent
                    source: "qrc:/icons/fullscreen.svg"
                    sourceSize: Qt.size(Theme.dp(22), Theme.dp(22))
                    color: Theme.inkOnAccent
                }
            }
        }
    }

    Popup {
        id: full
        objectName: "qrFullScreen"

        // 0 in its place on the page, 1 over the screen.
        property real progress: 0
        property rect from
        property bool _awake: false
        readonly property real _roomWidth: width - Theme.insetLeft - Theme.insetRight
        readonly property real _roomHeight: height - Theme.insetTop - Theme.insetBottom
        readonly property real _target: Math.max(0, Math.min(_roomWidth, _roomHeight) - Theme.padding * 2)

        function keepAwake(on) {
            if (on === _awake || !control.platform)
                return
            _awake = on
            control.platform.setScreenAwake(on)
        }

        parent: control.Overlay.overlay
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        padding: 0
        modal: true
        // Its own backdrop, coming with the code.
        dim: false
        // Back too.
        focus: true
        closePolicy: Popup.CloseOnEscape

        onAboutToShow: {
            from = control._placeOnScreen()
            keepAwake(true)
        }
        onAboutToHide: {
            // The page may have been laid out anew meanwhile (the screen turned).
            if (control.visible)
                from = control._placeOnScreen()
            keepAwake(false)
        }

        enter: Transition {
            NumberAnimation {
                target: full; property: "progress"; from: 0; to: 1
                duration: Theme.durationPage; easing.type: Easing.OutCubic
            }
        }
        exit: Transition {
            NumberAnimation {
                target: full; property: "progress"; to: 0
                duration: Theme.durationPage; easing.type: Easing.OutCubic
            }
        }

        background: Rectangle {
            color: "black"
            opacity: full.progress * 0.94
        }
        contentItem: Item {
            Image {
                objectName: "qrFullScreenImage"
                readonly property real toX: Theme.insetLeft + (full._roomWidth - full._target) / 2
                readonly property real toY: Theme.insetTop + (full._roomHeight - full._target) / 2
                x: full.from.x + (toX - full.from.x) * full.progress
                y: full.from.y + (toY - full.from.y) * full.progress
                width: full.from.width + (full._target - full.from.width) * full.progress
                height: width
                // The page's own picture (the same source and size): there
                // already as the code is tapped.
                source: control._drawn
                sourceSize: Qt.size(control._drawnSide, control._drawnSide)
                asynchronous: true
                mipmap: true
            }
            MouseArea {
                anchors.fill: parent
                onClicked: full.close()
            }
        }
    }
}
