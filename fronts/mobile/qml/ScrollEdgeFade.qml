import QtQuick

// A scrollable view's items dissolving into its background at the top and
// bottom, where there's more beyond that edge, as on the desktop
// (Ui::ScrollEdgeFade): the top one only while scrolled away from the top, the
// bottom one only while not at the bottom, each coming and going smoothly.
Item {
    id: fade

    required property Flickable view
    property color color: Theme.surface0

    readonly property bool moreAbove: view.contentY > view.originY + 1
    readonly property bool moreBelow: view.contentY + view.height < view.originY + view.contentHeight - 1
    readonly property int _height: Math.min(Theme.dp(28), height / 2)

    anchors.fill: parent
    z: 10

    function _stop(alpha) { return Qt.rgba(color.r, color.g, color.b, alpha) }

    Rectangle {
        objectName: "fadeTop"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: fade._height
        opacity: fade.moreAbove ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: fade._stop(1.0) }
            GradientStop { position: 0.5; color: fade._stop(0.55) }
            GradientStop { position: 1.0; color: fade._stop(0.0) }
        }
    }
    Rectangle {
        objectName: "fadeBottom"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: fade._height
        opacity: fade.moreBelow ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: fade._stop(0.0) }
            GradientStop { position: 0.5; color: fade._stop(0.55) }
            GradientStop { position: 1.0; color: fade._stop(1.0) }
        }
    }
}
