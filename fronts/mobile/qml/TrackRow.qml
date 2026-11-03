import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// A track in a list: cover, title, artist, how long it is.
ItemDelegate {
    id: row

    // `var`: a replica's rows arrive empty until fetched.
    required property int index
    required property var title
    required property var artist
    required property var durationMs
    required property var coverUrl
    required property var isCurrent
    readonly property bool current: isCurrent === true
    property bool playing: false

    width: ListView.view.width
    height: Theme.rowHeight + Theme.dp(8)

    background: Rectangle {
        color: row.down ? Theme.surface300 : (row.current ? Theme.surface200 : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle {
            visible: row.current
            width: Theme.dp(3)
            height: parent.height - Theme.dp(16)
            anchors.verticalCenter: parent.verticalCenter
            radius: width / 2
            color: Theme.accent
        }
    }

    contentItem: RowLayout {
        spacing: Theme.dp(14)
        Cover {
            Layout.preferredWidth: Theme.dp(52)
            Layout.preferredHeight: Theme.dp(52)
            source: row.coverUrl || ""

            // What plays now, over its cover.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                visible: row.current && row.playing
                color: "#80000000"
                IconImage {
                    anchors.centerIn: parent
                    source: "qrc:/icons/volume_up.svg"
                    sourceSize.width: Theme.dp(24)
                    sourceSize.height: Theme.dp(24)
                    color: Theme.accent
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.dp(2)
            Text {
                Layout.fillWidth: true
                text: row.title || ""
                color: row.current ? Theme.accent : Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: row.artist || ""
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }
        Text {
            text: Theme.formatTime(row.durationMs)
            color: Theme.inkTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.features: { "tnum": 1 }
        }
    }
}
