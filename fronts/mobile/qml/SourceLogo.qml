import QtQuick
import QtQuick.Controls.impl

// A source in full color, where it stands for the source rather than being one
// in a list. Without a logo: its icon if there's one, else the app's.
Item {
    id: root

    property string logo
    property string icon

    IconImage {
        anchors.fill: parent
        visible: root.logo === "" && root.icon !== ""
        source: visible ? root.icon : ""
        sourceSize.width: width
        sourceSize.height: height
        color: Theme.accent
    }
    Image {
        objectName: "sourceLogoImage"
        anchors.fill: parent
        visible: root.logo !== "" || root.icon === ""
        source: root.logo !== "" ? root.logo : "qrc:/icons/logo.svg"
        sourceSize.width: width
        sourceSize.height: height
        fillMode: Image.PreserveAspectFit
    }
}
