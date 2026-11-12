import QtQuick
import QtQuick.Layouts

// A setting that's a number.
SettingsCell {
    id: cell

    property string title
    property string description
    property int value
    property int from: 0
    property int to: 100
    property int step: 1
    property string suffix
    property string fromText
    signal moved(int value)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.padding
        SettingText { title: cell.title }
        Stepper {
            value: cell.value
            from: cell.from
            to: cell.to
            step: cell.step
            suffix: cell.suffix
            fromText: cell.fromText
            onMoved: (value) => cell.moved(value)
        }
    }
    // Under it all: beside the buttons a line or two of text runs to a column.
    SettingText { description: cell.description }
}
