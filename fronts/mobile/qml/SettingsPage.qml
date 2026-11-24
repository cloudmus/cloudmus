import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cloudmus.Mobile

// What a user of the player sets. The tools for working on it are a page
// further, on DeveloperPage.
Page {
    id: page
    bottomPadding: Theme.pageInsetBottom

    property QtObject backend
    property QtObject platform
    // Main.qml's uiSettings.
    property QtObject ui
    // Ui::Localizer: the language of the views.
    property QtObject localizer
    signal closeRequested()
    signal developerRequested()
    signal downloadsRequested()
    // `proxy`: the row of the one to edit, null for a new one.
    signal sourceRequested(string sourceId)
    signal proxyRequested(var proxy)

    readonly property QtObject screen: backend.settingsScreen
    readonly property QtObject saving: backend.downloadsScreen

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
            }

            SettingsGroup {
                title: qsTr("Saving tracks")
                SettingSwitch {
                    objectName: "enableDownloads"
                    title: qsTr("Allow saving tracks to this device")
                    description: qsTr("Saved tracks are for your own listening on this device.")
                    checked: page.saving ? page.saving.enabled : false
                    onToggled: (checked) => page.saving.setEnabled(checked)
                }
                SettingsRow {
                    objectName: "downloadsRow"
                    visible: page.saving !== null && page.saving.enabled === true
                    text: qsTr("Downloads")
                    subtitle: page.saving !== null && page.saving.summary !== "" ? page.saving.summary
                        : qsTr("What's being saved, and what has been")
                    onClicked: page.downloadsRequested()
                }
                // As the desktop's Downloads settings.
                SettingsCell {
                    visible: page.saving !== null && page.saving.enabled === true
                    SettingText {
                        title: qsTr("Subfolders")
                        description: page.saving ? qsTr("Tracks are saved to %1/").arg(
                            page.saving.directory + ["", "/<source>", "/<artist>", "/<artist>/<album>"][page.saving.layout])
                                                 : ""
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        Repeater {
                            model: [{ t: qsTr("None"), v: 0 }, { t: qsTr("By source"), v: 1 },
                                    { t: qsTr("By artist"), v: 2 }, { t: qsTr("By artist and album"), v: 3 }]
                            Chip {
                                required property var modelData
                                objectName: "layout_" + modelData.v
                                text: modelData.t
                                checked: page.saving !== null
                                         && page.saving.layout === modelData.v
                                onClicked: {
                                    page.saving.setLayout(modelData.v)
                                    checked = Qt.binding(() => page.saving !== null
                                                               && page.saving.layout === modelData.v)
                                }
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
                    // How full the space set aside is; red when nearly out.
                    Rectangle {
                        objectName: "cacheBar"
                        Layout.fillWidth: true
                        implicitHeight: Theme.dp(8)
                        radius: height / 2
                        color: Theme.surface300
                        Rectangle {
                            width: parent.width * (page.screen ? page.screen.cacheUsedFraction : 0)
                            height: parent.height
                            radius: height / 2
                            color: width > parent.width * 0.9 ? Theme.danger : Theme.accent
                            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                        }
                    }
                }
                SettingStepper {
                    objectName: "diskReserve"
                    title: qsTr("Keep free on the disk")
                    description: qsTr("When less than this is left free, old tracks and covers are removed first; then tracks play without being downloaded, and nothing new is saved.")
                    value: page.screen ? page.screen.reserveMb : 0
                    to: 65536
                    step: 256
                    suffix: " " + qsTr("MB")
                    onMoved: (value) => page.screen.setReserveMb(value)
                }
                SettingsCell {
                    visible: page.screen !== null && page.screen.diskFreeText !== ""
                    SettingText {
                        objectName: "diskFree"
                        title: qsTr("%1 free now").arg(page.screen ? page.screen.diskFreeText : "")
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
                        // The model is a replica that fetches a role on its first read, and
                        // only the title and the address are read to draw the row. Reading
                        // the rest here as well gets them in before a tap, which would
                        // otherwise find the id empty and open a new proxy half filled in.
                        readonly property var proxy: ({
                            proxyId: model.proxyId, name: model.name, kind: model.kind, host: model.host,
                            port: model.port, username: model.username, password: model.password })
                        onClicked: if (proxy.proxyId !== "") page.proxyRequested(proxy)
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
                // A head unit has the track on its main screen, and no mini player.
                SettingSwitch {
                    objectName: "miniPlayerEverywhere"
                    visible: page.width <= page.height * 1.1
                    title: qsTr("Mini player on every screen")
                    checked: page.ui.miniPlayerEverywhere
                    onToggled: (checked) => page.ui.miniPlayerEverywhere = checked
                }
                SettingsCell {
                    visible: page.localizer !== null
                    SettingText { title: qsTr("Language") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        Repeater {
                            // Auto, then every language in its own name.
                            model: page.localizer
                                ? [{ name: qsTr("Auto"), code: "" }].concat(page.localizer.languages) : []
                            Chip {
                                required property var modelData
                                objectName: "language_" + (modelData.code || "auto")
                                text: modelData.name
                                checked: page.localizer.setting === modelData.code
                                onClicked: {
                                    page.localizer.setSetting(modelData.code)
                                    checked = Qt.binding(() => page.localizer.setting === modelData.code)
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
                shown: page.platform.android
                title: qsTr("Music storage")
                // A source's Device music is on, and Android hasn't let it in yet.
                SettingsRow {
                    visible: page.screen !== null && page.screen.mediaPermissionNeeded === true
                    text: qsTr("Allow reading the device's music")
                    onClicked: page.platform.requestMusicPermission()
                }
                SettingsRow {
                    text: qsTr("Storage permissions")
                    onClicked: page.platform.grantMusicStorage()
                }
            }

            SettingsGroup {
                title: qsTr("App")
                SettingsRow {
                    text: qsTr("Developer tools")
                    onClicked: page.developerRequested()
                }
            }
        }
    }
}
