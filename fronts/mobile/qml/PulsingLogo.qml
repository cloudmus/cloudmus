import QtQuick

// The logo, moving with the music: it swells on a hit and settles back, and
// grows a little with the loudness. As the desktop's Ui::PulsingLogo. A tap
// plays the logo's sound (pulse.playLogoSound()), and it swells hard on each of
// the sound's strikes.
//
// It moves only in step(), called by whatever animates the page around it
// (CloudsBackdrop's stepped()): one clock for both, the logo's swell in step
// with the clouds' rush.
Item {
    id: logo

    // Ui::Backend's pulse (ViewModel::PulseScreen); null: the logo stays still.
    property QtObject pulse: null
    property int side: Theme.dp(72)

    readonly property real _beatSwell: 0.18 // growth at a full-strength beat
    readonly property real _levelSwell: 0.05 // growth at full loudness
    readonly property real _settleMs: 380
    // The logo's own sound: it's the logo's, so it swells harder, and settles
    // slower, as the handpan rings.
    readonly property real _strikeSwell: 0.32
    readonly property real _soundLevelSwell: 0.08
    readonly property real _strikeSettleMs: 480
    // The last beat and the last strike: how strong, and how long ago.
    property real _beat: 0
    property real _beatAgeMs: _settleMs
    property real _strike: 0
    property real _strikeAgeMs: _strikeSettleMs
    property real _scale: 1

    // The room for the largest swell is part of its size: what's around it
    // mustn't move as it grows.
    implicitWidth: Math.ceil(side * (1 + Math.max(_beatSwell + _levelSwell, _strikeSwell + _soundLevelSwell)))
    implicitHeight: implicitWidth

    Connections {
        target: logo.pulse
        ignoreUnknownSignals: true
        function onBeat(strength) {
            logo._beat = strength
            logo._beatAgeMs = 0
        }
        function onLogoHit(strength) {
            logo._strike = strength
            logo._strikeAgeMs = 0
        }
    }

    // Out of a swell to none, easing out (cubic), as the desktop's.
    function _settling(strength, ageMs, settleMs) {
        const left = 1 - Math.min(1, ageMs / settleMs)
        return strength * left * left * left
    }

    function step(dtMs) {
        _beatAgeMs = Math.min(_settleMs, _beatAgeMs + dtMs)
        _strikeAgeMs = Math.min(_strikeSettleMs, _strikeAgeMs + dtMs)
        const level = pulse && pulse.level !== undefined ? pulse.level : 0
        const soundLevel = pulse && pulse.logoLevel !== undefined ? pulse.logoLevel : 0
        const scale = 1 + _beatSwell * _settling(_beat, _beatAgeMs, _settleMs) + _levelSwell * level
            + _strikeSwell * _settling(_strike, _strikeAgeMs, _strikeSettleMs) + _soundLevelSwell * soundLevel
        // Unchanged, nothing to draw.
        if (Math.abs(scale - _scale) > 0.0005)
            _scale = scale
    }

    Image {
        objectName: "pulsingLogoImage"
        anchors.centerIn: parent
        width: logo.side
        height: logo.side
        // Sharp at its largest.
        sourceSize.width: Math.ceil(logo.side * 1.45)
        sourceSize.height: Math.ceil(logo.side * 1.45)
        source: "qrc:/icons/logo.svg"
        smooth: true
        scale: logo._scale
    }

    TapHandler {
        objectName: "logoTap"
        enabled: logo.pulse !== null
        onTapped: logo.pulse.playLogoSound()
    }
}
