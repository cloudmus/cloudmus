import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Effects
import QtQuick.Layouts

// The track that plays, big: cover, title, position and the transport. With
// room to the side (`sideBySide`, or `progress` on the way there) the cover is
// left of the rest.
Item {
    id: panel

    property QtObject nowPlaying
    property bool sideBySide: false
    // Over the generated background, which is dark in either theme.
    property bool overAura: false
    property bool showMenu: false
    // Swiping sideways goes to the next or the previous track. Off under what's
    // over the panel.
    property bool swipeEnabled: true
    // Where a swipe would go and what comes in: ViewModel::MainScreen.
    property QtObject neighbors
    // A finger going sideways, or the cover not back from that. It stays that way
    // until it's lifted: nothing else is to take it up or down meanwhile.
    readonly property bool swipedSideways: trackSwipe.active || _swiping
    // What it plays from, shown above the title with the cover beside it: where
    // the list that says so is folded away. Empty: nothing is shown.
    property string playlistTitle
    signal menuRequested()
    // How far the tracks after this one have got (PreloadCells).
    property var preloadFractions: []
    property color inkColor: overAura ? "#FFFFFF" : Theme.ink
    property color inkSecondaryColor: overAura ? "#D9FFFFFF" : Theme.inkSecondary
    readonly property string seed: nowPlaying && nowPlaying.backgroundSeed ? nowPlaying.backgroundSeed : ""

    // 0 the cover above the rest .. 1 beside it: animating this moves them from
    // one place to the other, which a layout can't.
    property real progress: sideBySide ? 1 : 0
    // Of the width, what the cover may take when beside the rest.
    property real sideCoverShare: 0.48
    // Of the height, likewise.
    property real sideCoverHeightShare: 1

    readonly property real _gap: Theme.padding
    readonly property real _detailsHeight: details.implicitHeight
    // Below the title: the same in every layout.
    readonly property real _belowTitle: seekBar.implicitHeight + transport.implicitHeight + details.spacing * 2
    readonly property real _coverOnTop: height - _belowTitle - titleRow.implicitHeight - _gap
    // Too low for a cover above the title (a phone on its side): beside it,
    // with the position and the transport under both. Set from outside: worked
    // out here from the sizes it changes, it would flip back and forth for good.
    property bool compact: false
    readonly property real _compactRow: Math.max(0, height - _belowTitle)
    readonly property real _aboveSide: compact ? Math.min(_compactRow, width * 0.42)
                                               : Math.max(0, Math.min(width, _coverOnTop))
    // 1: shuffle and repeat are shown somewhere else; the title takes their room.
    property real modesElsewhere: 0
    // As large as there's room for, leaving the rest the width it needs.
    readonly property real _besideSide: Math.max(Theme.dp(120),
        Math.min(height * sideCoverHeightShare, width * sideCoverShare, width - _gap * 2 - details.implicitWidth))
    readonly property real _besideDetailsX: _besideSide + _gap * 2

    readonly property int _textDirection: nowPlaying && nowPlaying.trackDirection === -1 ? -1 : 1

    function _mix(from, to) { return from + (to - from) * progress }
    function _titleSizeAt(at) { return Theme.fontTitle + Theme.dp(3) + (Theme.dp(28) - Theme.fontTitle - Theme.dp(3)) * at }
    // Not on the way from one layout to the other.
    readonly property bool _atRest: progress <= 0 || progress >= 1
    // The side of the picture the cover keeps, whatever it's shown at; 0: as shown,
    // for a panel that stays in one layout. Not worked out from this panel's size,
    // which changes on the way.
    property real coverMaxSide: 0

    // The picture stays one size, scaled to what's asked for: its mask's texture
    // isn't made anew each frame, nor the picture decoded again at the other end.
    Item {
        id: coverBox
        objectName: "trackCover"
        x: panel._mix(panel.compact ? 0 : (panel.width - panel._aboveSide) / 2, 0)
        y: panel._mix(panel.compact ? (panel._compactRow - panel._aboveSide) / 2
                                    : (panel.height - panel._detailsHeight - panel._gap - panel._aboveSide) / 2,
                      (panel.height - panel._besideSide) / 2)
        width: panel._mix(panel._aboveSide, panel._besideSide)
        height: width
        readonly property alias changing: cover.changing
        readonly property alias enteringScale: cover.enteringScale
        readonly property alias shownSource: cover.shownSource
        readonly property alias shownSide: cover.shownSide
        // A slight breath while playing.
        scale: panel.nowPlaying && panel.nowPlaying.playing ? 1.0 : 0.92
        Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }

        Cover {
            id: cover
            objectName: "trackCoverPicture"
            width: panel.coverMaxSide > 0 ? panel.coverMaxSide : coverBox.width
            height: width
            transformOrigin: Item.TopLeft
            scale: coverBox.width / width
            source: panel.nowPlaying ? panel.nowPlaying.coverUrl : ""
            seed: panel.seed
            // The same on screen at any scale. In the picture itself, without a
            // GPU, it can't follow.
            radius: _masked && scale > 0 ? Theme.coverRadius / scale : Theme.coverRadius
            liveCorners: true
            // Under a swipe the new one has come in already, as the neighbor.
            zoomOnChange: !panel._swiping
            // Going off, smaller, from its center: its own scale is from its corner.
            readonly property real _going: panel._swipeScale(panel._oldFar)
            x: panel._swipeX + coverBox.width * (1 - _going) / 2
            y: coverBox.width * (1 - _going) / 2
            opacity: Math.max(0, 1 - panel._oldFar / 0.85) * (1 - panel._landed)
            layer.enabled: panel._swipeLayers
            layer.textureSize: panel._swipeTexture
            layer.effect: MultiEffect {
                autoPaddingEnabled: true
                blurEnabled: true
                blurMax: 32
                blur: Math.min(1, panel._oldFar * 1.3)
            }
            transform: Scale { xScale: cover._going; yScale: cover._going }
        }
        // The track a swipe goes to, looking out from the side: only so far while
        // the finger is on the old one, all the way once that's let go of.
        Cover {
            id: neighbor
            objectName: "trackCoverNeighbor"
            width: coverBox.width
            height: width
            visible: panel._swipeTo !== 0
            source: panel._handingOver || panel._landed > 0 ? panel._frozenSource
                : !panel.neighbors ? "" : panel._swipeTo > 0 ? panel.neighbors.nextCoverUrl
                : panel._swipeTo < 0 ? panel.neighbors.previousCoverUrl : ""
            seed: panel._handingOver || panel._landed > 0 ? panel._frozenSeed
                : !panel.neighbors ? "" : panel._swipeTo > 0 ? panel.neighbors.nextSeed
                : panel._swipeTo < 0 ? panel.neighbors.previousSeed : ""
            x: panel._swipeTo * coverBox.width * 0.85 * (1 - panel._newNear)
            opacity: Math.min(1, panel._newNear * 1.4)
            scale: panel._swipeScale(1 - panel._newNear)
            layer.enabled: panel._swipeLayers
            layer.textureSize: panel._swipeTexture
            layer.effect: MultiEffect {
                autoPaddingEnabled: true
                blurEnabled: true
                blurMax: 32
                blur: Math.min(1, (1 - panel._newNear) * 1.3)
            }
        }
    }
    // A swipe sideways: the cover goes with the finger, dissolving, shrinking a
    // little and blurring the further it gets, never all that far. Its neighbor
    // looks out the same way, lazily, only part of the way: let go of near, the one
    // goes back and the other hides again; thrown, the neighbor takes its place.
    // With nowhere to go the cover only gives a little.
    property real _swipeX: 0
    // Where it goes: 1 the next track, -1 the previous one, 0 nowhere.
    property int _swipeTo: 0
    // Thrown: how far the neighbor has got, from where it was to its place.
    property real _landed: 0
    // Over to the neighbor, and waiting for the panel's own cover to catch up.
    property bool _handingOver: false
    // Fixed once thrown: "next" is another track as soon as the track changes, and
    // it would blink through that one's cover.
    property string _frozenSource
    property string _frozenSeed
    property string _sourceBefore
    readonly property bool _swiping: _swipeTo !== 0 || _swipeX !== 0 || _handingOver
    // How far the old one is: 0 in its place, 1 as far as it gets.
    readonly property real _swipeReach: coverBox.width * 0.7
    readonly property real _oldFar: _swipeReach > 0 ? Math.min(1, Math.abs(_swipeX) / _swipeReach) : 0
    // How near the new one is: part of the way under a finger, then all of it.
    readonly property real _newNear: (_swipeTo !== 0 ? _oldFar * 0.55 : 0) * (1 - _landed) + _landed
    function _swipeScale(far) { return 1 - far * 0.18 }
    readonly property bool _swipeLayers: _swipeTo !== 0 && _canBlur
    // Half the pixels are plenty for what's dissolving, at this cost per frame.
    readonly property size _swipeTexture: Qt.size(coverBox.width * Screen.devicePixelRatio / 2,
                                                  coverBox.width * Screen.devicePixelRatio / 2)
    readonly property bool _canBlur: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property real _swipeVelocity: Theme.dp(500)
    function _canSwipe(to) {
        if (!neighbors)
            return false
        return to > 0 ? neighbors.canGoNext === true : neighbors.canGoPrevious === true
    }
    // Follows the finger at first, then less and less: it can't be taken far.
    function _swipePosition(dx) {
        const reach = _swipeReach
        return Math.sign(dx) * reach * (1 - Math.exp(-Math.abs(dx) / reach))
    }
    function _swipeBack() {
        swipeBack.to = 0
        swipeBack.restart()
    }
    function _throw() {
        _frozenSource = neighbor.source
        _frozenSeed = neighbor.seed
        _sourceBefore = cover.source
        swipeThrow.restart()
    }
    DragHandler {
        id: trackSwipe
        objectName: "trackSwipe"
        enabled: panel.swipeEnabled && !panel._handingOver && panel._landed === 0
        target: null
        yAxis.enabled: false
        property real velocity: 0
        property double movedAt: 0
        onActiveTranslationChanged: {
            if (!active || activeTranslation.x === 0)
                return
            velocity = centroid.velocity.x
            movedAt = Date.now()
            swipeBack.stop()
            const to = activeTranslation.x < 0 ? 1 : -1
            const can = panel._canSwipe(to)
            panel._swipeTo = can ? to : 0
            panel._swipeX = can ? panel._swipePosition(activeTranslation.x) : activeTranslation.x * 0.15
        }
        onActiveChanged: {
            if (active)
                return
            // A finger that stopped before lifting hasn't thrown anything.
            const thrown = Date.now() - movedAt < 100 ? velocity : 0
            const thrownThatWay = -thrown * panel._swipeTo > panel._swipeVelocity
            if (panel._swipeTo !== 0 && (panel._oldFar > 0.45 || thrownThatWay))
                panel._throw()
            else
                panel._swipeBack()
        }
    }
    NumberAnimation {
        id: swipeBack
        target: panel
        property: "_swipeX"
        duration: Theme.durationPage * 0.7
        easing.type: Easing.OutCubic
        onFinished: panel._swipeTo = 0
    }
    NumberAnimation {
        id: swipeThrow
        target: panel
        property: "_landed"
        from: 0
        to: 1
        duration: Theme.durationPage
        easing.type: Easing.OutCubic
        onFinished: {
            // Only now: what a new track sets off doesn't get in the way of this.
            panel._handingOver = true
            caughtUpLate.restart()
            if (panel._swipeTo > 0)
                panel.nowPlaying.next()
            else
                panel.nowPlaying.previous()
        }
    }
    // The panel's own cover has what the neighbor shows: the one takes over from
    // the other, with nothing to see.
    function _caughtUp() {
        if (!_handingOver)
            return
        _handingOver = false
        caughtUpLate.stop()
        _swipeTo = 0
        _swipeX = 0
        _landed = 0
    }
    function _catchUpIfThere() {
        if (!_handingOver || cover.changing || cover.shownSource !== cover._wanted)
            return
        // What's known is what's to be there. Not known beforehand (a station's
        // next one, a cover yet to come): any other than the one before, of that track.
        const there = _frozenSource !== "" ? cover.source === _frozenSource
                                           : cover.source !== _sourceBefore && cover.seed === _frozenSeed
        if (there)
            _caughtUp()
    }
    Timer {
        id: caughtUpLate
        interval: 1500
        onTriggered: panel._caughtUp()
    }
    Connections {
        target: cover
        function onShownSourceChanged() { panel._catchUpIfThere() }
        function onChangingChanged() { panel._catchUpIfThere() }
        function onSourceChanged() { panel._catchUpIfThere() }
    }

    // Beside the layout, not in it: fading in, it moves nothing.
    RowLayout {
        id: playlistLine
        objectName: "trackPlaylist"
        x: details.x
        y: details.y - height - Theme.dp(6)
        visible: panel.progress > 0 && panel.playlistTitle !== ""
        opacity: panel.progress
        spacing: Theme.dp(8)
        IconImage {
            source: "qrc:/icons/playlist_play.svg"
            sourceSize.width: Theme.dp(22)
            sourceSize.height: Theme.dp(22)
            color: panel.inkSecondaryColor
        }
        ShadowText {
            objectName: "trackPlaylistTitle"
            Layout.fillWidth: true
            shadowed: panel.overAura
            text: panel.playlistTitle
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: panel.inkSecondaryColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
        }
    }

    // Only when it has come to rest: laid out anew each frame, with the shadow
    // around it, it'd cost as much as the title would (see there).
    Binding {
        target: playlistLine
        property: "width"
        value: details.width
        when: panel._atRest
        restoreMode: Binding.RestoreNone
    }

    ColumnLayout {
        id: details
        objectName: "trackDetails"
        x: panel._mix(0, panel._besideDetailsX)
        y: panel._mix(panel.height - panel._detailsHeight, (panel.height - panel._detailsHeight) / 2)
        width: panel._mix(panel.width, panel.width - panel._besideDetailsX)
        spacing: Theme.dp(8)

        Item {
            Layout.fillWidth: true
            Layout.leftMargin: panel.compact ? (panel._aboveSide + panel._gap) * (1 - panel.progress) : 0
            Layout.preferredHeight: panel.compact ? panel._mix(panel._compactRow, titleRow.implicitHeight)
                                                  : titleRow.implicitHeight

            RowLayout {
                id: titleRow
                anchors.fill: parent
                // The text is always laid out at its largest and scaled to what's
                // wanted: laid out anew each frame, with the shadows and masks
                // around it, it would cost the most of anything here, and laid out
                // anew at the end of the way, it would jump. Only its width follows,
                // at the end, for a line that fits now or doesn't any more.
                Item {
                    id: titleSlot
                    Layout.fillWidth: true
                    implicitHeight: titleBlock.implicitHeight * titleBlock.scale
                    TrackTitleBlock {
                        id: titleBlock
                        objectName: "trackTitle"
                        y: (titleSlot.height - titleSlot.implicitHeight) / 2
                        transformOrigin: Item.TopLeft
                        scale: panel._titleSizeAt(panel.progress) / panel._titleSizeAt(1)
                        title: panel.nowPlaying ? panel.nowPlaying.title : ""
                        artist: panel.nowPlaying ? panel.nowPlaying.artist : ""
                        direction: panel._textDirection
                        titleColor: panel.inkColor
                        artistColor: panel.inkSecondaryColor
                        titleSize: Math.round(panel._titleSizeAt(1))
                        // Theme.fontBody when the panel has the cover above the text.
                        artistSize: Math.round(Theme.fontBody * panel._titleSizeAt(1) / panel._titleSizeAt(0))
                    }
                    Binding {
                        target: titleBlock
                        property: "width"
                        value: titleSlot.width / titleBlock.scale
                        when: panel._atRest
                        restoreMode: Binding.RestoreNone
                    }
                }
                // What a queue does: not on the dev tracks, which don't have them.
                IconButton {
                    objectName: "shuffleButton"
                    Layout.preferredWidth: Theme.touch * (1 - panel.modesElsewhere)
                    opacity: 1 - panel.modesElsewhere
                    visible: panel.modesElsewhere < 1 && panel.nowPlaying && panel.nowPlaying.repeat !== undefined
                    enabled: panel.nowPlaying && panel.nowPlaying.radio !== true
                    iconName: "shuffle"
                    color: panel.nowPlaying && panel.nowPlaying.shuffle === true ? Theme.accent : panel.inkColor
                    onClicked: panel.nowPlaying.toggleShuffle()
                }
                IconButton {
                    objectName: "repeatButton"
                    Layout.preferredWidth: Theme.touch * (1 - panel.modesElsewhere)
                    opacity: 1 - panel.modesElsewhere
                    visible: panel.modesElsewhere < 1 && panel.nowPlaying && panel.nowPlaying.repeat !== undefined
                    iconName: panel.nowPlaying && panel.nowPlaying.repeat === 2 ? "repeat_one" : "repeat"
                    color: panel.nowPlaying && panel.nowPlaying.repeat > 0 ? Theme.accent : panel.inkColor
                    onClicked: panel.nowPlaying.cycleRepeat()
                }
                IconButton {
                    objectName: "trackMenuButton"
                    visible: panel.showMenu && panel.nowPlaying && panel.nowPlaying.hasTrack === true
                    iconName: "more_vert"
                    color: panel.inkColor
                    onClicked: panel.menuRequested()
                }
            }
        }

        SeekBar {
            id: seekBar
            Layout.fillWidth: true
            positionMs: panel.nowPlaying ? panel.nowPlaying.positionMs : 0
            durationMs: panel.nowPlaying ? panel.nowPlaying.durationMs : 0
            bufferedMs: panel.nowPlaying && panel.nowPlaying.bufferedMs !== undefined ? panel.nowPlaying.bufferedMs : -1
            inkColor: panel.inkColor
            inkSecondaryColor: panel.inkSecondaryColor
            overAura: panel.overAura
            preloadFractions: panel.preloadFractions
            troughColor: panel.overAura ? "#40FFFFFF" : Theme.surface400
            onSeekRequested: (positionMs) => panel.nowPlaying.seek(positionMs)
        }

        RowLayout {
            id: transport
            Layout.alignment: Qt.AlignHCenter
            spacing: panel._mix(panel.compact ? Theme.dp(2) : Theme.dp(10), Theme.dp(10))
            IconButton {
                visible: panel.nowPlaying && panel.nowPlaying.dislikeSupported === true
                iconName: panel.nowPlaying && panel.nowPlaying.disliked ? "heart_off" : "heart_off_outline"
                color: panel.inkColor
                onClicked: panel.nowPlaying.setDisliked(!panel.nowPlaying.disliked)
            }
            IconButton {
                iconName: "skip_previous"
                color: panel.inkColor
                size: Theme.touchLarge
                iconSize: Theme.dp(34)
                onClicked: panel.nowPlaying.previous()
            }
            IconButton {
                iconName: panel.nowPlaying && panel.nowPlaying.playing ? "pause" : "play_arrow"
                filled: true
                size: Math.round(panel._mix(panel.compact ? Theme.dp(60) : Theme.dp(72), Theme.dp(72)))
                iconSize: Math.round(size * 0.55)
                onClicked: panel.nowPlaying.togglePause()

                Spinner {
                    anchors.fill: parent
                    anchors.margins: -Theme.dp(4)
                    running: panel.nowPlaying && panel.nowPlaying.loading === true
                    // Around the accent button, in what stands out from it.
                    color: panel.inkColor
                }
            }
            IconButton {
                iconName: "skip_next"
                color: panel.inkColor
                size: Theme.touchLarge
                iconSize: Theme.dp(34)
                onClicked: panel.nowPlaying.next()
            }
            IconButton {
                visible: panel.nowPlaying && panel.nowPlaying.likeSupported === true
                iconName: panel.nowPlaying && panel.nowPlaying.liked ? "favorite" : "favorite_border"
                color: panel.nowPlaying && panel.nowPlaying.liked ? Theme.accent : panel.inkColor
                onClicked: panel.nowPlaying.setLiked(!panel.nowPlaying.liked)
            }
        }
    }
}
