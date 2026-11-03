import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// A track in a list, marked as on the desktop (TrackRowDelegate): a dot if it's
// downloaded, a line along the bottom while it downloads, a heart if liked or
// disliked, and faded if it can't be played.
ItemDelegate {
    id: row

    // `var`: a replica's rows arrive empty until fetched.
    required property int index
    required property var title
    required property var artist
    required property var durationMs
    required property var coverUrl
    required property var isCurrent
    // Whether it's on disk, or how far its download has got (0..1, else empty).
    required property var cached
    required property var downloadProgress
    required property var liked
    required property var disliked
    required property var unavailable
    readonly property bool downloading: typeof downloadProgress === "number" && downloadProgress >= 0
    readonly property color faintAccent: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
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
        // Its download, along the bottom edge.
        Rectangle {
            objectName: "downloadBar"
            visible: row.downloading
            // From under the title to the row's right margin.
            x: row.leftPadding + Theme.dp(52) + Theme.dp(14)
            anchors.bottom: parent.bottom
            height: Theme.dp(2)
            radius: height / 2
            width: (parent.width - x - row.rightPadding) * (row.downloading ? Math.min(1, row.downloadProgress) : 0)
            color: row.faintAccent
            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    contentItem: RowLayout {
        spacing: Theme.dp(14)
        opacity: row.unavailable === true ? 0.45 : 1
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
        RowLayout {
            spacing: Theme.dp(8)
            Rectangle {
                objectName: "cachedMark"
                visible: row.cached === true
                implicitWidth: Theme.dp(6)
                implicitHeight: Theme.dp(6)
                radius: width / 2
                color: row.faintAccent
            }
            IconImage {
                visible: row.liked === true || row.disliked === true
                source: row.liked === true ? "qrc:/icons/favorite.svg" : "qrc:/icons/heart_off.svg"
                sourceSize.width: Theme.dp(14)
                sourceSize.height: Theme.dp(14)
                color: row.liked === true ? Theme.accent : Theme.inkTertiary
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
