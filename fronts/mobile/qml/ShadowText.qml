import QtQuick
import QtQuick.Effects
import QtQuick.Window

// Text over the generated background, with a soft shadow that follows its
// glyphs: just enough to lift it off whatever the art has there.
Text {
    property bool shadowed: true

    // Without a GPU (software rendering) an effect shows nothing at all, text
    // included: then it goes without.
    layer.enabled: shadowed && GraphicsInfo.api !== GraphicsInfo.Software && visible && text !== ""
    layer.effect: MultiEffect {
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowColor: "#000000"
        shadowOpacity: 0.5
        shadowBlur: 0.35
        shadowVerticalOffset: Theme.dp(1)
    }
}
