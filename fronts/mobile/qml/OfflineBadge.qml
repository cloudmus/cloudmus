import QtQuick
import QtQuick.Controls.impl
import QtQuick.Layouts

// No network: a quiet state to see where the source and its playlists are
// shown, in place of an error for each thing that doesn't work because of it.
RowLayout {
    id: badge

    property QtObject backend
    // Only the icon, for a narrow bar.
    property bool iconOnly: false

    objectName: "offlineBadge"
    visible: backend !== null && backend.mainScreen !== null && backend.mainScreen.offline === true
    spacing: Theme.dp(6)

    IconImage {
        source: "qrc:/icons/cloud_off.svg"
        sourceSize.width: Theme.dp(20)
        sourceSize.height: Theme.dp(20)
        color: Theme.inkSecondary
    }
    Text {
        visible: !badge.iconOnly
        text: qsTr("No connection")
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }
}
