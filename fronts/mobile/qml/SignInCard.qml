import QtQuick
import QtQuick.Layouts

// Signing in to any source that takes a code entered on a web page: the button
// asking for one, then where to enter it, the code itself, and the address as
// a QR code (a head unit may have no browser, and typing an address into a
// phone in a car is no fun).
Panel {
    id: card

    // The page to open and the code to enter there; empty until asked for.
    property string url
    property string code
    property string error
    property QtObject platform: null
    // The QR code beside the rest rather than under it.
    property bool wide: false
    signal signInRequested()

    GridLayout {
        id: grid
        Layout.fillWidth: true
        // The panel's width, whatever's in it would like.
        Layout.minimumWidth: 0
        columns: card.wide && card.url !== "" ? 2 : 1
        columnSpacing: Theme.padding * 2
        rowSpacing: Theme.padding
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.dp(12)
            BodyText {
                text: qsTr("Sign in to see the playlists")
                font.weight: Font.DemiBold
            }
            BodyText {
                visible: text !== ""
                text: card.error
                color: Theme.danger
            }
            BodyText {
                objectName: "authLink"
                visible: text !== ""
                textFormat: Text.StyledText
                linkColor: Theme.accent
                text: card.url !== ""
                      ? qsTr("Open %1 on any device and enter this code:")
                            .arg("<a href=\"" + card.url + "\">" + card.url + "</a>")
                      : ""
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
            RowLayout {
                visible: card.code !== ""
                spacing: Theme.dp(8)
                Text {
                    objectName: "authCode"
                    text: card.code
                    color: Theme.accent
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontHeadline
                    font.weight: Font.Bold
                    TapHandler { onTapped: copyCode.clicked() }
                }
                CopyButton {
                    id: copyCode
                    objectName: "copyAuthCode"
                    content: card.code
                    platform: card.platform
                }
            }
            // While a code is shown the source is waiting for it: asking again
            // would only change the code.
            TextButton {
                objectName: "signIn"
                visible: card.code === ""
                Layout.fillWidth: true
                text: qsTr("Sign in")
                tint: Theme.accent
                onClicked: card.signInRequested()
            }
        }
        QrCode {
            objectName: "authQr"
            readonly property real side: Math.min(card.width - card.padding * 2, Theme.dp(card.wide ? 240 : 300))
            visible: card.url !== ""
            text: card.url
            platform: card.platform
            Layout.preferredWidth: side
            Layout.preferredHeight: side
            Layout.alignment: card.wide ? Qt.AlignTop : Qt.AlignHCenter
            // Room for the corner button, which sticks out.
            Layout.bottomMargin: Theme.dp(8)
        }
    }
}
