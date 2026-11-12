import QtQuick
import QtQuick.Layouts

// A group of settings: its title in the accent color, and its cells close
// together, as one block with large corners on the outside.
ColumnLayout {
    id: group

    property string title
    // A word on the group as a whole, under its title.
    property string description
    // Only the caller knows if a group has anything to show: a hidden group's
    // cells all read as hidden too, so they can't be asked.
    property bool shown: true
    default property alias cells: column.data

    Layout.fillWidth: true
    visible: shown
    spacing: Theme.dp(8)

    function arrange() {
        const cells = []
        for (const child of column.visibleChildren) {
            if ("roundTop" in child)
                cells.push(child)
        }
        for (let i = 0; i < cells.length; ++i) {
            cells[i].roundTop = i === 0
            cells[i].roundBottom = i === cells.length - 1
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.dp(4)
        visible: group.title !== ""
        text: group.title
        color: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
    }
    Text {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.dp(4)
        Layout.rightMargin: Theme.dp(4)
        visible: group.description !== ""
        text: group.description
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }
    ColumnLayout {
        id: column
        Layout.fillWidth: true
        spacing: Theme.settingsCellGap
        onVisibleChildrenChanged: Qt.callLater(group.arrange)
    }
}
