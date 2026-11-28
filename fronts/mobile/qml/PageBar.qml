import QtQuick
import QtQuick.Layouts

// The bar on top of a page reached from another one: back, and its title.
Rectangle {
    id: bar

    property string title
    signal backRequested()

    implicitHeight: Theme.barHeight
    color: Theme.surface100

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(4)
        anchors.rightMargin: Theme.dp(4)
        IconButton {
            iconName: "arrow_back"
            onClicked: bar.backRequested()
        }
        Text {
            Layout.fillWidth: true
            text: bar.title
            color: Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
        }
    }
}
