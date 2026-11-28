import QtQuick

// ScrollList's physics and edges for content that isn't a list.
Flickable {
    id: area

    // What's behind it, for the content to dissolve into.
    property alias fadeColor: edgeFade.color
    // Off for a background that isn't one color, to dissolve the content some
    // other way.
    property alias fadeShown: edgeFade.visible

    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.StopAtBounds
    maximumFlickVelocity: Theme.flickMaxVelocity
    flickDeceleration: Theme.flickDeceleration
    pixelAligned: true

    // What the on-screen keyboard covers of the area (the window isn't made
    // shorter for it, see Appearance.java). That much room is added below the
    // content, and a field typed in here is scrolled above the keyboard, as in
    // the system's own forms.
    readonly property real keyboardOverlap: Theme.insetKeyboard <= 0 || !Window.window ? 0
        : Math.max(0, mapToItem(null, 0, height).y - (Window.window.height - Theme.insetKeyboard))
    bottomMargin: keyboardOverlap

    function revealFocused() {
        const item = Window.activeFocusItem
        if (keyboardOverlap <= 0 || !item || !(item instanceof TextInput))
            return
        let inside = item.parent
        while (inside && inside !== contentItem)
            inside = inside.parent
        if (!inside)
            return
        const top = item.mapToItem(contentItem, 0, 0).y - Theme.padding
        const bottom = top + item.height + Theme.padding * 2
        const shown = height - keyboardOverlap
        if (bottom > contentY + shown)
            contentY = Math.min(bottom - shown, contentHeight + keyboardOverlap - height)
        else if (top < contentY)
            contentY = Math.max(0, top)
    }
    onKeyboardOverlapChanged: Qt.callLater(revealFocused)

    // A Flickable leaves contentY where it was as the content gets shorter (or
    // the area taller): scrolled down a long page, a short one that comes in
    // its place would show only the room past its end. Back within bounds.
    function _clamp() {
        if (moving || dragging)
            return
        const end = Math.max(0, contentHeight + bottomMargin - height)
        if (contentY > end)
            contentY = end
    }
    onContentHeightChanged: Qt.callLater(_clamp)
    onHeightChanged: Qt.callLater(_clamp)
    onBottomMarginChanged: Qt.callLater(_clamp)
    onMovementEnded: _clamp()
    Connections {
        target: area.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(area.revealFocused) }
    }

    ScrollEdgeFade {
        id: edgeFade
        // The view's, not its content's: it stays put as that scrolls.
        parent: area
        view: area
    }
}
