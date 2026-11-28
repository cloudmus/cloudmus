import QtQuick
import QtQuick.Layouts

// A setting's name and what it's about, beside its control or above it.
ColumnLayout {
    property string title
    property string description

    Layout.fillWidth: true
    spacing: Theme.dp(2)

    Text {
        Layout.fillWidth: true
        visible: text !== ""
        text: parent.title
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.ink
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }
    Text {
        Layout.fillWidth: true
        visible: text !== ""
        text: parent.description
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }
}
