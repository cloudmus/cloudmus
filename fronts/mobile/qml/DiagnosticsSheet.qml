import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// The test bench: the device and screen, autoplay, the UI scale and
// scrolling, and where the log is (the startup timeline is on the Log page,
// "Startup").
Popup {
    id: sheet

    property QtObject backend
    property QtObject platform
    // -1: automatic.
    property real userScale: -1
    property real flickMaxVelocity: Theme.flickMaxVelocityBase
    property real flickDeceleration: Theme.flickDecelerationBase
    property bool showFps: false
    property string renderLoop: ""
    property bool touchCompression: false
    signal logRequested()
    signal stopRequested()

    // How far it's out: 1 open, 0 gone. A Popup, not a Drawer: Drawer's own
    // swipe took vertical drags from the list inside and closed the sheet
    // instead of scrolling it, and without it a Drawer stops closing on a
    // tap outside. As Android's bottom sheets: a drag scrolls the list, and
    // only once it's at the top does pulling further down take the sheet
    // with it (see `list`); the handle drags it too.
    property real position: 0

    x: 0
    y: parent.height - height * position
    width: parent.width
    height: Math.min(parent.height * 0.9, handle.height + content.implicitHeight + Theme.padding * 2)
    padding: 0
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition {
        NumberAnimation { property: "position"; from: 0; to: 1; duration: Theme.durationPage; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "position"; to: 0; duration: Theme.durationPage; easing.type: Easing.InCubic }
    }

    // Lets go of a drag: back up, or closed if pulled down far enough — or
    // flicked down, however short the pull was, as Android's sheets do.
    // open() wouldn't bring it back: to Qt the sheet is still open.
    function settle() {
        if (sheet.position < 0.85 || sheet.releaseSpeed() > Theme.dp(400))
            sheet.close()
        else
            backUp.restart()
    }

    // Follows a drag, remembering the last ~100 ms of where it was, to know
    // how fast it was let go. (Qt's own velocity of the gesture needs a dozen
    // events to settle: wrong for a short flick.)
    property var drag: []
    function moveTo(position) {
        const now = Date.now()
        sheet.drag.push({ t: now, y: (1 - position) * sheet.height })
        while (sheet.drag.length > 2 && now - sheet.drag[0].t > 100)
            sheet.drag.shift()
        sheet.position = Math.max(0, Math.min(1, position))
    }
    // Downward speed at the end of the drag, px/s: none if the finger had
    // stopped before it was let go.
    function releaseSpeed() {
        const samples = sheet.drag
        sheet.drag = []
        if (samples.length < 2 || Date.now() - samples[samples.length - 1].t > 100)
            return 0
        const first = samples[0]
        const last = samples[samples.length - 1]
        return last.t > first.t ? (last.y - first.y) * 1000 / (last.t - first.t) : 0
    }
    NumberAnimation {
        id: backUp
        target: sheet
        property: "position"
        to: 1
        duration: Theme.durationPage
        easing.type: Easing.OutCubic
    }
    background: Rectangle {
        color: Theme.surface100
        radius: Theme.dp(18)
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.radius
            color: parent.color
        }
    }

    Item {
        id: handle
        anchors.top: parent.top
        width: parent.width
        height: Theme.touch

        Rectangle {
            anchors.centerIn: parent
            width: Theme.dp(40)
            height: Theme.dp(4)
            radius: height / 2
            color: Theme.borderStrong
        }

        DragHandler {
            target: null
            xAxis.enabled: false
            onTranslationChanged: if (active) sheet.moveTo(1 - translation.y / sheet.height)
            onActiveChanged: if (!active) sheet.settle()
        }
        TapHandler { onTapped: sheet.close() }
    }

    ScrollArea {
        id: list
        anchors.fill: parent
        anchors.topMargin: handle.height
        // Dragging past the top is allowed but doesn't move the content
        // (ScrollArea's boundsMovement): the overshoot moves the sheet.
        boundsBehavior: Flickable.DragOverBounds
        onVerticalOvershootChanged: {
            if (dragging && verticalOvershoot < 0)
                sheet.moveTo(1 + verticalOvershoot / sheet.height)
        }
        onDraggingChanged: if (!dragging && sheet.position < 1) sheet.settle()
        anchors.leftMargin: Theme.padding * 1.5
        anchors.rightMargin: Theme.padding * 1.5
        anchors.bottomMargin: Theme.padding
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: Theme.padding

            component Heading: Text {
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.dp(1)
            }
            component Body: Text {
                Layout.fillWidth: true
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            Heading { text: qsTr("Device") }
            Body { text: sheet.platform.deviceInfo }

            Heading { text: qsTr("Playback") }
            TextButton {
                text: qsTr("Stop and close")
                onClicked: sheet.stopRequested()
            }
            RowLayout {
                Layout.fillWidth: true
                Body {
                    text: qsTr("Play when the system starts")
                }
                Switch {
                    id: autoplay
                    checked: sheet.backend.settings ? sheet.backend.settings.autoplay : false
                    onToggled: sheet.backend.settings.setAutoplay(checked)
                }
            }

            Heading { text: qsTr("Interface scale") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Slider {
                    id: scaleSlider
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 0.75
                    to: 2.5
                    stepSize: 0.05
                    value: Theme.scale
                    onMoved: sheet.userScale = value
                }
                Body {
                    Layout.fillWidth: false
                    text: Theme.scale.toFixed(2) + "×"
                }
                TextButton {
                    text: qsTr("Auto")
                    enabled: sheet.userScale > 0
                    onClicked: sheet.userScale = -1
                }
            }

            Heading { text: qsTr("Scrolling") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Body { Layout.fillWidth: false; text: qsTr("Fling speed") }
                Slider {
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 2000; to: 20000; stepSize: 500
                    value: sheet.flickMaxVelocity
                    onMoved: sheet.flickMaxVelocity = value
                }
                Body { Layout.fillWidth: false; text: Math.round(sheet.flickMaxVelocity) }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Body { Layout.fillWidth: false; text: qsTr("Deceleration") }
                Slider {
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 500; to: 12000; stepSize: 250
                    value: sheet.flickDeceleration
                    onMoved: sheet.flickDeceleration = value
                }
                Body { Layout.fillWidth: false; text: Math.round(sheet.flickDeceleration) }
            }
            RowLayout {
                Layout.fillWidth: true
                Body { text: qsTr("Show frames per second") }
                Switch {
                    checked: sheet.showFps
                    onToggled: sheet.showFps = checked
                }
            }

            // Experiments for a finger drag that stutters at a steady frame
            // rate: how Qt draws and how it hands touches to the views.
            Heading { text: qsTr("Drawing (after a restart)") }
            RowLayout {
                spacing: Theme.spacing
                Body { Layout.fillWidth: false; text: qsTr("Render loop") }
                Repeater {
                    model: [{ t: qsTr("Default"), v: "" }, { t: "threaded", v: "threaded" }, { t: "basic", v: "basic" }]
                    Chip {
                        required property var modelData
                        text: modelData.t
                        checked: sheet.renderLoop === modelData.v
                        onClicked: sheet.renderLoop = modelData.v
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Body { text: qsTr("Merge touch moves into one per frame") }
                Switch {
                    checked: sheet.touchCompression
                    onToggled: sheet.touchCompression = checked
                }
            }

            Heading { text: qsTr("Log") }
            Body {
                text: sheet.platform.publicLogDirectory
                    ? qsTr("Copied to %1").arg(sheet.platform.publicLogDirectory)
                    : sheet.platform.logDirectory
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
            RowLayout {
                spacing: Theme.spacing
                TextButton {
                    text: qsTr("Open log")
                    onClicked: {
                        sheet.close()
                        sheet.logRequested()
                    }
                }
                TextButton {
                    text: qsTr("Export zip")
                    onClicked: exportResult.text = sheet.platform.exportLogs()
                }
                // The head unit's launcher and media apps, to decompile.
                TextButton {
                    visible: sheet.platform.android
                    text: qsTr("Export system apps")
                    onClicked: exportResult.text = sheet.platform.exportSystemApps()
                }
            }
            Body {
                id: exportResult
                visible: text.length > 0
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
