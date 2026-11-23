import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One proxy from the list, or a new one. Saved as a whole: half of an address
// is of no use to the sources going through it, and they restart for a change.
Page {
    id: page
    bottomPadding: Theme.pageInsetBottom

    property QtObject backend
    property QtObject platform
    property string proxyId
    property string name
    property string kind: "http"
    property string host
    property int port: 1080
    property string username
    property string password
    signal closeRequested()

    readonly property QtObject screen: backend.settingsScreen
    readonly property bool valid: hostField.text.trim() !== "" && portField.acceptableInput

    Component.onCompleted: screen.forgetProxyCheck()

    background: Rectangle { color: Theme.surface0 }

    // A placeholder is gone once something is typed, so each field is named
    // above it.
    component FieldLabel: Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }

    header: PageBar {
        title: page.proxyId ? qsTr("Proxy") : qsTr("New proxy")
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
            SettingsCell {
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing
                Chip {
                    text: "HTTP"
                    checked: page.kind === "http"
                    onClicked: { page.kind = "http"; checked = Qt.binding(() => page.kind === "http") }
                }
                Chip {
                    objectName: "proxySocks"
                    text: "SOCKS5"
                    checked: page.kind === "socks5"
                    onClicked: { page.kind = "socks5"; checked = Qt.binding(() => page.kind === "socks5") }
                }
            }
            }
            SettingsCell {
            FieldLabel { text: qsTr("Name") }
            InputField {
                id: nameField
                objectName: "proxyName"
                text: page.name
                placeholderText: qsTr("e.g. Home SOCKS")
            }
            FieldLabel { text: qsTr("Host or IP") }
            InputField {
                id: hostField
                objectName: "proxyHost"
                text: page.host
                placeholderText: qsTr("e.g. 192.168.1.10")
            }
            FieldLabel { text: qsTr("Port") }
            InputField {
                id: portField
                objectName: "proxyPort"
                text: String(page.port)
                placeholderText: qsTr("1080")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
            }
            FieldLabel { text: qsTr("Username") }
            InputField {
                id: userField
                objectName: "proxyUser"
                text: page.username
                placeholderText: qsTr("Optional")
            }
            FieldLabel { text: qsTr("Password") }
            InputField {
                id: passwordField
                objectName: "proxyPassword"
                text: page.password
                echoMode: showPassword ? TextInput.Normal : TextInput.Password
                placeholderText: qsTr("Optional")
                // Room for the eye so the text doesn't run under it.
                rightPadding: Theme.padding + eye.width
                property bool showPassword: false
                IconButton {
                    id: eye
                    objectName: "proxyPasswordEye"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.touch - Theme.dp(8)
                    iconName: passwordField.showPassword ? "visibility_off" : "visibility"
                    color: Theme.inkSecondary
                    onClicked: passwordField.showPassword = !passwordField.showPassword
                }
            }
            }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing
                TextButton {
                    objectName: "proxySave"
                    text: qsTr("Save")
                    enabled: page.valid
                    onClicked: {
                        page.screen.saveProxy(page.proxyId, nameField.text, page.kind, hostField.text,
                                              parseInt(portField.text), userField.text, passwordField.text)
                        page.closeRequested()
                    }
                }
                TextButton {
                    objectName: "proxyCheck"
                    text: qsTr("Check")
                    enabled: page.valid && !page.screen.proxyChecking
                    busy: page.screen.proxyChecking
                    tint: page.screen.proxyCheckText === "" ? "transparent"
                          : page.screen.proxyCheckOk ? Theme.success : Theme.danger
                    onClicked: page.screen.checkProxy(page.kind, hostField.text, parseInt(portField.text),
                                                      userField.text, passwordField.text)
                }
                TextButton {
                    objectName: "proxyRemove"
                    visible: page.proxyId !== ""
                    text: qsTr("Remove")
                    onClicked: {
                        page.screen.removeProxy(page.proxyId)
                        page.closeRequested()
                    }
                }
            }
        }
    }

    Connections {
        target: page.screen
        function onProxyCheckingChanged() {
            if (!page.screen.proxyChecking && page.screen.proxyCheckText !== "")
                toast.show(page.screen.proxyCheckText, !page.screen.proxyCheckOk, undefined, page.screen.proxyCheckOk)
        }
    }
    Toast { id: toast; platform: page.platform }
}
