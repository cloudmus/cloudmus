import QtQuick
import QtQuick.Controls.impl

// A checkbox's box in the theme's colors: an outline, filling with the accent
// as it's checked, the tick growing out of its middle. Only the look: the row
// it's on takes the press.
Item {
    id: mark

    property bool checked: false
    // Pressed: the box gives a little under the finger.
    property bool down: false
    // Being changed, its answer not back yet: a spinner in the box's place.
    property bool busy: false

    implicitWidth: Theme.dp(24)
    implicitHeight: Theme.dp(24)

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Theme.dp(22)
        height: Theme.dp(22)
        radius: Theme.dp(7)
        color: mark.checked ? Theme.accent : "transparent"
        border.width: mark.checked ? 0 : Theme.dp(2)
        border.color: Theme.borderStrong
        opacity: mark.busy ? 0 : 1
        scale: mark.down ? 0.86 : 1
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

        IconImage {
            anchors.centerIn: parent
            source: "qrc:/icons/check.svg"
            sourceSize.width: Theme.dp(18)
            sourceSize.height: Theme.dp(18)
            color: Theme.inkOnAccent
            scale: mark.checked ? 1 : 0
            // Out quicker than in, with a little overshoot coming in.
            Behavior on scale {
                NumberAnimation {
                    duration: mark.checked ? Theme.durationPage * 0.8 : Theme.durationFast
                    easing.type: mark.checked ? Easing.OutBack : Easing.InCubic
                }
            }
        }
    }

    Spinner {
        anchors.centerIn: parent
        width: box.width
        height: box.height
        running: mark.busy
    }
}
