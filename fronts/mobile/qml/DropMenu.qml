import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Templates as T

// The menu a button drops: opens under its parent button, its right edges
// lined up. Its panel unfolds downwards, then the entries come in one
// after another, each sliding down a little as it fades in, as Android's own
// menus do. With `upwards` it's all the other way round, for a button at the
// bottom of the screen: it opens over the button, unfolds upwards, and the
// entries come in from the bottom one, sliding up. Fill it with
// DropMenuItems. Every dropdown menu is this, so they all appear the same way.
Menu {
    id: menu

    // Opens over the button rather than under it.
    property bool upwards: false
    // 0 → 1 while it opens: how far the panel has unfolded.
    property real unfold: 1
    // Only up or down: a panel widening to the left reads as the menu jumping
    // sideways, the more so when the first frames come late.
    readonly property real _panelWidth: width
    readonly property real _panelHeight: height * (0.3 + 0.7 * unfold)

    // The button it drops from: the parent it's declared in, unless set.
    property Item anchor

    // Placed over the whole window, by the button's center: a button shrinks
    // under the finger (IconButton's scale) and grows back as the menu opens,
    // and a menu placed through that scale moves with it, then jumps when
    // it's placed again. The center stays put while it scales.
    Component.onCompleted: {
        if (!anchor)
            anchor = parent
        parent = T.Overlay.overlay
    }
    function _place() {
        if (!anchor || !parent)
            return
        const center = anchor.mapToItem(parent, anchor.width / 2, anchor.height / 2)
        // Clear of the window's edges and the system's bars at the sides. A
        // button at the left (a head unit's) has its left edge lined up
        // instead, rather than the menu going off past it.
        const left = Theme.insetLeft + _edge
        const right = parent.width - Theme.insetRight - _edge
        let at = center.x + anchor.width / 2 - width
        if (at < left)
            at = center.x - anchor.width / 2
        x = Math.round(Math.max(left, Math.min(at, right - width)))
        y = Math.round(upwards ? center.y - anchor.height / 2 - height
                               : center.y + anchor.height / 2)
    }
    // As wide as its longest entry, within bounds: a long label elides only
    // past the widest a menu gets. Set as it opens (_fit()), for the entries
    // available then.
    width: Theme.dp(200)
    readonly property real _edge: Theme.dp(8)
    function _fit() {
        let widest = 0
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            if (entry && entry.available !== false)
                widest = Math.max(widest, entry.naturalWidth !== undefined ? entry.naturalWidth : entry.implicitWidth)
        }
        const room = parent ? parent.width - Theme.insetLeft - Theme.insetRight - _edge * 2 : Theme.dp(360)
        width = Math.ceil(Math.max(Theme.dp(200), Math.min(widest + leftPadding + rightPadding, Theme.dp(360), room)))
    }
    padding: Theme.dp(4)
    // A tap beside it only closes it, not lands on what's under it.
    modal: true
    dim: false

    // The popup itself isn't scaled: its positioner places it by the scaled
    // rectangle, which shows as the menu jumping as it opens. The panel and
    // what shows through it change size instead.
    background: Item {
        Rectangle {
            anchors.right: parent.right
            // Growing away from the button.
            y: menu.upwards ? parent.height - height : 0
            width: menu._panelWidth
            height: menu._panelHeight
            radius: Theme.radius
            // Raised over the page: white in the light theme, its shadow is what
            // sets it apart.
            color: Theme.dark ? Theme.surface300 : Theme.surface200
            // Without a GPU an effect shows nothing at all: no shadow then.
            layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
            layer.effect: MultiEffect {
                autoPaddingEnabled: true
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.35
                shadowBlur: 0.5
                shadowVerticalOffset: Theme.dp(2)
            }
        }
    }

    contentItem: Item {
        id: content
        // A Column, not the style's ListView: a ListView lays its entries out
        // lazily, so the menu's height grows a frame after it opens, and the
        // panel unfolding by it jumps.
        implicitWidth: entries.implicitWidth
        implicitHeight: entries.implicitHeight
        // The part of the entries over the unfolded panel.
        Item {
            id: window
            x: menu.width - menu._panelWidth - menu.leftPadding
            y: menu.upwards ? menu.height - menu.topPadding - menu._panelHeight : -menu.topPadding
            width: menu._panelWidth
            height: menu._panelHeight
            clip: true
            Column {
                id: entries
                x: -window.x
                y: -window.y
                width: content.width
                Repeater { model: menu.contentModel }
            }
        }
    }

    onAboutToShow: {
        _fit()
        // An entry that came or went while it was closed isn't placed yet:
        // the first frame would have the old height. Before placing: over
        // the button it's placed by its height.
        entries.forceLayout()
        _place()
        // The one nearest the button first.
        let shown = 0
        for (let n = 0; n < count; ++n) {
            const entry = itemAt(upwards ? count - 1 - n : n)
            if (entry && entry.available && entry.appear)
                entry.appear(60 + shown++ * 35, upwards)
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100; easing.type: Easing.OutCubic }
            NumberAnimation { property: "unfold"; from: 0; to: 1; duration: 240; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast; easing.type: Easing.InCubic }
    }
}
