import QtQuick
import QtQuick.Controls.impl

// Over a list whose filter matches none of its rows. It rises into view and
// sinks out of it as `shown` changes: set that, never `visible`.
Item {
    id: placeholder

    property bool shown: false
    // 0 gone .. 1 here.
    property real presence: shown ? 1 : 0
    Behavior on presence {
        NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic }
    }
    visible: presence > 0
    opacity: presence

    Column {
        id: content
        anchors.centerIn: parent
        // Off its place only on the way there.
        anchors.verticalCenterOffset: (1 - placeholder.presence) * Theme.dp(24)
        width: Math.min(parent.width - Theme.padding * 2, Theme.dp(360))
        spacing: Theme.spacing

        IconImage {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/icons/search.svg"
            sourceSize: Qt.size(Theme.dp(56), Theme.dp(56))
            color: Theme.inkTertiary
            scale: 0.6 + 0.4 * placeholder.presence
        }
        Text {
            width: parent.width
            text: qsTr("Nothing found")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: qsTr("Try another title, artist or album")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.inkSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }
}
