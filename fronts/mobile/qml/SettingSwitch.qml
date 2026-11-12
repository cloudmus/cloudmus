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
        onTapped: cell.toggled(!cell.checked)
    }
}
