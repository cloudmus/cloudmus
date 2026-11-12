import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One setting a source describes of itself (docs/protocol.md §7.7), whatever
// its kind: a cell of a group that comes in rows from the source, with the
// group's title on its first. Text is sent when the field is left: a source may
// check it, and shouldn't at each letter.
ColumnLayout {
    id: field

    required property string key
    required property string groupTitle
    required property string groupDescription
    required property bool isFirst
    required property bool isLast
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
    Layout.topMargin: isFirst ? Theme.settingsGroupGap - Theme.settingsCellGap : 0
    spacing: Theme.dp(8)

    readonly property bool isText: type === "string" || type === "path" || type === "secret"

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.dp(4)
        visible: field.groupTitle !== ""
        text: field.groupTitle
        textFormat: Text.PlainText
        color: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
    }
    Text {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.dp(4)
        Layout.rightMargin: Theme.dp(4)
        visible: field.groupDescription !== ""
        text: field.groupDescription
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.inkSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
    }

    SettingsCell {
        roundTop: field.isFirst
        roundBottom: field.isLast

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.padding
            SettingText {
                title: field.label
                description: field.type === "boolean" ? field.description : ""
            }
            AppSwitch {
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
        SettingText { description: field.type === "boolean" ? "" : field.description }
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
}
