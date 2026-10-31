pragma Singleton

import QtQuick

// Colors, sizes and type of the mobile front. Colors are the desktop
// front's palette (fronts/qt/src/Theme/Tokens.cpp); every size is in dp
// times `scale`, so one layout serves a phone and a 14" head unit.
QtObject {
    id: theme

    // Set by Main.qml from the window: auto from its short side unless the
    // user picked a scale in Diagnostics.
    property real scale: 1.0
    property bool dark: Qt.styleHints.colorScheme !== Qt.ColorScheme.Light

    function dp(value) { return Math.round(value * scale) }

    // Auto scale: a phone (~360-420 dp across) is 1.0; more room — a
    // tablet, a head unit — grows the controls, up to half again, since
    // they're used at arm's length.
    function autoScale(width, height) {
        const shortSide = Math.min(width, height)
        return Math.max(1.0, Math.min(1.5, shortSide / 420))
    }

    readonly property color surface0: dark ? "#14100D" : "#EEE8E2"
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
    readonly property color accentPressed: dark ? "#E85A38" : "#8E2A0C"
    readonly property color onAccent: dark ? "#1C1006" : "#FFFFFF"
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
    // Touch targets: 48 dp is the platform's minimum; 56 for the main ones.
    readonly property int touch: dp(48)
    readonly property int touchLarge: dp(56)
    readonly property int rowHeight: dp(64)
    readonly property int barHeight: dp(56)

    // Fling physics (ScrollList): Qt's defaults (2500, 1500) cut a fast
    // swipe short and stop it hard. These felt native on a Pixel 7; still
    // tunable in Diagnostics.
    property real flickMaxVelocityBase: 12000
    property real flickDecelerationBase: 2000
    readonly property real flickMaxVelocity: flickMaxVelocityBase * scale
    readonly property real flickDeceleration: flickDecelerationBase * scale

    readonly property int durationFast: 150
    readonly property int durationPage: 320

    function formatTime(ms) {
        if (!(ms > 0))
            return "0:00"
        const total = Math.floor(ms / 1000)
        const seconds = total % 60
        return Math.floor(total / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }
}
