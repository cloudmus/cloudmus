import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// The log of both processes as it's written: filters on top, the newest
// lines at the bottom, followed until scrolled up.
Page {
    id: page

    property QtObject backend
    property QtObject platform
    signal closeRequested()

    background: Rectangle { color: Theme.surface0 }

    LogModel {
        id: log
        directory: page.platform.logDirectory
        minLevel: levelGroup.checkedButton ? levelGroup.checkedButton.level : "D"
        process: processGroup.checkedButton ? processGroup.checkedButton.process : ""
        thisBootOnly: bootChip.checked
        text: search.text
        onCountChanged: if (list.follow) Qt.callLater(list.positionViewAtEnd)
    }

    header: Rectangle {
        implicitHeight: headerColumn.implicitHeight
        color: Theme.surface100

        ColumnLayout {
            id: headerColumn
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.dp(6)

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.barHeight
                Layout.leftMargin: Theme.dp(4)
                Layout.rightMargin: Theme.dp(4)
                IconButton {
                    iconName: "arrow_back"
                    onClicked: page.closeRequested()
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Log") + "  ·  " + log.count
                    color: Theme.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                }
                IconButton {
                    iconName: "file_download"
                    onClicked: toast.show(page.platform.exportLogs())
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: chips.implicitHeight
                contentWidth: chips.implicitWidth + Theme.padding * 2
                flickableDirection: Flickable.HorizontalFlick
                clip: true

                Row {
                    id: chips
                    x: Theme.padding
                    spacing: Theme.dp(6)

                    ButtonGroup { id: levelGroup }
                    Repeater {
                        model: [{ t: "D", l: "D" }, { t: "I", l: "I" }, { t: "W", l: "W" }, { t: "E", l: "E" }]
                        Chip {
                            required property var modelData
                            property string level: modelData.l
                            text: modelData.t + "+"
                            checked: modelData.l === "D"
                            ButtonGroup.group: levelGroup
                        }
                    }
                    Item { width: Theme.dp(8); height: 1 }
                    ButtonGroup { id: processGroup }
                    Repeater {
                        model: [{ t: qsTr("All"), p: "" }, { t: "playback", p: "playback" }, { t: "ui", p: "ui" }]
                        Chip {
                            required property var modelData
                            property string process: modelData.p
                            text: modelData.t
                            checked: modelData.p === ""
                            ButtonGroup.group: processGroup
                        }
                    }
                    Item { width: Theme.dp(8); height: 1 }
                    Chip {
                        id: bootChip
                        text: qsTr("This boot")
                    }
                }
            }

            TextField {
                id: search
                Layout.fillWidth: true
                Layout.leftMargin: Theme.padding
                Layout.rightMargin: Theme.padding
                Layout.bottomMargin: Theme.dp(8)
                implicitHeight: Theme.touch
                placeholderText: qsTr("Search")
                placeholderTextColor: Theme.inkTertiary
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.surface200
                    border.color: search.activeFocus ? Theme.accent : Theme.border
                }
            }
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: log
        // Follows new lines while at the bottom; scrolling up stops it.
        property bool follow: true
        onMovementEnded: follow = atYEnd
        Component.onCompleted: positionViewAtEnd()
        ScrollBar.vertical: ScrollBar { }

        delegate: Text {
            required property string time
            required property real uptime
            required property string process
            required property string level
            required property string tag
            required property string message

            width: ListView.view.width
            leftPadding: Theme.dp(10)
            rightPadding: Theme.dp(10)
            topPadding: Theme.dp(2)
            bottomPadding: Theme.dp(2)
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            font.family: Theme.monoFamily
            font.pixelSize: Theme.dp(12)
            color: level === "E" || level === "F" ? Theme.danger
                : level === "W" ? Theme.warning
                : level === "D" ? Theme.inkSecondary
                : tag === "Startup" ? Theme.accent : Theme.ink
            text: time.substring(11) + " [" + uptime.toFixed(1) + "] "
                + (process === "playback" ? "P" : process === "ui" ? "U" : process) + " "
                + level + " " + tag + ": " + message
        }
    }

    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.padding
        spacing: Theme.dp(10)

        IconButton {
            iconName: "expand_less"
            filled: false
            color: Theme.ink
            // To where the latest boot of the system starts.
            onClicked: {
                list.follow = false
                list.positionViewAtIndex(Math.max(0, log.lastBootRow()), ListView.Beginning)
            }
            background: Rectangle { radius: width / 2; color: Theme.surface300 }
        }
        IconButton {
            visible: !list.follow
            iconName: "expand_more"
            filled: true
            onClicked: {
                list.follow = true
                list.positionViewAtEnd()
            }
        }
    }

    Toast { id: toast }
}
