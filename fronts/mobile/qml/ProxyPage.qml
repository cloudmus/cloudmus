import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One proxy from the list, or a new one. Saved as a whole: half of an address
// is of no use to the sources going through it, and they restart for a change.
Page {
    id: page
    bottomPadding: Theme.insetBottom

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
            InputField {
                id: nameField
                objectName: "proxyName"
                text: page.name
                placeholderText: qsTr("Name, e.g. Home SOCKS")
            }
            InputField {
                id: hostField
                objectName: "proxyHost"
                text: page.host
                placeholderText: qsTr("Host or IP")
            }
            InputField {
                id: portField
                objectName: "proxyPort"
                text: String(page.port)
                placeholderText: qsTr("Port")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
            }
            InputField {
                id: userField
                objectName: "proxyUser"
                text: page.username
                placeholderText: qsTr("Username (optional)")
            }
            InputField {
                id: passwordField
                objectName: "proxyPassword"
                text: page.password
                echoMode: TextInput.Password
                placeholderText: qsTr("Password (optional)")
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
            Spinner { visible: page.screen.proxyChecking; running: visible }
            Text {
                objectName: "proxyCheckResult"
                Layout.fillWidth: true
                visible: text !== ""
                text: page.screen.proxyCheckText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: page.screen.proxyCheckOk ? Theme.inkSecondary : Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
