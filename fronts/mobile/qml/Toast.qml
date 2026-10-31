import QtQuick

// A short message at the bottom that goes away by itself.
Rectangle {
    id: toast

    function show(message) {
        label.text = message
        opacity = 1
        hideTimer.restart()
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.dp(96)
    width: Math.min(parent.width - Theme.padding * 2, label.implicitWidth + Theme.padding * 2)
    height: label.implicitHeight + Theme.dp(20)
    radius: Theme.radius
    color: Theme.surface400
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.parent.width - Theme.padding * 4)
        wrapMode: Text.Wrap
        color: Theme.ink
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }

    Timer {
        id: hideTimer
        interval: 4000
        onTriggered: toast.opacity = 0
    }
}
