import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Window

// One thing the app has to tell the user: a short line, a × that closes it,
// and — when there is a technical line behind it — a chevron that unfolds that
// line (the toast then stays until it is closed). A long press copies what is
// shown. Placed, stacked and slid in by ToastStack; it asks to be closed by
// `closeRequested` (the ×, or its time running out).
Rectangle {
    id: toast

    // `isError`: told apart by its color.
    property bool isError: false
    property bool isSuccess: false
    property string details: ""
    property bool expanded: false
    objectName: "toastCard"
    // For copying.
    property QtObject platform
    property string message: ""
    readonly property string shownText: expanded && details !== "" ? message + "\n\n" + details : message
    // 1 → 0 over `lifetimeMs`: the share of the time left, drawn as a bar.
    property real remaining: 1
    readonly property int lifetimeMs: 4000
    readonly property color inkColor: isError || isSuccess ? "#FFFFFF" : Theme.ink

    signal closeRequested()
    // On its way out: the bar holds where it is.
    function stopCountdown() { countdown.stop() }

    onExpandedChanged: {
        if (expanded)
            countdown.stop() // what's being read doesn't go away by itself
        else
            countdown.restart()
    }
    Component.onCompleted: countdown.start()

    height: column.implicitHeight + Theme.dp(8) * 2
    radius: Theme.radius
    color: isError ? Theme.danger : (isSuccess ? Theme.success : Theme.surface400)
    Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    // A shadow that lifts it off whatever it pops up over. Under the toast
    // (negative z), as its own shape: the toast's text stays out of the effect.
    // Without a GPU an effect shows nothing at all: then it goes without.
    Rectangle {
        z: -1
        anchors.fill: parent
        radius: toast.radius
        color: toast.color
        layer.enabled: toast.visible && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowColor: "#000000"
            shadowOpacity: 0.5
            shadowBlur: 0.6
            shadowVerticalOffset: Theme.dp(2)
        }
    }

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
        anchors.rightMargin: Theme.dp(12)
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
                onClicked: toast.closeRequested()
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

    // The time left, as on the desktop: a thin bar at the right edge that
    // drains from the top. Not while unfolded: nothing runs then.
    Item {
        visible: !toast.expanded
        width: Theme.dp(3)
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(5)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.dp(8)
        anchors.bottomMargin: Theme.dp(8)
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: toast.inkColor
            opacity: 0.25
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * toast.remaining
            radius: width / 2
            color: toast.inkColor
        }
    }

    NumberAnimation {
        id: countdown
        target: toast
        property: "remaining"
        from: 1
        to: 0
        duration: toast.lifetimeMs
        onFinished: toast.closeRequested()
    }
}
