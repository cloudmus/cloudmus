import QtQuick
import QtQuick.Layouts

// A block of a page, a card unless made transparent. It comes, goes (`shown`,
// never `visible`) and changes height with an animation (PanelMotion), so
// whatever is put in it or taken out opens it up or folds it.
Rectangle {
    id: panel

    property bool shown: true
    // Off while the page changes as a whole, with a transition of its own.
    property alias animated: motion.animated
    // Off for a panel of panels: they move, and it follows them.
    property alias smoothResize: motion.smoothResize
    // Past the first layout: other changes of the panel's look may move too.
    readonly property alias live: motion.live
    property real padding: Theme.padding
    property real verticalPadding: padding
    property real minimumHeight: 0
    property alias spacing: body.spacing
    default property alias content: body.data

    Layout.fillWidth: true
    radius: Theme.radius
    color: Theme.surface200

    PanelMotion {
        id: motion
        item: panel
        shown: panel.shown
        naturalHeight: Math.max(panel.minimumHeight, body.implicitHeight + panel.verticalPadding * 2)
    }

    // From the top, at its own height: a panel on its way uncovers it.
    ColumnLayout {
        id: body
        x: panel.padding
        y: (motion.naturalHeight - implicitHeight) / 2
        width: panel.width - panel.padding * 2
        height: implicitHeight
    }
}
