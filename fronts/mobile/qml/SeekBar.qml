import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The position in the track with the times either side; dragging seeks on
// release, so the replica isn't flooded with seeks.
ColumnLayout {
    id: bar

    property real positionMs: 0
    property real durationMs: 0
    signal seekRequested(real positionMs)

    spacing: Theme.dp(2)

    Slider {
        id: slider
        Layout.fillWidth: true
        from: 0
        to: Math.max(1, bar.durationMs)
        value: pressed ? value : bar.positionMs
        live: true
        onPressedChanged: if (!pressed) bar.seekRequested(value)

        implicitHeight: Theme.touch
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: Theme.dp(slider.pressed ? 6 : 4)
            radius: height / 2
            color: Theme.surface400
            Behavior on height { NumberAnimation { duration: Theme.durationFast } }

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: height / 2
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: Theme.dp(slider.pressed ? 22 : 16)
            height: width
            radius: width / 2
            color: Theme.ink
            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: Theme.formatTime(slider.pressed ? slider.value : bar.positionMs)
            color: Theme.inkSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.features: { "tnum": 1 }
        }
        Item { Layout.fillWidth: true }
        Text {
            text: Theme.formatTime(bar.durationMs)
            color: Theme.inkSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.features: { "tnum": 1 }
        }
    }
}
