import QtQuick

// The app's one way of putting something on the clipboard from a button: the
// copy glyph beside what it copies (`content`), a check mark for a moment once
// pressed.
IconButton {
    id: button

    property string content
    // Ui::Platform, for the clipboard.
    property QtObject platform: null
    property bool copied: false

    size: Theme.dp(36)
    iconName: copied ? "check" : "content_copy"
    color: copied ? Theme.accent : Theme.inkSecondary
    // Shown again, or holding something else, it hasn't copied that.
    onContentChanged: copied = false
    onVisibleChanged: if (!visible) copied = false
    onClicked: {
        button.platform.copyText(button.content)
        copied = true
        revert.restart()
    }

    Timer {
        id: revert
        interval: 1500
        onTriggered: button.copied = false
    }
}
