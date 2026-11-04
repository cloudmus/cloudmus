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
    // `proxy`: the row of the one to edit, null for a new one.
    signal sourceRequested(string sourceId)
    signal proxyRequested(var proxy)

    readonly property QtObject screen: backend.settingsScreen

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: qsTr("Settings")
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

            SettingsCell {
                objectName: "spaceWarning"
                visible: warningText.text !== ""
                Text {
                    id: warningText
                    Layout.fillWidth: true
                    text: page.backend.mainScreen ? page.backend.mainScreen.spaceWarning : ""
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            SettingsGroup {
                title: qsTr("Playback")
                SettingSwitch {
                    objectName: "autoplay"
                    title: qsTr("Play when the system starts")
                    checked: page.backend.settings ? page.backend.settings.autoplay : false
                    onToggled: (checked) => page.backend.settings.setAutoplay(checked)
                }
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
            }

            SettingsGroup {
                title: qsTr("Preloading")
                description: qsTr("How many tracks after the playing one are downloaded in advance, so they start at once and play through a bad connection.")
                SettingStepper {
                    objectName: "preloadPlaylist"
                    title: qsTr("Tracks ahead in a playlist")
                    value: page.screen ? page.screen.playlistTracks : 0
                    to: 50
                    onMoved: (value) => page.screen.setPlaylistTracks(value)
                }
                SettingStepper {
                    title: qsTr("Tracks ahead in a radio")
                    value: page.screen ? page.screen.radioTracks : 0
                    to: 50
                    onMoved: (value) => page.screen.setRadioTracks(value)
                }
                SettingStepper {
                    objectName: "cacheSize"
                    title: qsTr("Disk space for tracks")
                    value: page.screen ? page.screen.cacheMaxMb : 0
                    to: 65536
                    step: 256
                    suffix: " " + qsTr("MB")
                    onMoved: (value) => page.screen.setCacheMaxMb(value)
                }
                SettingsCell {
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.padding
                        SettingText {
                            objectName: "cacheUsed"
                            title: qsTr("%1 used").arg(page.screen ? page.screen.cacheUsedText : "")
                        }
                        TextButton {
                            objectName: "clearCache"
                            text: qsTr("Clear")
                            onClicked: page.screen.clearCache()
                        }
                    }
                }
            }

            SettingsGroup {
                title: qsTr("Sources")
                Repeater {
                    model: page.backend.settingsSources
                    SettingsRow {
                        required property string sourceId
                        required property string name
                        required property string iconUrl
                        required property string status
                        objectName: "sourceRow_" + sourceId
                        text: name
                        iconSource: iconUrl
                        subtitle: status
                        onClicked: page.sourceRequested(sourceId)
                    }
                }
            }

            SettingsGroup {
                title: qsTr("Network")
                description: qsTr("Proxies a source can connect through: choose one on the source's own page. Everything of that source goes through it: the source itself, its streams and its covers.")
                Repeater {
                    model: page.backend.settingsProxies
                    SettingsRow {
                        required property var model
                        objectName: "proxyRow_" + model.title
                        text: model.title
                        subtitle: model.host + ":" + model.port
                        onClicked: page.proxyRequested({
                            proxyId: model.proxyId, name: model.name, kind: model.kind, host: model.host,
                            port: model.port, username: model.username, password: model.password })
                    }
                }
                SettingsRow {
                    objectName: "addProxy"
                    text: qsTr("Add proxy")
                    onClicked: page.proxyRequested(null)
                }
            }

            SettingsGroup {
                title: qsTr("Privacy")
                SettingSwitch {
                    objectName: "analytics"
                    title: qsTr("Send usage statistics to Google Analytics")
                    checked: page.screen ? page.screen.analytics : false
                    onToggled: (checked) => page.screen.setAnalytics(checked)
                }
            }

            SettingsGroup {
                title: qsTr("Interface")
                SettingsCell {
                    SettingText { title: qsTr("Theme") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        Repeater {
                            model: [{ t: qsTr("Auto"), v: "auto" }, { t: qsTr("Light"), v: "light" },
                                    { t: qsTr("Dark"), v: "dark" }]
                            Chip {
                                required property var modelData
                                objectName: "scheme_" + modelData.v
                                text: modelData.t
                                checked: page.ui.colorScheme === modelData.v
                                onClicked: {
                                    page.ui.colorScheme = modelData.v
                                    checked = Qt.binding(() => page.ui.colorScheme === modelData.v)
                                }
                            }
                        }
                    }
                }
                SettingsCell {
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.padding
                        SettingText { title: qsTr("Scale") }
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
                    AppSlider {
                        from: 0.75
                        to: 2.5
                        stepSize: 0.05
                        value: Theme.scale
                        onMoved: page.ui.scale = value
                    }
                }
            }

            SettingsGroup {
                title: qsTr("App")
                SettingsRow {
                    text: qsTr("Stop and close")
                    chevron: false
                    onClicked: page.stopRequested()
                }
                SettingsRow {
                    text: qsTr("Developer tools")
                    onClicked: page.developerRequested()
                }
            }
        }
    }
}
