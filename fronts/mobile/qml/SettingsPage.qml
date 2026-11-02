import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// What a user of the player sets. The tools for working on it are a page
// further, on DeveloperPage.
Page {
    id: page

    property QtObject backend
    property QtObject platform
    // Main.qml's uiSettings.
    property QtObject ui
    signal closeRequested()
    signal developerRequested()
    signal stopRequested()

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: qsTr("Settings")
        onBackRequested: page.closeRequested()
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

            SectionHeading { text: qsTr("Playback") }
            RowLayout {
                Layout.fillWidth: true
                BodyText { text: qsTr("Play when the system starts") }
                Switch {
                    checked: page.backend.settings ? page.backend.settings.autoplay : false
                    onToggled: page.backend.settings.setAutoplay(checked)
                }
            }
            // Another sound (the phone over Bluetooth) took the audio and
            // paused us: play on once it's over, if it was that short.
            BodyText { text: qsTr("Play on after another sound, if it took up to") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing
                Repeater {
                    model: [{ t: qsTr("Off"), v: 0 }, { t: qsTr("30 s"), v: 30 }, { t: qsTr("1 min"), v: 60 },
                            { t: qsTr("5 min"), v: 300 }]
                    Chip {
                        required property var modelData
                        text: modelData.t
                        checked: page.backend.settings
                                 && page.backend.settings.resumeAfterLossSec === modelData.v
                        onClicked: page.backend.settings.setResumeAfterLossSec(modelData.v)
                    }
                }
            }

            SectionHeading { text: qsTr("Interface scale") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Slider {
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 0.75
                    to: 2.5
                    stepSize: 0.05
                    value: Theme.scale
                    onMoved: page.ui.scale = value
                }
                BodyText {
                    Layout.fillWidth: false
                    text: Theme.scale.toFixed(2) + "×"
                }
                TextButton {
                    text: qsTr("Auto")
                    enabled: page.ui.scale > 0
                    onClicked: page.ui.scale = -1
                }
            }

            // GWM head units: what gets the track and the controls into the
            // car's media widget.
            SectionHeading {
                visible: page.platform.android
                text: qsTr("Car")
            }
            TextButton {
                visible: page.platform.android
                text: qsTr("Connect to Media Center")
                onClicked: mediaCenterResult.text = page.platform.connectToMediaCenter()
            }
            BodyText {
                id: mediaCenterResult
                visible: text.length > 0
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }

            SectionHeading { text: qsTr("App") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing
                TextButton {
                    text: qsTr("Stop and close")
                    onClicked: page.stopRequested()
                }
                TextButton {
                    text: qsTr("Developer tools")
                    onClicked: page.developerRequested()
                }
            }
        }
    }
}
