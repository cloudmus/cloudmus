import QtQuick

// A scrollable view's items dissolving into its background at the top and
// bottom, where there's more beyond that edge, as on the desktop
// (Ui::ScrollEdgeFade): the top one only while scrolled away from the top, the
// bottom one only while not at the bottom, each coming and going smoothly. For
// a view scrolled sideways (Qt.Horizontal), at its left and right instead.
Item {
    id: fade

    required property Flickable view
    property color color: Theme.surface0
    property int orientation: Qt.Vertical

    readonly property bool _horizontal: orientation === Qt.Horizontal
    readonly property bool moreAbove: !_horizontal && view.contentY > view.originY + 1
    readonly property bool moreBelow: !_horizontal && view.contentY + view.height < view.originY + view.contentHeight - 1
    readonly property bool moreBefore: _horizontal && view.contentX > view.originX + 1
    readonly property bool moreAfter: _horizontal && view.contentX + view.width < view.originX + view.contentWidth - 1
    readonly property int _height: Math.min(Theme.dp(28), height / 2)
    readonly property int _width: Math.min(Theme.dp(28), width / 2)

    anchors.fill: parent
    z: 10

    function _stop(alpha) { return Qt.rgba(color.r, color.g, color.b, alpha) }

    component Edge: Rectangle {
        property bool shown
        // From the edge's own side: solid there, clear inwards.
        property bool fromEnd
        property int gradientOrientation: Gradient.Vertical
        visible: opacity > 0
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        gradient: Gradient {
            orientation: gradientOrientation
            GradientStop { position: 0.0; color: fade._stop(fromEnd ? 0.0 : 1.0) }
            GradientStop { position: 0.5; color: fade._stop(0.55) }
            GradientStop { position: 1.0; color: fade._stop(fromEnd ? 1.0 : 0.0) }
        }
    }

    Edge {
        objectName: "fadeTop"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: fade._height
        shown: fade.moreAbove
    }
    Edge {
        objectName: "fadeBottom"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: fade._height
        shown: fade.moreBelow
        fromEnd: true
    }
    Edge {
        objectName: "fadeLeft"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: fade._width
        shown: fade.moreBefore
        gradientOrientation: Gradient.Horizontal
    }
    Edge {
        objectName: "fadeRight"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: fade._width
        shown: fade.moreAfter
        fromEnd: true
        gradientOrientation: Gradient.Horizontal
    }
}
