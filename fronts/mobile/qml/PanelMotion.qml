import QtQuick
import QtQuick.Layouts

// How a block of a page comes, goes and changes height: smoothly, never at
// once, the rest of the page moving with it. It drives the item's
// `implicitHeight`, `visible`, `opacity`, `clip` and `Layout.bottomMargin`,
// so the item sets none of them itself. Panel.qml and the setting cells use
// it; reach for it only for an item that can't be a Panel.
QtObject {
    id: motion

    required property Item item
    // The height the item wants when it's all there.
    required property real naturalHeight
    property bool shown: true
    // Off while the page changes as a whole, with a transition of its own.
    property bool animated: true
    // Off for a block of blocks: they move, and it follows them.
    property bool smoothResize: true

    // Not as the page is first laid out: nothing "appears" then.
    readonly property bool live: animated && _settled
    property bool _settled: false
    property Timer _start: Timer {
        interval: 100
        running: true
        onTriggered: motion._settled = true
    }
    // Nor as the width changes (the screen turned): the heights that follow
    // from it are the same blocks laid out anew.
    property Timer _widening: Timer { interval: 100 }
    property Connections _widths: Connections {
        target: motion.item
        function onWidthChanged() { motion._widening.restart() }
    }

    // 0 gone, 1 here.
    property real presence: shown ? 1 : 0
    Behavior on presence {
        enabled: motion.live
        NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic }
    }
    // On its way to naturalHeight; kept as it was while the item goes, so that
    // what's emptied meanwhile doesn't make it jump.
    property real height: 0
    Behavior on height {
        // Coming in, it already moves: the height is followed as it is.
        enabled: motion.live && motion.smoothResize && motion.presence === 1 && !motion._widening.running
        NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic }
    }
    property Binding _follow: Binding {
        target: motion
        property: "height"
        value: motion.naturalHeight
        when: motion.shown
        restoreMode: Binding.RestoreNone
    }

    // The layout's spacing goes with the item, not in a step at the end.
    readonly property real _gap: {
        const layout = item.parent
        if (!layout)
            return 0
        return (layout.rowSpacing !== undefined ? layout.rowSpacing : layout.spacing) || 0
    }
    property Binding _implicitHeight: Binding {
        target: motion.item
        property: "implicitHeight"
        value: motion.height * motion.presence
    }
    property Binding _visible: Binding {
        target: motion.item
        property: "visible"
        value: motion.presence > 0
    }
    property Binding _opacity: Binding {
        target: motion.item
        property: "opacity"
        value: motion.presence
    }
    property Binding _clip: Binding {
        target: motion.item
        property: "clip"
        value: motion.presence < 1 || motion.height !== motion.naturalHeight
    }
    property Binding _margin: Binding {
        target: motion.item.Layout
        property: "bottomMargin"
        value: -(1 - motion.presence) * motion._gap
    }
}
