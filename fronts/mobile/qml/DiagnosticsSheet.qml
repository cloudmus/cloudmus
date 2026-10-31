import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// The test bench: the device and screen, autoplay, the UI scale and
// scrolling, and where the log is (the startup timeline is on the Log page,
// "Startup").
Drawer {
    id: sheet

    property QtObject backend
    property QtObject platform
    // -1: automatic.
    property real userScale: -1
    property real flickMaxVelocity: Theme.flickMaxVelocityBase
    property real flickDeceleration: Theme.flickDecelerationBase
    property bool showFps: false
    signal logRequested()
    signal stopRequested()

    edge: Qt.BottomEdge
    width: parent.width
    height: Math.min(parent.height * 0.9, content.implicitHeight + Theme.padding * 3)
    dim: true
    background: Rectangle {
        color: Theme.surface100
        radius: Theme.dp(18)
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.radius
            color: parent.color
        }
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

            component Heading: Text {
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.dp(1)
            }
            component Body: Text {
                Layout.fillWidth: true
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            Heading { text: qsTr("Device") }
            Body { text: sheet.platform.deviceInfo }

            Heading { text: qsTr("Playback") }
            TextButton {
                text: qsTr("Stop and close")
                onClicked: sheet.stopRequested()
            }
            RowLayout {
                Layout.fillWidth: true
                Body {
                    text: qsTr("Play when the system starts")
                }
                Switch {
                    id: autoplay
                    checked: sheet.backend.settings ? sheet.backend.settings.autoplay : false
                    onToggled: sheet.backend.settings.setAutoplay(checked)
                }
            }

            Heading { text: qsTr("Interface scale") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Slider {
                    id: scaleSlider
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 0.75
                    to: 2.5
                    stepSize: 0.05
                    value: Theme.scale
                    onMoved: sheet.userScale = value
                }
                Body {
                    Layout.fillWidth: false
                    text: Theme.scale.toFixed(2) + "×"
                }
                TextButton {
                    text: qsTr("Auto")
                    enabled: sheet.userScale > 0
                    onClicked: sheet.userScale = -1
                }
            }

            Heading { text: qsTr("Scrolling") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Body { Layout.fillWidth: false; text: qsTr("Fling speed") }
                Slider {
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 2000; to: 20000; stepSize: 500
                    value: sheet.flickMaxVelocity
                    onMoved: sheet.flickMaxVelocity = value
                }
                Body { Layout.fillWidth: false; text: Math.round(sheet.flickMaxVelocity) }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.padding
                Body { Layout.fillWidth: false; text: qsTr("Deceleration") }
                Slider {
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch
                    from: 500; to: 12000; stepSize: 250
                    value: sheet.flickDeceleration
                    onMoved: sheet.flickDeceleration = value
                }
                Body { Layout.fillWidth: false; text: Math.round(sheet.flickDeceleration) }
            }
            RowLayout {
                Layout.fillWidth: true
                Body { text: qsTr("Show frames per second") }
                Switch {
                    checked: sheet.showFps
                    onToggled: sheet.showFps = checked
                }
            }

            Heading { text: qsTr("Log") }
            Body {
                text: sheet.platform.publicLogDirectory
                    ? qsTr("Copied to %1").arg(sheet.platform.publicLogDirectory)
                    : sheet.platform.logDirectory
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
            RowLayout {
                spacing: Theme.spacing
                TextButton {
                    text: qsTr("Open log")
                    onClicked: {
                        sheet.close()
                        sheet.logRequested()
                    }
                }
                TextButton {
                    text: qsTr("Export zip")
                    onClicked: exportResult.text = sheet.platform.exportLogs()
                }
            }
            Body {
                id: exportResult
                visible: text.length > 0
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
