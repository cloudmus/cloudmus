import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Effects
import QtQuick.Layouts

// A choice of many, in the theme's colors: a field like InputField with a
// chevron that turns as its list opens, and the list a raised panel as
// DropMenu's, unfolding under the field (over it, near the screen's bottom)
// with a check by the current entry. For a list that grows (the languages);
// a few fixed choices are Chips.
ComboBox {
    id: control

    Layout.fillWidth: true
    implicitHeight: Theme.touch
    leftPadding: Theme.padding
    rightPadding: Theme.padding + Theme.dp(28)
    focusPolicy: Qt.NoFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    // Where the finger went down, for the ripple: a ComboBox has no pressX.
    PointHandler {
        id: touch
        acceptedButtons: Qt.AllButtons
    }
    property point _pressAt: Qt.point(width / 2, height / 2)
    onPressedChanged: if (pressed && touch.active) _pressAt = touch.point.position

    background: Rectangle {
        radius: Theme.dp(10)
        color: control.down && fieldRipple.flat ? Theme.surface400 : Theme.surface300
        border.width: control.popup.visible ? Theme.dp(2) : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        // The touch feedback of the buttons, from where the finger is.
        Ripple {
            id: fieldRipple
            pressed: control.pressed
            pressX: control._pressAt.x
            pressY: control._pressAt.y
            radius: parent.radius
        }
    }

    contentItem: Text {
        text: control.displayText
        color: Theme.ink
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: IconImage {
        x: control.width - width - Theme.dp(12)
        y: (control.height - height) / 2
        source: "qrc:/icons/expand_more.svg"
        sourceSize.width: Theme.dp(22)
        sourceSize.height: Theme.dp(22)
        color: control.popup.visible ? Theme.accent : Theme.inkSecondary
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
    }

    delegate: ItemDelegate {
        id: entry
        required property int index
        required property var model
        readonly property bool current: control.currentIndex === index
        width: ListView.view ? ListView.view.width : implicitWidth
        height: Theme.touch
        leftPadding: Theme.dp(12)
        rightPadding: Theme.dp(12)
        contentItem: RowLayout {
            spacing: Theme.dp(12)
            Text {
                Layout.fillWidth: true
                text: entry.model[control.textRole]
                color: entry.current ? Theme.accent : Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: entry.current ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            IconImage {
                visible: entry.current
                source: "qrc:/icons/check.svg"
                sourceSize.width: Theme.dp(20)
                sourceSize.height: Theme.dp(20)
                color: Theme.accent
            }
        }
        background: Rectangle {
            radius: Theme.radius
            color: entry.down && ripple.flat ? Theme.surface400
                 : entry.current ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12) : "transparent"
            Ripple {
                id: ripple
                pressed: entry.pressed
                pressX: entry.pressX
                pressY: entry.pressY
                radius: parent.radius
            }
        }
    }

    popup: Popup {
        id: list
        // Under the field, or over it when there's no room below.
        readonly property real _room: control.Window.height > 0
            ? control.Window.height - control.mapToItem(null, 0, control.height).y - Theme.insetBottom - Theme.dp(8)
            : height
        readonly property bool _upwards: _room < implicitHeight && control.mapToItem(null, 0, 0).y > _room
        y: _upwards ? -height - Theme.dp(6) : control.height + Theme.dp(6)
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Theme.dp(360))
        padding: Theme.dp(4)
        // 0 → 1 while it opens, as DropMenu's panel.
        property real unfold: 1
        enter: Transition {
            NumberAnimation { property: "unfold"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 }
        }
        onAboutToShow: opacity = 1
        // The current one in view.
        onOpened: contentItem.positionViewAtIndex(control.currentIndex, ListView.Contain)

        background: Item {
            Rectangle {
                y: list._upwards ? parent.height - height : 0
                width: parent.width
                height: parent.height * (0.3 + 0.7 * list.unfold)
                radius: Theme.radius
                color: Theme.dark ? Theme.surface300 : Theme.surface200
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

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            opacity: list.unfold
            boundsBehavior: Flickable.StopAtBounds
            maximumFlickVelocity: Theme.flickMaxVelocity
            flickDeceleration: Theme.flickDeceleration
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }
}
