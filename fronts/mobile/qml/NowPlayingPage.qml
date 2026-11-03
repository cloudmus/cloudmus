import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The track that plays, big: cover, title, position and the transport.
// Landscape (a head unit) puts the cover left of the controls, portrait (a
// phone) above them. Closed with the arrow, Back, or a swipe down.
Page {
    id: page

    property QtObject backend
    property QtObject platform
    readonly property QtObject nowPlaying: backend.nowPlaying
    readonly property bool wide: width > height * 1.1
    signal closeRequested()
    signal trackMenuRequested()

    background: AuraBackground {
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
            Text {
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

    // A swipe down closes the page, following the finger until let go.
    DragHandler {
        id: swipe
        target: null
        xAxis.enabled: false
        onActiveChanged: {
            if (!active && translation.y > page.height * 0.18)
                page.closeRequested()
        }
    }
    transform: Translate { y: swipe.active ? Math.max(0, swipe.translation.y) : 0 }

    TrackPanel {
        anchors.fill: parent
        anchors.margins: Theme.padding * 1.5
        anchors.topMargin: Theme.padding / 2
        nowPlaying: page.nowPlaying
        sideBySide: page.wide
        overAura: true
        showMenu: page.backend.trackScreen !== null
        onMenuRequested: {
            page.backend.trackScreen.showPlaying()
            page.trackMenuRequested()
        }
    }
}
