import QtQuick
import QtQuick.Layouts

// One block of a SettingsGroup, which says which of its corners are the group's.
Rectangle {
    id: cell

    property bool roundTop: true
    property bool roundBottom: true
    default property alias contents: content.data

    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.touchLarge, content.implicitHeight + Theme.padding * 1.5)
    color: Theme.surface200
    topLeftRadius: roundTop ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
    topRightRadius: topLeftRadius
    bottomLeftRadius: roundBottom ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
    bottomRightRadius: bottomLeftRadius

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.padding
        spacing: Theme.spacing
    }
}
