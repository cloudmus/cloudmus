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

            SectionHeading { text: qsTr("Preloading") }
            Text {
                Layout.fillWidth: true
                text: qsTr("How many tracks after the playing one are downloaded in advance, so they start at once and play through a bad connection.")
                wrapMode: Text.Wrap
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
            RowLayout {
                Layout.fillWidth: true
                BodyText { text: qsTr("Tracks ahead in a playlist") }
                Stepper {
                    objectName: "preloadPlaylist"
                    value: page.screen ? page.screen.playlistTracks : 0
                    to: 50
                    onMoved: (value) => page.screen.setPlaylistTracks(value)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                BodyText { text: qsTr("Tracks ahead in a radio") }
                Stepper {
                    value: page.screen ? page.screen.radioTracks : 0
                    to: 50
                    onMoved: (value) => page.screen.setRadioTracks(value)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                BodyText { text: qsTr("Disk space for tracks") }
                Stepper {
                    objectName: "cacheSize"
                    value: page.screen ? page.screen.cacheMaxMb : 0
                    to: 65536
                    step: 256
                    suffix: " " + qsTr("MB")
                    onMoved: (value) => page.screen.setCacheMaxMb(value)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                BodyText {
                    objectName: "cacheUsed"
                    text: qsTr("%1 used").arg(page.screen ? page.screen.cacheUsedText : "")
                }
                TextButton {
                    objectName: "clearCache"
                    text: qsTr("Clear")
                    onClicked: page.screen.clearCache()
                }
            }

            SectionHeading { text: qsTr("Sources") }
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

            SectionHeading { text: qsTr("Network") }
            Text {
                Layout.fillWidth: true
                text: qsTr("Proxies a source can connect through: choose one on the source's own page. Everything of that source goes through it: the source itself, its streams and its covers.")
                wrapMode: Text.Wrap
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
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
            TextButton {
                objectName: "addProxy"
                text: qsTr("Add proxy")
                onClicked: page.proxyRequested(null)
            }

            SectionHeading { text: qsTr("Privacy") }
            RowLayout {
                Layout.fillWidth: true
                BodyText { text: qsTr("Send usage statistics to Google Analytics") }
                Switch {
                    objectName: "analytics"
                    checked: page.screen ? page.screen.analytics : false
                    onToggled: page.screen.setAnalytics(checked)
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
