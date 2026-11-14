import QtQuick
import QtQuick.Controls.impl
import QtQuick.Layouts

// Not all online: no network on the device, or a service that can't be
// reached. A quiet state to see where the source and its playlists are
// shown, in place of an error for each thing that doesn't work because of
// it. A tap checks every service now and tells what's known.
RowLayout {
    id: badge

    property QtObject backend
    // Only the icon, for a narrow bar.
    property bool iconOnly: false
    readonly property QtObject screen: backend !== null ? backend.mainScreen : null

    objectName: "offlineBadge"
    visible: screen !== null && screen.offline === true
    spacing: Theme.dp(6)

    IconImage {
        source: "qrc:/icons/cloud_off.svg"
        sourceSize.width: Theme.dp(20)
        sourceSize.height: Theme.dp(20)
        color: Theme.inkSecondary
        // A check under way.
        opacity: badge.screen !== null && badge.screen.checkingConnection ? 0.4 : 1
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
    Text {
        visible: !badge.iconOnly
        text: badge.screen !== null && badge.screen.connectionText !== "" ? badge.screen.connectionText
                                                                          : qsTr("No connection")
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideRight
        Layout.maximumWidth: Theme.dp(180)
    }

    TapHandler {
        onTapped: {
            if (badge.screen !== null)
                badge.screen.checkConnection()
        }
    }
}
