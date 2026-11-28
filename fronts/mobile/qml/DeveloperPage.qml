import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// The test bench: the device, the log (the startup timeline is on the Log
// page, "Startup"), what's learned about a head unit, scrolling and drawing.
Page {
    id: page
    bottomPadding: Theme.pageInsetBottom

    property QtObject backend
    property QtObject platform
    // Main.qml's uiSettings.
    property QtObject ui
    signal closeRequested()
    signal logRequested()
    signal stopRequested()
    // Main.qml checks with the update server set here.
    signal checkForUpdatesRequested()
    // Hidden from Settings again (until the version is tapped five times).
    signal hideRequested()

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
                SettingSwitch {
                    objectName: "developerToolsShown"
                    title: qsTr("Developer tools")
                    description: qsTr("Turned off, they're hidden from Settings until the version on the About page is tapped five times.")
                    checked: true
                    onToggled: (checked) => { if (!checked) page.hideRequested() }
                }
            }

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

            // Trying an update with no release: one device offers an APK
            // ("Serve updates"), the other takes it from there ("Update
            // server": the address the first shows). docs/android.md, "Updates".
            SettingsGroup {
                shown: page.platform.android
                title: qsTr("Trying updates")
                SettingsCell {
                    SettingText {
                        title: qsTr("Update server")
                        description: qsTr("Another device's address from “Serve updates” below, to take the update from it instead of GitHub. Empty: GitHub.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        InputField {
                            id: updateServerField
                            objectName: "updateServer"
                            text: page.backend.settings ? page.backend.settings.updateServer : ""
                            placeholderText: "192.168.43.1:8765"
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                            onEditingFinished: if (page.backend.settings) page.backend.settings.setUpdateServer(text)
                        }
                        TextButton {
                            text: qsTr("Check now")
                            onClicked: {
                                if (page.backend.settings)
                                    page.backend.settings.setUpdateServer(updateServerField.text)
                                // After the core has the address.
                                checkLater.restart()
                            }
                            Timer {
                                id: checkLater
                                interval: 300
                                onTriggered: page.checkForUpdatesRequested()
                            }
                        }
                    }
                }
                SettingSwitch {
                    objectName: "serveUpdates"
                    title: qsTr("Serve updates")
                    description: qsTr("Offers an APK to the devices on this network as a newer version: the one chosen, or this app's own.")
                    checked: page.platform.updateServerStatus !== ""
                    onToggled: (checked) => checked ? page.platform.startUpdateServer() : page.platform.stopUpdateServer()
                }
                SettingsCell {
                    id: servedCell
                    shown: page.platform.updateServerStatus !== ""
                    readonly property var lines: page.platform.updateServerStatus.split("\n")
                    SettingText {
                        title: qsTr("Address for the other device")
                        description: servedCell.lines.slice(0, -1).join("\n")
                    }
                    SettingText {
                        title: qsTr("Offered")
                        description: servedCell.lines[servedCell.lines.length - 1]
                    }
                    TextButton {
                        text: qsTr("Choose APK…")
                        onClicked: page.platform.chooseServedApk()
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
                    text: qsTr("Check music storage")
                    onClicked: exportResult.text = page.platform.probeMusicStorage()
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
            Panel {
                shown: page.platform.exportingApps
                color: "transparent"
                radius: 0
                padding: 0
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
            Panel {
                shown: exportResult.text.length > 0
                color: "transparent"
                radius: 0
                padding: 0
                BodyText {
                    id: exportResult
                    color: Theme.inkSecondary
                    font.pixelSize: Theme.fontSmall
                }
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
                    shown: page.platform.android
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
                    shown: page.platform.android && !!playback.settings && playback.settings.audioOutput === "opensles"
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
                title: qsTr("Session")
                // Another sound (the phone over Bluetooth) took the audio and
                // paused us: play on once it's over, if it was that short.
                SettingsCell {
                    SettingText { title: qsTr("Play on after another sound, if it took up to") }
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
                }
                SettingsRow {
                    text: qsTr("Stop and close")
                    chevron: false
                    onClicked: page.stopRequested()
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
