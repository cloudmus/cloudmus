import QtQuick

// Three bars moving out of step, for "this is what plays": not tied to the music.
// They only run while shown.
Item {
    id: bars

    property color color: "#FFFFFF"
    readonly property int barWidth: Theme.dp(4)
    readonly property int gap: Theme.dp(3)

    implicitWidth: barWidth * 3 + gap * 2
    implicitHeight: Theme.dp(20)

    component Bar: Rectangle {
        id: bar

        property int period
        // Where in its swing it starts, as a share of the height.
        property real low
        property real high

        anchors.bottom: parent.bottom
        width: bars.barWidth
        radius: width / 2
        color: bars.color
        height: bars.height * low

        SequentialAnimation on height {
            running: bars.visible
            loops: Animation.Infinite
            NumberAnimation { from: bars.height * bar.low; to: bars.height * bar.high; duration: bar.period; easing.type: Easing.InOutSine }
            NumberAnimation { from: bars.height * bar.high; to: bars.height * bar.low; duration: bar.period; easing.type: Easing.InOutSine }
        }
    }

    Bar { objectName: "bar1"; x: 0; period: 430; low: 0.35; high: 0.85 }
    Bar { objectName: "bar2"; x: bars.barWidth + bars.gap; period: 310; low: 0.55; high: 1 }
    Bar { objectName: "bar3"; x: (bars.barWidth + bars.gap) * 2; period: 520; low: 0.25; high: 0.7 }
}
