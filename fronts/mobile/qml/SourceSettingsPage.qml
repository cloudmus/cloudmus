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

    ScrollArea {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.padding
        contentHeight: content.implicitHeight + Theme.padding * 2
        clip: true

        ColumnLayout {
            id: content
            y: Theme.padding
            width: parent.width
            spacing: Theme.settingsGroupGap

            SettingsGroup {
                SettingSwitch {
                    objectName: "sourceEnabled"
                    title: qsTr("Enabled")
                    description: page.screen.status !== "" ? page.screen.status : page.screen.description
                    checked: page.screen.enabled
                    onToggled: (checked) => page.screen.setEnabled(checked)
                }
            }

            SettingsGroup {
                shown: page.screen.usesNetwork
                title: qsTr("Connection")
                SettingsCell {
                    Flow {
                        Layout.fillWidth: true
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
                    SettingText { description: page.screen.connectionHint }
                }
            }

            SettingsGroup {
                shown: page.screen.hasRadio
                title: qsTr("Radio")
                description: qsTr("The radio leaves out this many of the tracks last played from this source, or all it played in this many days if that is more, as long as it has others to play. Applies to the next station you start.")
                SettingStepper {
                    title: qsTr("Tracks before a repeat")
                    value: page.screen.radioRepeatGap
                    to: 500
                    step: 10
                    onMoved: (value) => page.screen.setRadioRepeatGap(value)
                }
                SettingStepper {
                    title: qsTr("Or days")
                    value: page.screen.radioRepeatDays
                    to: 30
                    fromText: qsTr("Off")
                    onMoved: (value) => page.screen.setRadioRepeatDays(value)
                }
            }

            SettingsGroup {
                shown: page.screen.downloadsStreams
                title: qsTr("Downloading")
                SettingStepper {
                    title: qsTr("Connections")
                    description: qsTr("How many parts of a track are downloaded at once. A service that slows each connection down gives several more in all. Applies to the next track.")
                    value: page.screen.downloadConnections
                    to: 6
                    fromText: qsTr("Auto")
                    onMoved: (value) => page.screen.setDownloadConnections(value)
                }
            }

            SettingsGroup {
                shown: page.screen.fieldsLoading || page.screen.fieldsError !== ""
                SettingsCell {
                    SettingText {
                        description: page.screen.fieldsLoading ? qsTr("Loading settings…")
                            : qsTr("Couldn't load the settings: %1").arg(page.screen.fieldsError)
                    }
                }
            }

            // Its own settings: its groups come as rows, each knowing its place.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: -(Theme.settingsGroupGap)
                spacing: Theme.settingsCellGap
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
}
