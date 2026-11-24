import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// An update of the app, as the desktop's update dialog takes it, one step
// after another (ViewModel::UpdateScreen): what's new, the download, and the
// install — Android's package installer, after the user allowed CloudMus to
// install apps, or the store the app came from (Platform.updateChannel).
Page {
    id: page
    bottomPadding: Theme.pageInsetBottom

    property QtObject backend
    property QtObject platform
    signal closeRequested()

    readonly property QtObject screen: backend ? backend.updateScreen : null
    readonly property int step: screen ? screen.step : 0
    // ViewModel::UpdateScreen::Step and Via.
    readonly property int stepOffer: 1
    readonly property int stepDownloading: 2
    readonly property int stepReady: 3
    readonly property int stepInstalling: 4
    readonly property int stepFailed: 5
    readonly property bool viaStore: screen !== null && screen.via === 1
    readonly property bool viaPage: screen !== null && screen.via === 2
    readonly property string version: screen ? screen.version : ""

    // Ready, and Android doesn't let CloudMus install apps yet: asked again
    // whenever the app comes back to the front (from Android's settings).
    property bool allowed: platform ? platform.canInstallPackages() : false
    readonly property bool active: Qt.application.state === Qt.ApplicationActive
    onActiveChanged: if (active) _installIfReady()
    onStepChanged: _installIfReady()
    Component.onCompleted: _installIfReady()

    function _installIfReady() {
        if (step !== stepReady || !platform)
            return
        allowed = platform.canInstallPackages()
        if (!allowed)
            return
        const playing = backend.nowPlaying !== null && backend.nowPlaying.playing === true
        platform.installUpdate(screen.downloadedFile, playing, qsTr("Updates"),
            qsTr("CloudMus is updated to %1"), qsTr("Tap to open it"))
        screen.installStarted()
    }

    function install() {
        if (viaPage) {
            screen.releasePageOpened()
            Qt.openUrlExternally(screen.releasePage)
            return
        }
        screen.install()
        if (viaStore)
            platform.startStoreUpdate()
    }

    // Leaving the page during the download doesn't stop it: Ready brings the
    // page back (Main.qml). Anywhere else it's backing out.
    function leave() {
        if (screen && step !== stepDownloading && step !== stepInstalling)
            screen.close()
        page.closeRequested()
    }

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: qsTr("CloudMus Update")
        onBackRequested: page.leave()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.padding
        spacing: Theme.spacing

        Text {
            objectName: "updateHeading"
            Layout.fillWidth: true
            Layout.topMargin: Theme.padding
            wrapMode: Text.Wrap
            color: Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            text: page.step === page.stepDownloading ? qsTr("Downloading CloudMus %1").arg(page.version)
                : page.step === page.stepReady && !page.allowed ? qsTr("Allow CloudMus to install the update")
                : page.step === page.stepReady || page.step === page.stepInstalling
                    ? qsTr("Installing CloudMus %1").arg(page.version)
                : page.step === page.stepFailed ? qsTr("The update didn't work out")
                : qsTr("A new version of CloudMus is available")
        }

        BodyText {
            objectName: "updateText"
            visible: text !== ""
            textFormat: Text.StyledText
            color: Theme.inkSecondary
            text: !page.screen ? ""
                : page.step === page.stepOffer
                    ? qsTr("Version <b>%1</b> is out — you have <b>%2</b>.")
                          .arg(page.version, page.screen.currentVersionText)
                : page.step === page.stepReady && !page.allowed
                    ? qsTr("Android asks once to let CloudMus install apps. Turn on “Allow from this source”, then come back: the update goes on by itself.")
                : page.step === page.stepInstalling && page.viaStore
                    ? qsTr("The store downloads the update, then restarts CloudMus in the new version.")
                : page.step === page.stepInstalling
                    ? qsTr("Confirm the update in Android's window. CloudMus closes and opens again in the new version.")
                : page.step === page.stepFailed
                    ? qsTr("%1\n\nYou can try again later or download the new version yourself.")
                          .arg(page.screen.error).replace(/\n/g, "<br>")
                : ""
        }

        // Why it can't install by itself.
        Rectangle {
            Layout.fillWidth: true
            visible: page.step === page.stepOffer && page.screen !== null && page.screen.unavailableReason !== ""
            implicitHeight: reason.implicitHeight + Theme.dp(20)
            radius: Theme.dp(8)
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
            BodyText {
                id: reason
                anchors.fill: parent
                anchors.margins: Theme.dp(10)
                font.weight: Font.DemiBold
                text: page.screen ? page.screen.unavailableReason : ""
            }
        }

        // The releases' notes.
        ScrollArea {
            id: notesArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.step === page.stepOffer
            contentWidth: width
            contentHeight: notes.implicitHeight + Theme.padding
            clip: true
            Text {
                id: notes
                objectName: "updateNotes"
                width: notesArea.width
                wrapMode: Text.Wrap
                textFormat: Text.MarkdownText
                color: Theme.ink
                linkColor: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                text: page.screen ? page.screen.notes : ""
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        // The download, or the store's.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.padding
            spacing: Theme.dp(8)
            visible: page.step === page.stepDownloading
                || (page.step === page.stepInstalling && page.viaStore)
                || (page.step === page.stepReady && page.allowed)
            readonly property real fraction: page.step === page.stepDownloading
                ? (page.screen ? page.screen.progress : -1)
                : page.viaStore && page.platform ? page.platform.storeUpdateProgress : -1
            // A thin bar; while the size isn't known, a piece of it runs to and fro.
            Rectangle {
                id: track
                objectName: "updateProgress"
                readonly property real fraction: parent.fraction
                Layout.fillWidth: true
                implicitHeight: Theme.dp(6)
                radius: height / 2
                color: Theme.surface300
                clip: true
                Rectangle {
                    id: fill
                    height: parent.height
                    radius: height / 2
                    color: Theme.accent
                    width: track.fraction < 0 ? track.width * 0.3 : track.fraction * track.width
                    x: 0
                    SequentialAnimation on x {
                        running: track.fraction < 0 && track.visible
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) fill.x = 0
                        NumberAnimation { from: 0; to: track.width * 0.7; duration: 900; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: track.width * 0.7; to: 0; duration: 900; easing.type: Easing.InOutQuad }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: page.step === page.stepDownloading
                text: page.screen ? page.screen.progressText : ""
                color: Theme.inkSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
        }

        Item { Layout.fillHeight: true; visible: !notesArea.visible }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.bottomMargin: Theme.padding
            spacing: Theme.spacing
            TextButton {
                objectName: "updateSkip"
                visible: page.step === page.stepOffer
                text: qsTr("Skip This Version")
                onClicked: {
                    page.screen.skip()
                    page.closeRequested()
                }
            }
            TextButton {
                objectName: "updateReleasePage"
                visible: page.step === page.stepFailed && !page.viaPage
                text: qsTr("Open Release Page")
                onClicked: {
                    page.screen.releasePageOpened()
                    Qt.openUrlExternally(page.screen.releasePage)
                }
            }
            TextButton {
                objectName: "updateCancel"
                visible: page.step === page.stepDownloading
                text: qsTr("Cancel")
                onClicked: page.screen.close()
            }
            TextButton {
                objectName: "updateClose"
                visible: page.step === page.stepFailed || page.step === page.stepInstalling
                text: qsTr("Close")
                onClicked: page.leave()
            }
            TextButton {
                objectName: "updatePermission"
                visible: page.step === page.stepReady && !page.allowed
                text: qsTr("Open Settings")
                tint: Theme.accent
                onClicked: page.platform.requestInstallPermission()
            }
            TextButton {
                objectName: "updateInstall"
                visible: page.step === page.stepOffer || page.step === page.stepFailed
                text: page.viaPage ? qsTr("Open Release Page")
                    : page.step === page.stepFailed ? qsTr("Try Again") : qsTr("Install")
                tint: Theme.accent
                onClicked: page.install()
            }
        }
    }
}
