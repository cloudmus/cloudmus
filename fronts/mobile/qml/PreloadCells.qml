import QtQuick

// The tracks downloaded ahead, right after a progress bar and drawn like a
// continuation of it, as on the desktop (Ui::PreloadCells): a cell each, filling
// as it comes. It never grows past a few cells' width: more tracks than fit share
// that as thinner cells, and past the point where one would be a hairline they
// become one bar, filled by how much of them all is there.
Item {
    id: cells

    // How far each track has got, 0 to 1, in play order.
    property var fractions: []
    property color troughColor: Theme.surface400
    property color fillColor: Theme.accent
    property int barHeight: Theme.dp(4)

    // Less than on the desktop: a phone's progress bar has little width to give.
    readonly property int cellWidth: Theme.dp(8)
    readonly property int cellGap: Theme.dp(2)
    readonly property int maxFullCells: 4
    readonly property real maxWidth: maxFullCells * cellWidth + (maxFullCells - 1) * cellGap
    readonly property real tickGap: Theme.dp(1)
    readonly property real minTickWidth: Theme.dp(2)

    readonly property int count: fractions ? fractions.length : 0
    readonly property bool asCells: count <= maxFullCells
    readonly property real tickWidth: count > 0 ? (maxWidth - (count - 1) * tickGap) / count : 0
    readonly property bool asBar: !asCells && tickWidth < minTickWidth
    readonly property int shownCount: asBar ? 1 : count
    readonly property real shownWidth: asBar ? maxWidth : asCells ? cellWidth : tickWidth
    readonly property real shownGap: asCells ? cellGap : tickGap

    function fractionOf(index) {
        if (!asBar)
            return fractions[index] || 0
        let sum = 0
        for (let i = 0; i < count; ++i)
            sum += fractions[i]
        return sum / count
    }

    visible: count > 0
    implicitWidth: count === 0 ? 0 : shownCount * shownWidth + (shownCount - 1) * shownGap
    implicitHeight: barHeight

    Repeater {
        // By count: the cells stay as their progress changes.
        model: cells.shownCount
        Rectangle {
            required property int index
            x: index * (cells.shownWidth + cells.shownGap)
            anchors.verticalCenter: parent.verticalCenter
            width: cells.shownWidth
            height: cells.barHeight
            radius: height / 2
            color: cells.troughColor
            Rectangle {
                objectName: "cellFill"
                width: parent.width * Math.min(1, cells.fractionOf(parent.index))
                height: parent.height
                radius: height / 2
                color: cells.fillColor
                Behavior on width { NumberAnimation { duration: Theme.durationPage } }
            }
        }
    }
}
