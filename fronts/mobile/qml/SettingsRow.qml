import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// A cell of a SettingsGroup that leads somewhere or does something.
AbstractButton {
    id: row

    property string iconSource
    property string subtitle
    property bool chevron: true
    property bool roundTop: true
    property bool roundBottom: true
    // Instead of `visible`: the row comes and goes with an animation.
    property bool shown: true

    Layout.fillWidth: true
    leftPadding: Theme.padding
    rightPadding: Theme.padding
    focusPolicy: Qt.NoFocus

    PanelMotion {
        id: motion
        item: row
        shown: row.shown
        naturalHeight: Math.max(Theme.touchLarge, content.implicitHeight + Theme.padding * 1.5)
    }

    background: Rectangle {
        color: row.down && ripple.flat ? Theme.surface300 : Theme.surface200
        topLeftRadius: row.roundTop ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
        topRightRadius: topLeftRadius
        bottomLeftRadius: row.roundBottom ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
        bottomRightRadius: bottomLeftRadius
        // A neighbor came or went.
        Behavior on topLeftRadius { enabled: motion.live; NumberAnimation { duration: Theme.durationFast } }
        Behavior on bottomLeftRadius { enabled: motion.live; NumberAnimation { duration: Theme.durationFast } }
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Ripple {
            id: ripple
            pressed: row.pressed
            pressX: row.pressX
            pressY: row.pressY
            radius: Math.max(parent.topLeftRadius, parent.bottomLeftRadius)
        }
    }
    contentItem: RowLayout {
        id: content
        spacing: Theme.padding
        IconImage {
            visible: row.iconSource !== ""
            source: row.iconSource
            sourceSize: Qt.size(Theme.dp(24), Theme.dp(24))
            color: Theme.ink
            Layout.preferredWidth: Theme.dp(24)
            Layout.preferredHeight: Theme.dp(24)
        }
        SettingText { title: row.text; description: row.subtitle }
        IconImage {
            visible: row.chevron
            source: "qrc:/icons/chevron_right.svg"
            sourceSize: Qt.size(Theme.dp(22), Theme.dp(22))
            color: Theme.inkTertiary
            Layout.preferredWidth: Theme.dp(22)
            Layout.preferredHeight: Theme.dp(22)
        }
    }
}
