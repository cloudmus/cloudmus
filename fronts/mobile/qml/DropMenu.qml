import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Templates as T

// The menu a button drops: opens under its parent button, its right edges
// lined up. Its panel unfolds downwards, then the entries come in one
// after another, each sliding down a little as it fades in, as Android's own
// menus do. Fill it with DropMenuItems. Every dropdown menu is this, so they
// all appear the same way.
Menu {
    id: menu

    // 0 → 1 while it opens: how far the panel has unfolded.
    property real unfold: 1
    // Only downwards: a panel widening to the left reads as the menu jumping
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
        x = Math.round(center.x + anchor.width / 2 - width)
        y = Math.round(center.y + anchor.height / 2)
    }
    width: Theme.dp(220)
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
            anchors.top: parent.top
            width: menu._panelWidth
            height: menu._panelHeight
            radius: Theme.radius
            color: Theme.surface300
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
            y: -menu.topPadding
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
        _place()
        // An entry that came or went while it was closed isn't placed yet:
        // the first frame would have the old height.
        entries.forceLayout()
        let shown = 0
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            if (entry && entry.available && entry.appear)
                entry.appear(60 + shown++ * 35)
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
