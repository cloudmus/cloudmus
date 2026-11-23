import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// What's being saved, waiting, and what has been since the list was last
// cleared: each with its progress and a way to cancel it. The notification
// leads here too; a head unit, which shows none, has only this and the
// indicator on the main screen.
Page {
    id: page
    bottomPadding: Theme.insetBottom

    property QtObject backend
    property QtObject platform
    signal closeRequested()

    readonly property QtObject screen: backend.downloadsScreen

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: qsTr("Downloads")
        onBackRequested: page.closeRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BodyText {
            Layout.fillWidth: true
            Layout.margins: Theme.padding
            visible: page.screen !== null && page.screen.enabled === false
            text: qsTr("Saving tracks is off. It's turned on in Settings.")
        }

        // Where to, and the actions.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.padding
            Layout.rightMargin: Theme.padding
            Layout.topMargin: Theme.dp(8)
            Layout.bottomMargin: Theme.dp(8)
            visible: page.screen !== null && page.screen.enabled === true
            spacing: Theme.spacing
            Text {
                Layout.fillWidth: true
                text: page.screen ? page.screen.directory : ""
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideMiddle
            }
            TextButton {
                objectName: "cancelAllDownloads"
                visible: page.screen !== null && page.screen.active === true
                text: qsTr("Cancel all")
                onClicked: page.screen.cancelAll()
            }
            TextButton {
                objectName: "clearDownloads"
                visible: page.screen !== null && page.screen.canClear === true
                text: qsTr("Clear")
                onClicked: page.screen.clearFinished()
            }
        }

        ScrollList {
            id: list
            objectName: "downloads"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.backend.downloads
            delegate: Item {
                id: job
                required property var jobId
                required property var title
                required property var status
                required property var detail
                required property var progress
                required property var isRunning
                required property var isOver
                required property var hasFailed
                required property var isCancelled
                required property var isPlaylist
                readonly property bool _measured: typeof progress === "number" && progress >= 0
                width: ListView.view.width
                height: Math.max(Theme.rowHeight, texts.implicitHeight + Theme.dp(20))

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.padding
                    anchors.rightMargin: Theme.dp(4)
                    spacing: Theme.dp(12)
                    // What it is, and how it's going.
                    Item {
                        Layout.preferredWidth: Theme.dp(28)
                        Layout.preferredHeight: Theme.dp(28)
                        ProgressRing {
                            anchors.fill: parent
                            running: job.isRunning === true
                            progress: job._measured ? job.progress : -1
                        }
                        IconImage {
                            anchors.centerIn: parent
                            visible: job.isRunning !== true
                            readonly property string _icon: job.hasFailed === true ? "warning"
                                : job.isCancelled === true ? "close"
                                : job.isOver === true ? "check" : "file_download"
                            source: "qrc:/icons/" + _icon + ".svg"
                            sourceSize: Qt.size(Theme.dp(22), Theme.dp(22))
                            color: job.hasFailed === true ? Theme.danger
                                 : job.isOver === true ? Theme.inkSecondary : Theme.inkTertiary
                        }
                    }
                    ColumnLayout {
                        id: texts
                        Layout.fillWidth: true
                        spacing: Theme.dp(2)
                        Text {
                            Layout.fillWidth: true
                            text: job.title || ""
                            color: Theme.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: job.status || ""
                            color: job.hasFailed === true ? Theme.danger : Theme.inkSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                        // A playlist's track under way.
                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: job.detail || ""
                            color: Theme.inkTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                        }
                    }
                    IconButton {
                        objectName: "cancelDownload"
                        visible: job.isOver !== true
                        iconName: "close"
                        onClicked: page.screen.cancel(job.jobId)
                    }
                }
            }
            BodyText {
                parent: list
                anchors.centerIn: parent
                width: implicitWidth
                visible: list.count === 0 && page.screen !== null && page.screen.enabled === true
                text: qsTr("Nothing is being saved")
            }
        }
    }
}
