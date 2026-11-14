import QtQuick
import QtQuick.Layouts

// What the app has to tell the user, at the bottom: a short line, a × that
// closes it, and — when there is a technical line behind it — a chevron that
// unfolds that line (the toast then stays until it is closed). A long press
// copies what is shown.
Rectangle {
    id: toast

    // `isError`: told apart by its color.
    property bool isError: false
    property bool isSuccess: false
    property string details: ""
    property bool expanded: false
    // For copying.
    property QtObject platform
    property string message: ""
    readonly property string shownText: expanded && details !== "" ? message + "\n\n" + details : message
    readonly property color inkColor: isError || isSuccess ? "#FFFFFF" : Theme.ink

    function show(text, error, detail, success) {
        isError = error === true
        isSuccess = success === true
        message = text
        details = detail === undefined ? "" : detail
        expanded = false
        opacity = 1
        hideTimer.restart()
    }
    function hide() {
        hideTimer.stop()
        opacity = 0
        expanded = false
    }

    onExpandedChanged: {
        if (expanded)
            hideTimer.stop() // what's being read doesn't go away by itself
        else if (opacity > 0)
            hideTimer.restart()
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.dp(96) + Theme.insetBottom
    width: Math.min(parent.width - Theme.padding * 2, Theme.dp(560))
    height: column.implicitHeight + Theme.dp(8) * 2
    radius: Theme.radius
    color: isError ? Theme.danger : (isSuccess ? Theme.success : Theme.surface400)
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    MouseArea {
        anchors.fill: parent
        onPressAndHold: if (toast.platform) toast.platform.copyText(toast.shownText)
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.dp(4)
        spacing: Theme.dp(4)

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: toast.message
                wrapMode: Text.Wrap
                color: toast.inkColor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
            IconButton {
                objectName: "toastChevron"
                visible: toast.details !== ""
                size: Theme.dp(40)
                iconName: toast.expanded ? "expand_less" : "expand_more"
                color: toast.inkColor
                onClicked: toast.expanded = !toast.expanded
            }
            IconButton {
                objectName: "toastClose"
                size: Theme.dp(40)
                iconName: "close"
                color: toast.inkColor
                onClicked: toast.hide()
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.rightMargin: Theme.dp(12)
            Layout.bottomMargin: Theme.dp(8)
            visible: toast.expanded && toast.details !== ""
            text: toast.details
            wrapMode: Text.WrapAnywhere
            color: toast.inkColor
            opacity: 0.8
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall - 1
        }
    }

    Timer {
        id: hideTimer
        interval: 4000
        onTriggered: toast.opacity = 0
    }
}
