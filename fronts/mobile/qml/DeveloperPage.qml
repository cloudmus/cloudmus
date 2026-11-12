import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// The test bench: the device, the log (the startup timeline is on the Log
// page, "Startup"), what's learned about a head unit, scrolling and drawing.
Page {
    id: page
    bottomPadding: Theme.insetBottom

    property QtObject backend
    property QtObject platform
    // Main.qml's uiSettings.
    property QtObject ui
    signal closeRequested()
    signal logRequested()

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: qsTr("Developer tools")
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
                title: qsTr("Device")
                SettingsCell {
                    BodyText { text: page.platform.deviceInfo }
                }
            }

            SettingsGroup {
                title: qsTr("Log")
                SettingsCell {
            SettingText {
                description: page.platform.publicLogDirectory
                    ? qsTr("Copied to %1").arg(page.platform.publicLogDirectory)
                    : page.platform.logDirectory
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing
                TextButton {
                    text: qsTr("Open log")
                    onClicked: page.logRequested()
                }
                TextButton {
                    text: qsTr("Export zip")
                    onClicked: exportResult.text = page.platform.exportLogs()
                }
            }
                }
            }

            SettingsGroup {
                shown: page.platform.android
                title: qsTr("Head unit")
                SettingsCell {
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing
                TextButton {
                    text: qsTr("Python probe")
                    onClicked: {
                        exportResult.text = qsTr("Running, it takes a minute…")
                        page.platform.runPythonProbe()
                    }
                }
                TextButton {
                    text: qsTr("Log system probe")
                    onClicked: exportResult.text = page.platform.logSystemProbe()
                }
                // The head unit's system apps, to decompile; a few hundred
                // MB, so it shows how far along it is.
                TextButton {
                    enabled: !page.platform.exportingApps
                    text: page.platform.exportingApps ? qsTr("Exporting…") : qsTr("Export system apps")
                    onClicked: {
                        exportResult.text = ""
                        page.platform.exportSystemApps()
                    }
                }
            }
            ColumnLayout {
                visible: page.platform.exportingApps
                Layout.fillWidth: true
                spacing: Theme.dp(6)
                ProgressBar {
                    id: exportBar
                    Layout.fillWidth: true
                    // Indeterminate (a moving stripe) while it counts the
                    // packages: something moves from the first moment.
                    indeterminate: page.platform.exportFraction < 0
                    value: Math.max(0, page.platform.exportFraction)
                    background: Rectangle {
                        implicitHeight: Theme.dp(6)
                        radius: height / 2
                        color: Theme.surface300
                    }
                    contentItem: Item {
                        implicitHeight: Theme.dp(6)
                        clip: true
                        Rectangle {
                            visible: !exportBar.indeterminate
                            width: exportBar.visualPosition * parent.width
                            height: parent.height
                            radius: height / 2
                            color: Theme.accent
                        }
                        Rectangle {
                            id: stripe
                            visible: exportBar.indeterminate
                            width: parent.width / 3
                            height: parent.height
                            radius: height / 2
                            color: Theme.accent
                            NumberAnimation on x {
                                running: stripe.visible
                                loops: Animation.Infinite
                                from: -stripe.width
                                to: stripe.parent.width
                                duration: 1200
                            }
                        }
                    }
                }
                BodyText {
                    text: page.platform.exportStatus
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    color: Theme.inkSecondary
                    font.pixelSize: Theme.fontSmall
                }
            }
            Connections {
                target: page.platform
                function onSystemAppsExported(result) { exportResult.text = result }
            }
            BodyText {
                id: exportResult
                visible: text.length > 0
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
                }
            }

            // Against the click of a pause or a track changed by hand: mpv's
            // volume fades a whole output chunk at a time, so it's smooth only
            // with OpenSL's small chunks (see Playback::AudioPlayer::setEdges()).
            SettingsGroup {
                id: playback
                readonly property QtObject settings: page.backend ? page.backend.settings : null
                title: qsTr("Playback")
                description: qsTr("An experiment against the click of a pause or a track changed by hand: fade out first, then wait for the silence to reach the speakers. Off by default: mpv's volume changes in steps, and each step can be heard.")
                SettingsCell {
                    visible: page.platform.android
                    SettingText { title: qsTr("Audio output") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        Repeater {
                            model: [{ t: "OpenSL ES", v: "opensles" }, { t: "AudioTrack", v: "audiotrack" }]
                            Chip {
                                required property var modelData
                                text: modelData.t
                                checked: playback.settings && playback.settings.audioOutput === modelData.v
                                onClicked: playback.settings.setAudioOutput(modelData.v)
                            }
                        }
                    }
                }
                SettingStepper {
                    visible: page.platform.android && !!playback.settings && playback.settings.audioOutput === "opensles"
                    title: qsTr("OpenSL chunk")
                    description: qsTr("Frames handed to the output at a time; 480 is 10 ms at 48 kHz. Smaller makes the fade smoother; too small and a slow device crackles.")
                    value: playback.settings ? playback.settings.outputChunkFrames : 0
                    from: 32
                    to: 4096
                    step: 32
                    onMoved: (value) => playback.settings.setOutputChunkFrames(value)
                }
                SettingStepper {
                    title: qsTr("Fade")
                    value: playback.settings ? playback.settings.fadeMs : 0
                    to: 500
                    step: 10
                    suffix: " " + qsTr("ms")
                    fromText: qsTr("Off")
                    onMoved: (value) => playback.settings.setFadeMs(value)
                }
                SettingStepper {
                    title: qsTr("Wait after the fade")
                    value: playback.settings ? playback.settings.fadeSettleMs : 0
                    to: 500
                    step: 10
                    suffix: " " + qsTr("ms")
                    onMoved: (value) => playback.settings.setFadeSettleMs(value)
                }
            }

            SettingsGroup {
                title: qsTr("Scrolling")
                SettingsCell {
                    RowLayout {
                        Layout.fillWidth: true
                        SettingText { title: qsTr("Fling speed") }
                        BodyText { Layout.fillWidth: false; text: Math.round(page.ui.flickMaxVelocity) }
                    }
                    AppSlider {
                        from: 2000; to: 20000; stepSize: 500
                        value: page.ui.flickMaxVelocity
                        onMoved: page.ui.flickMaxVelocity = value
                    }
                }
                SettingsCell {
                    RowLayout {
                        Layout.fillWidth: true
                        SettingText { title: qsTr("Deceleration") }
                        BodyText { Layout.fillWidth: false; text: Math.round(page.ui.flickDeceleration) }
                    }
                    AppSlider {
                        from: 500; to: 12000; stepSize: 250
                        value: page.ui.flickDeceleration
                        onMoved: page.ui.flickDeceleration = value
                    }
                }
                SettingSwitch {
                    title: qsTr("Show frames per second")
                    checked: page.ui.showFps
                    onToggled: (checked) => page.ui.showFps = checked
                }
            }

            SettingsGroup {
                title: qsTr("Blur")
                SettingSwitch {
                    objectName: "dynamicBlur"
                    title: qsTr("Dynamic blur behind glass")
                    description: qsTr("The panel over the screen shows what's behind it as it changes, not as it was when the panel came up. Takes more of the GPU while such a panel is open.")
                    checked: page.ui.dynamicBlur
                    onToggled: (checked) => page.ui.dynamicBlur = checked
                }
            }

            // Experiments for a finger drag that stutters at a steady frame
            // rate: how Qt draws and how it hands touches to the views.
            SettingsGroup {
                title: qsTr("Drawing (after a restart)")
                SettingsCell {
                    SettingText { title: qsTr("Render loop") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        Repeater {
                            model: [{ t: qsTr("Default"), v: "" }, { t: "threaded", v: "threaded" }, { t: "basic", v: "basic" }]
                            Chip {
                                required property var modelData
                                text: modelData.t
                                checked: page.ui.renderLoop === modelData.v
                                onClicked: page.ui.renderLoop = modelData.v
                            }
                        }
                    }
                }
                SettingSwitch {
                    title: qsTr("Merge touch moves into one per frame")
                    checked: page.ui.touchCompression
                    onToggled: (checked) => page.ui.touchCompression = checked
                }
            }
        }
    }
}
