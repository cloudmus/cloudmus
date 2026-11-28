import QtQuick

// The caret of a text field, as thick as Android's own, in the accent color.
Rectangle {
    id: caret

    width: Theme.dp(2)
    color: Theme.accent
    // The field shows and hides it; the blinking is its own, from each move on.
    visible: parent ? parent.cursorVisible : false
    SequentialAnimation on opacity {
        running: caret.visible
        loops: Animation.Infinite
        PropertyAction { value: 1 }
        PauseAnimation { duration: 500 }
        PropertyAction { value: 0 }
        PauseAnimation { duration: 500 }
    }
}
