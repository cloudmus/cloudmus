import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The position in the track with the times either side; dragging seeks on
// release, so the replica isn't flooded with seeks.
ColumnLayout {
    id: bar

    property real positionMs: 0
    property real durationMs: 0
    // How far the track has been fetched; negative: nothing to fetch.
    property real bufferedMs: -1
    // How far the tracks after it have got (PreloadCells).
    property var preloadFractions: []
    property color inkColor: Theme.ink
    property color inkSecondaryColor: Theme.inkSecondary
    property color troughColor: Theme.surface400
    // Over the generated background: its times get a shadow.
    property bool overAura: false
    readonly property color bufferedColor: overAura ? Qt.rgba(1, 1, 1, 0.6)
        : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
    signal seekRequested(real positionMs)

    spacing: Theme.dp(2)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.dp(6)

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
            color: bar.troughColor
            Behavior on height { NumberAnimation { duration: Theme.durationFast } }

            Rectangle {
                objectName: "buffered"
                visible: bar.bufferedMs > 0 && bar.durationMs > 0
                width: parent.width * Math.min(1, bar.bufferedMs / Math.max(1, bar.durationMs))
                height: parent.height
                radius: height / 2
                color: bar.bufferedColor
                Behavior on width { NumberAnimation { duration: Theme.durationFast } }
            }
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: height / 2
                color: bar.overAura ? Theme.accentOnAura : Theme.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: Theme.dp(slider.pressed ? 22 : 16)
            height: width
            radius: width / 2
            color: bar.inkColor
            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    PreloadCells {
        objectName: "seekPreload"
        Layout.alignment: Qt.AlignVCenter
        fractions: bar.preloadFractions
        troughColor: bar.troughColor
        fillColor: bar.bufferedColor
    }
    }

    RowLayout {
        Layout.fillWidth: true
        ShadowText {
            shadowed: bar.overAura
            text: Theme.formatTime(slider.pressed ? slider.value : bar.positionMs)
            color: bar.inkSecondaryColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.features: { "tnum": 1 }
        }
        Item { Layout.fillWidth: true }
        ShadowText {
            shadowed: bar.overAura
            text: Theme.formatTime(bar.durationMs)
            color: bar.inkSecondaryColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.features: { "tnum": 1 }
        }
    }
}
