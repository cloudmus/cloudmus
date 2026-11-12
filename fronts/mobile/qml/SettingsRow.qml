import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// A cell of a SettingsGroup that leads somewhere or does something.
AbstractButton {
    id: row

    property string iconSource
    property string subtitle
    property bool chevron: true
    property bool roundTop: true
    property bool roundBottom: true

    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.touchLarge, content.implicitHeight + Theme.padding * 1.5)
    leftPadding: Theme.padding
    rightPadding: Theme.padding
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        color: row.down ? Theme.surface300 : Theme.surface200
        topLeftRadius: row.roundTop ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
        topRightRadius: topLeftRadius
        bottomLeftRadius: row.roundBottom ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
        bottomRightRadius: bottomLeftRadius
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    contentItem: RowLayout {
        id: content
        spacing: Theme.padding
        IconImage {
            visible: row.iconSource !== ""
            source: row.iconSource
            sourceSize: Qt.size(Theme.dp(24), Theme.dp(24))
            color: Theme.ink
            Layout.preferredWidth: Theme.dp(24)
            Layout.preferredHeight: Theme.dp(24)
        }
        SettingText { title: row.text; description: row.subtitle }
        IconImage {
            visible: row.chevron
            source: "qrc:/icons/chevron_right.svg"
            sourceSize: Qt.size(Theme.dp(22), Theme.dp(22))
            color: Theme.inkTertiary
            Layout.preferredWidth: Theme.dp(22)
            Layout.preferredHeight: Theme.dp(22)
        }
    }
}
