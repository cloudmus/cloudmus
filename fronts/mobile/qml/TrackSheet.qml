import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Effects
import QtQuick.Layouts

// One track in detail, and what can be done with it, from any list: a sheet
// rising from the bottom on a phone, a panel from the right on a head unit,
// where a sheet wouldn't fit under its short screen.
Popup {
    id: sheet

    property QtObject backend
    // What it's glass over.
    property Item behind
    readonly property QtObject track: backend ? backend.trackScreen : null
    readonly property bool wide: parent && parent.width > parent.height * 1.1
    // The list of playlists, in place of the actions.
    property bool choosingPlaylist: false

    parent: Overlay.overlay
    modal: true
    dim: true
    padding: 0
    bottomPadding: Theme.insetBottom
    topPadding: wide ? Theme.insetTop : 0
    width: wide ? Math.min(parent.width * 0.5, Theme.dp(460)) : parent.width
    height: wide ? parent.height : Math.min(parent.height * 0.86, content.implicitHeight + Theme.padding * 2 + Theme.insetBottom)
    // Slid out by this share of its size. Not x and y themselves: an animation
    // would replace their bindings, and the sheet's height comes in later.
    property real slide: 1
    x: wide ? parent.width - width * (1 - slide) : 0
    y: wide ? 0 : parent.height - height * (1 - slide)
    onClosed: choosingPlaylist = false

    enter: Transition {
        NumberAnimation {
            property: "slide"
            from: 1
            to: 0
            duration: Theme.durationPage
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "slide"
            to: 1
            duration: Theme.durationPage
            easing.type: Easing.InCubic
        }
    }

    // What's behind goes black, and fades in and out with the sheet.
    Overlay.modal: Rectangle {
        color: "#80000000"
        Behavior on opacity { NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic } }
    }

    // Frosted glass over what's behind, as the desktop's window is. Without a GPU
    // an effect shows nothing at all, the sheet included: then it's plain.
    readonly property bool _glass: behind !== null && background.hasGpu === true
    onAboutToShow: if (_glass) snapshot.scheduleUpdate()

    // What's behind, blurred anew as it changes while the sheet is up
    // (Theme.dynamicBlur, on by default). With that off, as it is when the sheet
    // comes, blurred once and kept: moving the sheet only moves that, and no list
    // scrolling or bars going under it make it blur every frame. In a quarter of
    // the pixels each way: a blur has no use for more, and from fewer it reaches
    // further.
    ShaderEffectSource {
        id: snapshot
        objectName: "sheetSnapshot"
        visible: false
        sourceItem: sheet._glass ? sheet.behind : null
        live: Theme.dynamicBlur && sheet.visible
        width: sheet.behind ? sheet.behind.width : 0
        height: sheet.behind ? sheet.behind.height : 0
        textureSize: Qt.size(width / 4, height / 4)
    }

    // Rounded where it meets what's behind, with a shadow that follows that edge.
    background: Item {
        // From an item: a Popup isn't one, and this can't be asked there.
        // Not known until the window is up: not to be taken for one, for an effect
        // to be made and taken away again.
        readonly property bool hasGpu: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown

        Rectangle {
            id: sheetShape
            anchors.fill: parent
            color: Theme.surface100
            topLeftRadius: Theme.dp(20)
            topRightRadius: sheet.wide ? 0 : topLeftRadius
            bottomLeftRadius: sheet.wide ? topLeftRadius : 0
            layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
            layer.effect: MultiEffect {
                autoPaddingEnabled: true
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.55
                shadowBlur: 0.7
            }
        }
        // Within that shape: drawn as a whole and cut to it.
        Item {
            id: glass
            anchors.fill: parent
            visible: sheet._glass
            clip: true
            layer.enabled: sheet._glass
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: glassShape
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1
            }
            MultiEffect {
                objectName: "sheetGlass"
                // What's under it, wherever it has got to.
                x: -sheet.x
                y: -sheet.y
                width: snapshot.width
                height: snapshot.height
                source: snapshot
                autoPaddingEnabled: false
                blurEnabled: true
                blur: 1
                blurMax: 64
                saturation: 0.2
            }
            Rectangle {
                anchors.fill: parent
                color: Theme.surface100
                opacity: Theme.dark ? 0.88 : 0.84
            }
        }
        // Across a head unit's: a grip at the edge it can be pushed back over.
        Rectangle {
            objectName: "sheetHandle"
            visible: sheet.wide
            x: Theme.dp(8)
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.dp(4)
            height: Theme.dp(40)
            radius: width / 2
            color: Theme.inkTertiary
            opacity: 0.5
        }
        // As the sheet's shape, for the mask: drawn only into a texture.
        Rectangle {
            id: glassShape
            anchors.fill: parent
            visible: false
            layer.enabled: sheet._glass
            topLeftRadius: sheetShape.topLeftRadius
            topRightRadius: sheetShape.topRightRadius
            bottomLeftRadius: sheetShape.bottomLeftRadius
        }
    }

    // A swipe towards the edge it came from takes it away, following the finger.
    // Far enough or thrown, it goes on; otherwise it comes back.
    readonly property real _dismissShare: 0.25
    readonly property real _dismissVelocity: Theme.dp(600)
    function _letGo(velocity) {
        if (slide > _dismissShare || velocity > _dismissVelocity) {
            close()
        } else {
            slideBack.to = 0
            slideBack.restart()
        }
    }
    NumberAnimation {
        id: slideBack
        target: sheet
        property: "slide"
        duration: Theme.durationFast
        easing.type: Easing.OutCubic
    }

    component Action: ItemDelegate {
        id: action
        property string iconName
        property color tint: Theme.ink
        property bool keepsSheetOpen: false
        Layout.fillWidth: true
        implicitHeight: Theme.touch + Theme.dp(4)
        background: Rectangle { color: action.down ? Theme.surface300 : "transparent" }
        onClicked: if (!keepsSheetOpen) sheet.close()
        contentItem: RowLayout {
            spacing: Theme.dp(16)
            IconImage {
                source: "qrc:/icons/" + action.iconName + ".svg"
                sourceSize.width: Theme.dp(22)
                sourceSize.height: Theme.dp(22)
                color: action.tint
            }
            Text {
                Layout.fillWidth: true
                text: action.text
                color: Theme.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
        }
    }

    component Fact: RowLayout {
        property alias label: name.text
        property alias value: fact.text
        visible: fact.text !== ""
        Layout.fillWidth: true
        Text {
            id: name
            color: Theme.inkTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
        }
        Text {
            id: fact
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            color: Theme.inkSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }

    contentItem: ScrollArea {
        id: scroller
        // Down a phone: scrolled to the top, what's left of the finger's way moves
        // the sheet. Watched rather than grabbed, as the scrolling may have the
        // finger, from a layer over it all: a row would keep its press from what's
        // behind it, and this one has to see that too.
        Item {
            parent: scroller
            anchors.fill: parent
            z: 1

        PointHandler {
            id: pull
            enabled: !sheet.wide
            readonly property real threshold: Theme.dp(10)
            property real from: 0
            property bool following: false
            property real velocity: 0
            property double movedAt: 0
            onPointChanged: {
                if (!active)
                    return
                const y = point.scenePosition.y
                const distance = y - from
                if (!scroller.atYBeginning || distance <= 0) {
                    from = y
                    if (following)
                        sheet.slide = 0
                    return
                }
                velocity = point.velocity.y
                movedAt = Date.now()
                if (!following) {
                    if (distance < threshold)
                        return
                    following = true
                    from = y
                    slideBack.stop()
                }
                sheet.slide = Math.min(1, (y - from) / sheet.height)
            }
            onActiveChanged: {
                if (active) {
                    from = point.scenePosition.y
                } else if (following) {
                    following = false
                    // A finger that stopped before lifting hasn't thrown anything.
                    sheet._letGo(Date.now() - movedAt < 100 ? velocity : 0)
                }
            }
        }
        }
        // Across a head unit's: nothing else moves that way.
        DragHandler {
            enabled: sheet.wide
            target: null
            yAxis.enabled: false
            property real velocity: 0
            property double movedAt: 0
            onActiveTranslationChanged: {
                if (!active || activeTranslation.x === 0)
                    return
                velocity = centroid.velocity.x
                movedAt = Date.now()
                slideBack.stop()
                sheet.slide = Math.max(0, Math.min(1, activeTranslation.x / sheet.width))
            }
            onActiveChanged: if (!active) sheet._letGo(Date.now() - movedAt < 100 ? velocity : 0)
        }

        contentWidth: width
        contentHeight: content.implicitHeight + Theme.padding * 2
        clip: true
        fadeColor: Theme.surface100
        // Glass isn't one color to fade into: the rows themselves go see-through,
        // where there's more beyond that edge.
        fadeShown: !sheet._glass
        layer.enabled: sheet._glass
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            maskEnabled: true
            maskSource: dissolve
            // Together: the mask's own alpha, from 0 to 1. A threshold of 0 lets
            // all of it through.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
        Item {
            id: dissolve
            parent: scroller
            anchors.fill: parent
            visible: false
            layer.enabled: sheet._glass

            readonly property real depth: Math.min(Theme.dp(28), height / 2)
            property real topDepth: scroller.contentY > scroller.originY + 1 ? depth : 0
            property real bottomDepth: scroller.contentY + scroller.height
                                       < scroller.originY + scroller.contentHeight - 1 ? depth : 0
            Behavior on topDepth { NumberAnimation { duration: 180 } }
            Behavior on bottomDepth { NumberAnimation { duration: 180 } }

            Rectangle {
                width: parent.width
                height: dissolve.topDepth
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00FFFFFF" }
                    GradientStop { position: 1.0; color: "#FFFFFFFF" }
                }
            }
            Rectangle {
                y: dissolve.topDepth
                width: parent.width
                height: parent.height - dissolve.topDepth - dissolve.bottomDepth
                color: "#FFFFFFFF"
            }
            Rectangle {
                y: parent.height - height
                width: parent.width
                height: dissolve.bottomDepth
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#FFFFFFFF" }
                    GradientStop { position: 1.0; color: "#00FFFFFF" }
                }
            }
        }

        ColumnLayout {
            id: content
            // Under a swipe a row pressed at its start isn't pressed any more.
            enabled: !pull.following
            x: Theme.padding
            y: Theme.padding
            width: parent.width - Theme.padding * 2
            spacing: Theme.dp(4)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.dp(14)
                Cover {
                    Layout.preferredWidth: Theme.dp(96)
                    Layout.preferredHeight: Theme.dp(96)
                    source: sheet.track ? sheet.track.coverUrl : ""
                    seed: sheet.track ? sheet.track.seed : ""
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)
                    Text {
                        Layout.fillWidth: true
                        text: sheet.track ? sheet.track.title : ""
                        color: Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: sheet.track ? sheet.track.artist : ""
                        color: Theme.inkSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: Theme.dp(6)
                        IconImage {
                            visible: sheet.track && sheet.track.liked === true
                            source: "qrc:/icons/favorite.svg"
                            sourceSize.width: Theme.dp(16)
                            sourceSize.height: Theme.dp(16)
                            color: Theme.accent
                        }
                        IconImage {
                            visible: sheet.track && sheet.track.disliked === true
                            source: "qrc:/icons/heart_off.svg"
                            sourceSize.width: Theme.dp(16)
                            sourceSize.height: Theme.dp(16)
                            color: Theme.inkTertiary
                        }
                        Text {
                            visible: sheet.track && sheet.track.isExplicit === true
                            text: qsTr("Explicit")
                            color: Theme.inkTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.dp(8)
                Layout.bottomMargin: Theme.dp(8)
                spacing: Theme.dp(2)
                Fact { label: qsTr("Album"); value: sheet.track ? sheet.track.album : "" }
                Fact {
                    label: qsTr("Duration")
                    value: sheet.track && sheet.track.durationMs > 0 ? Theme.formatTime(sheet.track.durationMs) : ""
                }
                Fact { label: qsTr("Quality"); value: sheet.track ? sheet.track.quality : "" }
                Fact { label: qsTr("Source"); value: sheet.track ? sheet.track.sourceName : "" }
                Fact {
                    label: qsTr("Last played")
                    value: sheet.track && Theme.formatPlayedAt(sheet.track.lastPlayedAt) || qsTr("Never")
                }
            }

            // What can be done with it.
            ColumnLayout {
                Layout.fillWidth: true
                visible: !sheet.choosingPlaylist
                spacing: 0
                Action {
                    visible: sheet.track && sheet.track.canPlay === true
                    iconName: "play_arrow"
                    text: qsTr("Play")
                    onClicked: sheet.track.play()
                }
                Action {
                    iconName: "playlist_play"
                    text: qsTr("Play next")
                    onClicked: sheet.track.playNext()
                }
                Action {
                    iconName: "playlist_add"
                    text: qsTr("Add to queue")
                    onClicked: sheet.track.addToQueue()
                }
                Action {
                    visible: sheet.track && sheet.track.canEditPlaylists === true
                    iconName: "playlist_add"
                    text: qsTr("Playlists…")
                    // The playlists come in the actions' place.
                    keepsSheetOpen: true
                    onClicked: {
                        sheet.choosingPlaylist = true
                        sheet.track.loadPlaylists()
                    }
                }
                Action {
                    visible: sheet.track && sheet.track.canLike === true
                    iconName: sheet.track && sheet.track.liked ? "favorite" : "favorite_border"
                    tint: sheet.track && sheet.track.liked ? Theme.accent : Theme.ink
                    text: sheet.track && sheet.track.liked ? qsTr("Unlike") : qsTr("Like")
                    onClicked: sheet.track.toggleLiked()
                }
                Action {
                    visible: sheet.track && sheet.track.canDislike === true
                    iconName: sheet.track && sheet.track.disliked ? "heart_off" : "heart_off_outline"
                    tint: sheet.track && sheet.track.disliked ? Theme.accent : Theme.ink
                    text: sheet.track && sheet.track.disliked ? qsTr("Remove dislike") : qsTr("Dislike")
                    onClicked: sheet.track.toggleDisliked()
                }
                Action {
                    visible: sheet.track && sheet.track.canStartRadio === true
                    iconName: "radio"
                    text: qsTr("Start radio from this track")
                    onClicked: sheet.track.startRadio(qsTr("Radio: %1").arg(sheet.track.title))
                }
                Action {
                    visible: sheet.track && sheet.track.webUrl !== ""
                    iconName: "open_in_new"
                    text: qsTr("Open track page")
                    onClicked: Qt.openUrlExternally(sheet.track.webUrl)
                }
                Action {
                    visible: sheet.track && sheet.track.canDownload === true
                    iconName: "file_download"
                    text: qsTr("Save to downloads")
                    onClicked: sheet.track.download()
                }
            }

            // The user's own playlists, which hold it or not.
            ColumnLayout {
                Layout.fillWidth: true
                visible: sheet.choosingPlaylist
                spacing: 0
                IconButton {
                    iconName: "arrow_back"
                    onClicked: sheet.choosingPlaylist = false
                }
                Spinner {
                    Layout.alignment: Qt.AlignHCenter
                    running: sheet.track && sheet.track.playlistsLoading === true
                }
                BodyText {
                    visible: sheet.track && sheet.track.playlistsFailed === true
                    text: qsTr("Couldn't load playlists")
                }
                Repeater {
                    model: sheet.backend ? sheet.backend.trackPlaylists : null
                    delegate: CheckDelegate {
                        id: choice
                        required property var playlistId
                        required property var title
                        required property var contains
                        required property var changing
                        Layout.fillWidth: true
                        text: choice.title || ""
                        checked: choice.contains === true
                        enabled: choice.changing !== true
                        onClicked: sheet.track.setInPlaylist(choice.playlistId, checked)
                        contentItem: Text {
                            text: choice.text
                            color: Theme.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
