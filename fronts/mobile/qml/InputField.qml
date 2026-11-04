import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A line of text on a settings page. No word suggestions: it's an address, a
// name or a token, not a sentence.
TextField {
    id: field

    Layout.fillWidth: true
    implicitHeight: Theme.touch
    leftPadding: Theme.padding
    rightPadding: Theme.padding
    placeholderTextColor: Theme.inkTertiary
    color: Theme.ink
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
    background: Rectangle {
        radius: Theme.dp(10)
        color: Theme.surface300
        border.width: field.activeFocus ? Theme.dp(2) : 0
        border.color: Theme.accent
    }
}
