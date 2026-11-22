import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A question before something that can't be undone, such as deleting:
// a heading, a line on what happens, Cancel and the action. ask() opens it;
// `accepted` comes on the action, only.
Popup {
    id: dialog

    property string heading
    property string text
    property string actionText
    property bool danger: true
    signal accepted()

    function ask(heading, text, actionText) {
        dialog.heading = heading
        dialog.text = text
        dialog.actionText = actionText
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.padding * 2 : 0, Theme.dp(400))
    modal: true
    dim: true
    padding: Theme.dp(20)
    background: Rectangle {
        radius: Theme.dp(24)
        color: Theme.surface200
    }
    Overlay.modal: Rectangle { color: "#99000000" }

    contentItem: ColumnLayout {
        spacing: Theme.dp(12)
        Text {
            Layout.fillWidth: true
            text: dialog.heading
            color: Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            wrapMode: Text.Wrap
        }
        BodyText {
            Layout.fillWidth: true
            visible: text !== ""
            text: dialog.text
            color: Theme.inkSecondary
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: Theme.dp(8)
            spacing: Theme.spacing
            TextButton {
                objectName: "confirmCancel"
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            TextButton {
                objectName: "confirmAction"
                text: dialog.actionText
                tint: dialog.danger ? Theme.danger : Theme.accent
                onClicked: {
                    dialog.close()
                    dialog.accepted()
                }
            }
        }
    }
}
