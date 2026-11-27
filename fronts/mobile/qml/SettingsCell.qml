import QtQuick

// One block of a SettingsGroup, which says which of its corners are the group's.
Panel {
    id: cell

    property bool roundTop: true
    property bool roundBottom: true

    minimumHeight: Theme.touchLarge
    verticalPadding: Theme.padding * 0.75
    spacing: Theme.spacing
    topLeftRadius: roundTop ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
    topRightRadius: topLeftRadius
    bottomLeftRadius: roundBottom ? Theme.settingsCellRadius : Theme.settingsCellInnerRadius
    bottomRightRadius: bottomLeftRadius
    // A neighbor came or went.
    Behavior on topLeftRadius { enabled: cell.live; NumberAnimation { duration: Theme.durationFast } }
    Behavior on bottomLeftRadius { enabled: cell.live; NumberAnimation { duration: Theme.durationFast } }
}
