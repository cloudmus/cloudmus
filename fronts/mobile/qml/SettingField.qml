import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One setting a source describes of itself (docs/protocol.md §7.7), whatever
// its kind, with its group's heading if it comes first in one. Text is sent when
// the field is left: a source may check it, and shouldn't at each letter.
ColumnLayout {
    id: field

    required property string key
    required property string groupTitle
    required property string groupDescription
    required property string label
    required property string description
    required property string type
    required property var value
    required property bool isSet
    required property int minimum
    required property int maximum
    required property var options
    required property string placeholder
    required property string error

    signal changed(string key, var value)

    Layout.fillWidth: true
    spacing: Theme.dp(6)

    readonly property bool isText: type === "string" || type === "path" || type === "secret"

    SectionHeading {
        visible: field.groupTitle !== ""
        text: field.groupTitle
    }
    Text {
        Layout.fillWidth: true
        visible: field.groupDescription !== ""
        text: field.groupDescription
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }

    RowLayout {
        Layout.fillWidth: true
        BodyText { text: field.label; textFormat: Text.PlainText }
        Switch {
            objectName: "field_" + field.key
            visible: field.type === "boolean"
            checked: field.value === true
            onToggled: field.changed(field.key, checked)
        }
        Stepper {
            visible: field.type === "integer"
            value: Number(field.value)
            from: field.minimum
            to: field.maximum
            onMoved: (value) => field.changed(field.key, value)
        }
    }
    Flow {
        Layout.fillWidth: true
        visible: field.type === "enum"
        spacing: Theme.spacing
        Repeater {
            model: field.type === "enum" ? field.options : []
            Chip {
                required property var modelData
                objectName: "option_" + modelData.value
                text: modelData.label
                checked: field.value === modelData.value
                onClicked: {
                    checked = Qt.binding(() => field.value === modelData.value)
                    field.changed(field.key, modelData.value)
                }
            }
        }
    }
    InputField {
        id: edit
        objectName: "field_" + field.key
        visible: field.isText
        text: field.type === "secret" || !field.isText ? "" : String(field.value === undefined ? "" : field.value)
        echoMode: field.type === "secret" ? TextInput.Password : TextInput.Normal
        placeholderText: field.type === "secret" && field.isSet ? qsTr("Saved: type to replace") : field.placeholder
        onEditingFinished: send()
        function send() {
            if (visible)
                field.changed(field.key, text)
            if (field.type === "secret")
                clear()
        }
        // The back button leaves without the field ever losing focus.
        Component.onDestruction: if (activeFocus) send()
    }
    Text {
        Layout.fillWidth: true
        visible: field.description !== ""
        text: field.description
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }
    Text {
        objectName: "fieldError_" + field.key
        Layout.fillWidth: true
        visible: field.error !== ""
        text: field.error
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }
}
