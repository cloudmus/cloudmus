import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What's set about one source; signing in to it is in the Library.
Page {
    id: page

    property QtObject backend
    property QtObject platform
    signal closeRequested()

    readonly property QtObject screen: backend.sourceSettings

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: page.screen ? page.screen.name : ""
        onBackRequested: page.closeRequested()
    }

    component Hint: Text {
        Layout.fillWidth: true
        visible: text !== ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }

    ScrollArea {
        anchors.fill: parent
        anchors.margins: Theme.padding * 1.5
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: Theme.padding

            Hint { text: page.screen.description }
            RowLayout {
                Layout.fillWidth: true
                BodyText { text: qsTr("Enabled") }
                Switch {
                    objectName: "sourceEnabled"
                    checked: page.screen.enabled
                    onToggled: page.screen.setEnabled(checked)
                }
            }
            Hint { objectName: "sourceStatus"; text: page.screen.status }

            SectionHeading {
                visible: page.screen.usesNetwork
                text: qsTr("Connection")
            }
            Flow {
                Layout.fillWidth: true
                visible: page.screen.usesNetwork
                spacing: Theme.spacing
                Repeater {
                    model: page.backend.sourceConnections
                    Chip {
                        required property string connectionId
                        required property string title
                        required property bool isCurrent
                        objectName: "connection_" + connectionId
                        text: title
                        checked: isCurrent
                        onClicked: {
                            checked = Qt.binding(() => isCurrent)
                            page.screen.setConnection(connectionId)
                        }
                    }
                }
            }
            Hint {
                visible: page.screen.usesNetwork && text !== ""
                text: page.screen.connectionHint
            }

            SectionHeading {
                visible: page.screen.hasRadio
                text: qsTr("Radio")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: page.screen.hasRadio
                BodyText { text: qsTr("Tracks before a repeat") }
                Stepper {
                    value: page.screen.radioRepeatGap
                    to: 500
                    step: 10
                    onMoved: (value) => page.screen.setRadioRepeatGap(value)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: page.screen.hasRadio
                BodyText { text: qsTr("Or days") }
                Stepper {
                    value: page.screen.radioRepeatDays
                    to: 30
                    fromText: qsTr("Off")
                    onMoved: (value) => page.screen.setRadioRepeatDays(value)
                }
            }
            Hint {
                visible: page.screen.hasRadio
                text: qsTr("The radio leaves out this many of the tracks last played from this source, or all it played in this many days if that is more, as long as it has others to play. Applies to the next station you start.")
            }

            SectionHeading {
                visible: page.screen.downloadsStreams
                text: qsTr("Downloading")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: page.screen.downloadsStreams
                BodyText { text: qsTr("Connections") }
                Stepper {
                    value: page.screen.downloadConnections
                    to: 6
                    fromText: qsTr("Auto")
                    onMoved: (value) => page.screen.setDownloadConnections(value)
                }
            }
            Hint {
                visible: page.screen.downloadsStreams
                text: qsTr("How many parts of a track are downloaded at once. A service that slows each connection down gives several more in all. Applies to the next track.")
            }

            Hint { visible: page.screen.fieldsLoading; text: qsTr("Loading settings…") }
            Hint {
                visible: page.screen.fieldsError !== ""
                text: qsTr("Couldn't load the settings: %1").arg(page.screen.fieldsError)
            }
            Repeater {
                model: page.backend.sourceFields
                SettingField {
                    onChanged: (key, value) => {
                        if (typeof value === "boolean")
                            page.screen.setFieldBool(key, value)
                        else if (typeof value === "number")
                            page.screen.setFieldNumber(key, value)
                        else
                            page.screen.setFieldText(key, value)
                    }
                }
            }
        }
    }
}
