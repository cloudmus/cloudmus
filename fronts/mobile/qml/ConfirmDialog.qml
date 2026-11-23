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
    // Comes up from a little lower and smaller, and goes back the same way.
    // Not y: anchors place the dialog; the share is a translation of its own.
    property real rise: 0
    readonly property real _riseDistance: Theme.dp(32)
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.9; to: 1; duration: 240; easing.type: Easing.OutCubic }
            NumberAnimation { property: "rise"; from: 1; to: 0; duration: 240; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; to: 0.94; duration: Theme.durationFast; easing.type: Easing.InCubic }
            NumberAnimation { property: "rise"; to: 0.5; duration: Theme.durationFast; easing.type: Easing.InCubic }
        }
    }

    background: Rectangle {
        radius: Theme.dp(24)
        color: Theme.surface200
        transform: Translate { y: dialog.rise * dialog._riseDistance }
    }
    // What's behind darkens and lightens with it.
    Overlay.modal: Rectangle {
        color: "#99000000"
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
    }

    contentItem: ColumnLayout {
        spacing: Theme.dp(12)
        transform: Translate { y: dialog.rise * dialog._riseDistance }
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
