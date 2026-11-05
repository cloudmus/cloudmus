pragma Singleton

import QtQuick

// Colors, sizes and type of the mobile front. Colors are the desktop
// front's palette (fronts/qt/src/Theme/Tokens.cpp); every size is in dp
// times `scale`, so one layout serves a phone and a 14" head unit.
QtObject {
    id: theme

    // Set by Main.qml from the window: auto from its short side unless the
    // user picked a scale in Settings.
    property real scale: 1.0
    property bool dark: Qt.styleHints.colorScheme !== Qt.ColorScheme.Light

    function dp(value) { return Math.round(value * scale) }

    // Auto scale: a phone (~360-420 dp across) is 1.0; more room grows the
    // controls with it, up to twice: a 14" Full HD head unit (~1000 dp
    // across, at 160 dpi) is 2.0 — at arm's length, in a moving car, 1.5
    // was too small. A tablet comes in between (800 dp: 1.6).
    function autoScale(width, height) {
        const shortSide = Math.min(width, height)
        return Math.max(1.0, Math.min(2.0, shortSide / 500))
    }

    readonly property color surface0: dark ? "#14100D" : "#EEE8E2"
    // Under all the pages, seen as they change, and the splash: plain white or dark.
    readonly property color windowBackground: dark ? "#14100D" : "#FFFFFF"
    readonly property color surface100: dark ? "#1C1714" : "#FFFFFF"
    readonly property color surface200: dark ? "#241E1A" : "#FFFFFF"
    readonly property color surface300: dark ? "#2E2621" : "#E2D9D1"
    readonly property color surface400: dark ? "#3A3029" : "#D3C7BD"
    readonly property color border: dark ? "#362C25" : "#D6CBC2"
    readonly property color borderStrong: dark ? "#4A3D34" : "#B5A69A"
    readonly property color ink: dark ? "#F5EFEA" : "#1C1511"
    readonly property color inkSecondary: dark ? "#B4A89D" : "#5A4D44"
    readonly property color inkTertiary: dark ? "#7C7167" : "#807165"
    readonly property color accent: dark ? "#FF6A45" : "#C23D16"
    // On the generated background, which is dark in either theme.
    readonly property color accentOnAura: "#FF6A45"
    readonly property color accentPressed: dark ? "#E85A38" : "#8E2A0C"
    // Not "onAccent": QML takes a name like that for a signal handler, and the
    // color came out black whatever it was set to.
    readonly property color inkOnAccent: dark ? "#1C1006" : "#FFFFFF"
    readonly property color success: "#2E8B57"
    readonly property color danger: "#C6322B"
    readonly property color warning: "#D89A2B"

    property string fontFamily: "Manrope"
    readonly property string monoFamily: "monospace"

    readonly property int fontSmall: dp(13)
    readonly property int fontBody: dp(15)
    readonly property int fontTitle: dp(17)
    readonly property int fontHeadline: dp(24)

    readonly property int spacing: dp(8)
    readonly property int padding: dp(16)
    readonly property int radius: dp(10)
    // Of every cover, at any size, and of what's drawn over or around one.
    readonly property int coverRadius: dp(12)
    // Settings: a group's outer corners, where its cells meet, and the spaces.
    readonly property int settingsCellRadius: dp(20)
    readonly property int settingsCellInnerRadius: dp(4)
    readonly property int settingsCellGap: dp(2)
    readonly property int settingsGroupGap: dp(24)
    // Touch targets: 48 dp is the platform's minimum; 56 for the main ones.
    readonly property int touch: dp(48)
    readonly property int touchLarge: dp(56)
    readonly property int rowHeight: dp(64)
    readonly property int barHeight: dp(56)
    // What the status bar covers at the top: nothing goes under that one.
    property int insetTop: 0
    // What the system's navigation bar covers at the bottom: the window goes on
    // under it, for the generated background to reach the screen's edge, and
    // what's to be read or pressed keeps clear of it. Set by Main.qml.
    property int insetBottom: 0

    // Fling physics (ScrollList): Qt's defaults (2500, 1500) cut a fast
    // swipe short and stop it hard. These felt native on a Pixel 7; still
    // tunable in Developer tools.
    property real flickMaxVelocityBase: 12000
    property real flickDecelerationBase: 2000
    readonly property real flickMaxVelocity: flickMaxVelocityBase * scale
    readonly property real flickDeceleration: flickDecelerationBase * scale

    // Blur what's behind glass again for each frame it changes in, rather than once
    // as the glass comes. Costs the GPU that all along: a Developer tools setting,
    // to see how it does on a device. Set by Main.qml.
    property bool dynamicBlur: false

    readonly property int durationFast: 150
    readonly property int durationPage: 320

    // The generated background (AuraBackground; AuraImageProvider for the
    // form): one place, as whoever loads it beforehand must ask for the same.
    function auraUrl(seed, edgeFadePixels) {
        return "image://aura/h" + surface0.toString().substring(1) + "/" + edgeFadePixels + "/"
            + encodeURIComponent(seed)
    }

    // When a track played, as on the desktop.
    function formatPlayedAt(when) {
        const date = new Date(when)
        if (!when || isNaN(date))
            return ""
        const today = new Date()
        const yesterday = new Date(today.getFullYear(), today.getMonth(), today.getDate() - 1)
        const same = (a, b) => a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
                               && a.getDate() === b.getDate()
        const day = same(date, today) ? qsTr("Today")
                  : same(date, yesterday) ? qsTr("Yesterday")
                  : Qt.formatDate(date, "dd.MM.yyyy")
        return day + ", " + Qt.formatTime(date, "HH:mm")
    }

    function formatTime(ms) {
        if (!(ms > 0))
            return "0:00"
        const total = Math.floor(ms / 1000)
        const seconds = total % 60
        return Math.floor(total / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }
}
