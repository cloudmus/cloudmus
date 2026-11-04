import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The track that plays, big: cover, title, position and the transport.
// Landscape (a head unit) puts the cover left of the controls, portrait (a
// phone) above them. Closed with the arrow, Back, or a swipe down (Main.qml moves it).
Page {
    id: page

    property QtObject backend
    property QtObject platform
    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property bool wide: width > height * 1.1
    property bool swipeEnabled: true
    signal closeRequested()
    signal trackMenuRequested()
    // A finger pushing it down, by so much; let go, at that speed.
    signal pushed(real distance)
    signal pushEnded(real velocity)

    background: AuraBackground {
        objectName: "nowPlayingAura"
        seed: page.nowPlaying && page.nowPlaying.backgroundSeed ? page.nowPlaying.backgroundSeed
            : page.backend.playlist ? page.backend.playlist.title : ""
    }

    header: Item {
        implicitHeight: Theme.barHeight
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(4)
            anchors.rightMargin: Theme.padding
            IconButton {
                iconName: "expand_more"
                iconSize: Theme.dp(32)
                color: "#FFFFFF"
                onClicked: page.closeRequested()
            }
            ShadowText {
                Layout.fillWidth: true
                text: qsTr("Now playing")
                horizontalAlignment: Text.AlignHCenter
                color: "#E6FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Theme.dp(1)
            }
            // Balances the arrow, keeping the title centered. No "stop and
            // close" here: next to "collapse" it read as closing the page,
            // and in a car a slip would cut the music (it's in the
            // notification and in Settings).
            Item { implicitWidth: Theme.touch }
        }
    }

    // A swipe down closes the page, following the finger until let go. As the
    // finger lifts the handler zeroes its translation first, then says it's over:
    // that zero isn't a move, and the speed is the last one a move had.
    DragHandler {
        id: swipe
        // One way for a whole gesture, whichever it began as.
        enabled: page.swipeEnabled && !trackPanel.swipedSideways
        target: null
        xAxis.enabled: false
        property real velocity: 0
        property double movedAt: 0
        onActiveTranslationChanged: {
            if (!active || activeTranslation.y === 0)
                return
            velocity = centroid.velocity.y
            movedAt = Date.now()
            page.pushed(Math.max(0, activeTranslation.y))
        }
        // A finger that stopped before lifting hasn't thrown anything.
        onActiveChanged: if (!active) page.pushEnded(Date.now() - movedAt < 100 ? velocity : 0)
    }

    TrackPanel {
        id: trackPanel
        anchors.fill: parent
        anchors.margins: Theme.padding * 1.5
        anchors.topMargin: Theme.padding / 2
        anchors.bottomMargin: Theme.padding * 1.5 + Theme.insetBottom
        nowPlaying: page.nowPlaying
        sideBySide: page.wide
        overAura: true
        swipeEnabled: page.swipeEnabled && !swipe.active
        neighbors: page.backend.mainScreen
        preloadFractions: page.backend.mainScreen ? page.backend.mainScreen.preloadFractions : []
        showMenu: page.backend.trackScreen !== null
        onMenuRequested: {
            page.backend.trackScreen.showPlaying()
            page.trackMenuRequested()
        }
    }
}
