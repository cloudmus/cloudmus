import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A number set a step at a time, held to run: no keyboard to bring up over a
// settings page, and a hit area a finger finds in a car.
RowLayout {
    id: stepper

    property int value
    property int from: 0
    property int to: 100
    property int step: 1
    property string suffix
    // What `from` is called when it means something of its own: "Off", "Auto".
    property string fromText
    signal moved(int value)

    spacing: Theme.dp(4)
    Layout.fillWidth: false

    component StepButton: AbstractButton {
        id: button
        implicitWidth: Theme.touch
        implicitHeight: Theme.touch
        autoRepeat: true
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: height / 2
            color: button.down ? Theme.surface400 : Theme.surface300
        }
        contentItem: Text {
            text: button.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: button.enabled ? Theme.ink : Theme.inkTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
        }
    }

    StepButton {
        objectName: "stepDown"
        text: "−"
        enabled: stepper.value > stepper.from
        onClicked: stepper.moved(Math.max(stepper.from, stepper.value - stepper.step))
    }
    Text {
        Layout.minimumWidth: Theme.dp(72)
        horizontalAlignment: Text.AlignHCenter
        text: stepper.value === stepper.from && stepper.fromText
              ? stepper.fromText : stepper.value + stepper.suffix
        color: Theme.ink
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }
    StepButton {
        objectName: "stepUp"
        text: "+"
        enabled: stepper.value < stepper.to
        onClicked: stepper.moved(Math.min(stepper.to, stepper.value + stepper.step))
    }
}
