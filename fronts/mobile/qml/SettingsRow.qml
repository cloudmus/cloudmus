import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// A line that leads to a page of its own: a source, a proxy.
AbstractButton {
    id: row

    property string iconSource
    property string subtitle

    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.touch, content.implicitHeight + Theme.dp(12))
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: Theme.dp(10)
        color: row.down ? Theme.surface400 : Theme.surface200
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    contentItem: RowLayout {
        id: content
        spacing: Theme.padding
        Item { Layout.preferredWidth: Theme.dp(2) }
        IconImage {
            visible: row.iconSource !== ""
            source: row.iconSource
            sourceSize: Qt.size(Theme.dp(24), Theme.dp(24))
            color: Theme.ink
            Layout.preferredWidth: Theme.dp(24)
            Layout.preferredHeight: Theme.dp(24)
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            BodyText { text: row.text; maximumLineCount: 1; elide: Text.ElideRight }
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: row.subtitle
                elide: Text.ElideRight
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
        }
        IconImage {
            source: "qrc:/icons/chevron_right.svg"
            sourceSize: Qt.size(Theme.dp(22), Theme.dp(22))
            color: Theme.inkTertiary
            Layout.preferredWidth: Theme.dp(22)
            Layout.preferredHeight: Theme.dp(22)
        }
    }
}
