import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What's being saved to the download folder, and what has been. Saving is off
// until asked for, as on the desktop.
Page {
    id: page

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

        // Off: what it's for, and the way to turn it on.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.padding
            visible: page.screen && page.screen.enabled === false
            spacing: Theme.padding
            BodyText {
                Layout.fillWidth: true
                text: qsTr("Saving tracks is off. Saved tracks are for your own listening on this device.")
            }
            TextButton {
                objectName: "enableDownloads"
                text: qsTr("Allow saving tracks to this device")
                onClicked: page.screen.setEnabled(true)
            }
        }

        // On: where to, and the actions.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.padding
            visible: page.screen && page.screen.enabled === true
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
                visible: page.screen && page.screen.canSavePlaying === true
                text: qsTr("Save playing track")
                onClicked: page.screen.savePlaying()
            }
            TextButton {
                visible: page.screen && page.screen.canClear === true
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
                required property var progress
                required property var isRunning
                required property var isOver
                required property var hasFailed
                required property var isPlaylist
                width: ListView.view.width
                height: Theme.rowHeight

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.padding
                    anchors.rightMargin: Theme.dp(4)
                    spacing: Theme.dp(10)
                    ColumnLayout {
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
                    }
                    IconButton {
                        visible: job.isOver !== true
                        iconName: "close"
                        onClicked: page.screen.cancel(job.jobId)
                    }
                }
                // How far it has got, along the bottom edge; a sweep with nothing
                // to measure it by yet.
                Rectangle {
                    visible: job.isRunning === true
                    anchors.bottom: parent.bottom
                    x: typeof job.progress === "number" && job.progress >= 0 ? 0 : sweep.position * parent.width * 0.75
                    width: typeof job.progress === "number" && job.progress >= 0
                        ? parent.width * job.progress : parent.width * 0.25
                    height: Theme.dp(2)
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                    QtObject {
                        id: sweep
                        property real position: 0
                        property NumberAnimation running: NumberAnimation {
                            target: sweep
                            property: "position"
                            from: 0
                            to: 1
                            duration: 1200
                            loops: Animation.Infinite
                            running: job.isRunning === true && !(typeof job.progress === "number" && job.progress >= 0)
                        }
                    }
                }
            }
            BodyText {
                parent: list
                anchors.centerIn: parent
                width: implicitWidth
                visible: list.count === 0 && page.screen && page.screen.enabled === true
                text: qsTr("Nothing saved yet")
            }
        }
    }
}
