import QtQuick
import QtQuick.Controls.impl
import QtQuick.Layouts

// One of the people on the About page: the person icon, then their name,
// email and page, a line each; the two are links.
Rectangle {
    id: card

    // A map of name, email, url and shownUrl (Ui::Platform::authors()).
    required property var person

    Layout.fillWidth: true
    implicitHeight: row.implicitHeight + Theme.padding * 1.5
    radius: Theme.settingsCellRadius
    color: Theme.surface200

    function _link(href, text) {
        return "<a href=\"" + href + "\" style=\"color:" + Theme.accent + "; text-decoration:none;\">" + text + "</a>"
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.padding
        anchors.rightMargin: Theme.padding
        spacing: Theme.padding

        IconImage {
            Layout.alignment: Qt.AlignTop
            source: "qrc:/icons/person.svg"
            sourceSize.width: Theme.dp(36)
            sourceSize.height: Theme.dp(36)
            color: Theme.accent
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.dp(2)
            Text {
                objectName: "personName"
                Layout.fillWidth: true
                text: card.person.name
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            component LinkLine: Text {
                Layout.fillWidth: true
                textFormat: Text.RichText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.inkSecondary
                elide: Text.ElideRight
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
            LinkLine {
                objectName: "personEmail"
                visible: !!card.person.email
                text: card.person.email ? card._link("mailto:" + card.person.email, card.person.email) : ""
            }
            LinkLine {
                objectName: "personPage"
                visible: !!card.person.url
                text: card.person.url ? card._link(card.person.url, card.person.shownUrl) : ""
            }
        }
    }
}
