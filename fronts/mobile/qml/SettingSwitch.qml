import QtQuick
import QtQuick.Layouts

// A setting that's on or off. The whole cell takes the tap: a switch is small
// to hit in a car.
SettingsCell {
    id: cell

    property string title
    property string description
    property bool checked
    signal toggled(bool checked)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padding
        SettingText { title: cell.title; description: cell.description }
        AppSwitch {
            objectName: "switch"
            checked: cell.checked
            onToggled: cell.toggled(checked)
        }
    }
    TapHandler {
        id: tap
        onTapped: cell.toggled(!cell.checked)
    }
    // The handler sits in the cell's column; the ripple covers the whole cell.
    Ripple {
        readonly property point _at: tap.parent ? tap.parent.mapToItem(cell, tap.point.pressPosition) : Qt.point(0, 0)
        parent: cell
        pressed: tap.pressed
        pressX: _at.x
        pressY: _at.y
        radius: Math.max(cell.topLeftRadius, cell.bottomLeftRadius)
    }
}
