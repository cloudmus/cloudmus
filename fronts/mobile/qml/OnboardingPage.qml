import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl

// The first launch's slides: what CloudMus does and how to start, over the
// generated background (AuraBackground), its colors flowing from slide to
// slide. The pictures are drawn here from the app's own icons, and move only
// on the slide in view. The last slide leads to the Library; Skip, Back or
// "Not now" just close them. Over the whole window, status bar too.
Item {
    id: page

    property QtObject backend
    // Gone for good, the fade out done: Main.qml remembers it and drops the page.
    signal finished()
    signal libraryRequested()

    readonly property bool wide: width > height * 1.1
    // Wide (a phone on its side, a head unit): the picture in the left half,
    // the words in the right one with the dots and the button under them.
    readonly property real _margin: Theme.padding * 1.5
    readonly property real _rightX: Math.round(width * 0.5)
    readonly property real _rightWidth: width - _rightX - Theme.insetRight - _margin
    // The slide in view, or being gone to.
    property int current: 0
    readonly property bool onLast: current === slides.length - 1

    // Each slide's art, its words, and the background's seed: a different one
    // for each, for the colors to change as they go by.
    readonly property var slides: [
        { art: "logo", seed: "CloudMus · Welcome",
          title: qsTr("Welcome to CloudMus"), text: qsTr("All your music in one player") },
        { art: "sources", seed: "CloudMus · Sources",
          title: qsTr("Every source in one place"), text: qsTr("Music services and the files on your phone") },
        { art: "wave", seed: "CloudMus · Waves",
          title: qsTr("Endless waves"), text: qsTr("Music picked for you that never runs out") },
        { art: "offline", seed: "CloudMus · Offline",
          title: qsTr("Offline too"), text: qsTr("Save playlists and listen without the internet") },
        { art: "start", seed: "CloudMus · Start",
          title: qsTr("Getting started"), text: qsTr("Open the Library, pick a source and sign in") },
    ]

    function next() {
        if (onLast)
            openLibrary()
        else
            goTo(current + 1)
    }
    // The Library is opened under the slides, which then fade off it.
    function openLibrary() {
        if (leaving.running)
            return
        libraryRequested()
        close()
    }
    function close() {
        if (!leaving.running)
            leaving.start()
    }

    // Off when it comes under the fading start screen.
    property bool fadeIn: true
    opacity: fadeIn ? 0 : 1
    Component.onCompleted: if (fadeIn) entering.start()
    NumberAnimation {
        id: entering
        target: page
        property: "opacity"
        to: 1
        duration: Theme.durationPage
        easing.type: Easing.OutCubic
    }
    ParallelAnimation {
        id: leaving
        NumberAnimation { target: page; property: "opacity"; to: 0; duration: Theme.durationPage; easing.type: Easing.InCubic }
        NumberAnimation { target: page; property: "scale"; to: 1.04; duration: Theme.durationPage; easing.type: Easing.InCubic }
        onFinished: page.finished()
    }

    // Nothing under the slides is reached through them.
    MouseArea {
        anchors.fill: parent
        onWheel: (wheel) => wheel.accepted = true
    }

    AuraBackground {
        objectName: "onboardingAura"
        anchors.fill: parent
        // All of it in color: no edges dissolving into the window's.
        edgeFade: 0
        seed: page.slides[page.current].seed
    }
    // A little darker at the bottom, under the words and the buttons.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.5
        gradient: Gradient {
            GradientStop { position: 0; color: "#00000000" }
            GradientStop { position: 1; color: "#59000000" }
        }
    }

    // What's white over the background, the pictures' parts.
    readonly property color glass: Qt.rgba(1, 1, 1, 0.16)
    readonly property color glassEdge: Qt.rgba(1, 1, 1, 0.38)

    component Disc: Rectangle {
        property real side
        width: side
        height: side
        radius: side / 2
    }
    // A white disc with an icon in it, in the background's warm accent.
    component Badge: Disc {
        id: badge
        property string iconSource
        property real iconShare: 0.52
        color: "#FFFFFF"
        IconImage {
            anchors.centerIn: parent
            source: badge.iconSource
            sourceSize.width: badge.side * badge.iconShare
            sourceSize.height: badge.side * badge.iconShare
            color: Theme.accentOnAura
        }
    }
    // Up and down a little, forever, while `running`.
    component Float: Item {
        id: floater
        property bool running: false
        property real amplitude: Theme.dp(6)
        property int period: 3200
        property real shift: 0
        transform: Translate { y: floater.shift }
        SequentialAnimation on shift {
            running: floater.running
            loops: Animation.Infinite
            NumberAnimation { from: -floater.amplitude; to: floater.amplitude; duration: floater.period / 2; easing.type: Easing.InOutSine }
            NumberAnimation { from: floater.amplitude; to: -floater.amplitude; duration: floater.period / 2; easing.type: Easing.InOutSine }
        }
    }

    // 1. The logo among clouds drifting by.
    Component {
        id: logoArt
        Item {
            id: art
            property real side
            property bool running
            Disc {
                anchors.centerIn: parent
                side: art.side * 0.72
                color: Qt.rgba(1, 1, 1, 0.12)
                SequentialAnimation on scale {
                    running: art.running
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.92; to: 1.06; duration: 1600; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.06; to: 0.92; duration: 1600; easing.type: Easing.InOutSine }
                }
            }
            Disc {
                anchors.centerIn: parent
                side: art.side * 0.52
                color: Qt.rgba(1, 1, 1, 0.14)
            }
            Repeater {
                // x, y (of the side, from the center), width, opacity, period.
                model: [
                    [-0.36, -0.30, 0.30, 0.55, 3600],
                    [0.34, -0.20, 0.22, 0.45, 4200],
                    [-0.30, 0.30, 0.20, 0.40, 3900],
                    [0.30, 0.32, 0.28, 0.60, 3300],
                ]
                delegate: Float {
                    required property var modelData
                    running: art.running
                    period: modelData[4]
                    width: art.side * modelData[2]
                    height: width * 5 / 8
                    x: art.width / 2 + art.side * modelData[0] - width / 2
                    y: art.height / 2 + art.side * modelData[1] - height / 2
                    ColorImage {
                        anchors.fill: parent
                        source: "qrc:/icons/cloud.svg"
                        sourceSize.width: 192
                        sourceSize.height: 120
                        color: "#FFFFFF"
                        opacity: modelData[3]
                    }
                }
            }
            Float {
                anchors.centerIn: parent
                width: art.side * 0.38
                height: width
                running: art.running
                amplitude: Theme.dp(4)
                Image {
                    anchors.fill: parent
                    source: "qrc:/icons/logo.svg"
                    sourceSize.width: width * 1.5
                    sourceSize.height: height * 1.5
                    smooth: true
                }
            }
        }
    }

    // 2. Sources going round the logo: the ones this app has, outside; the
    // phone, a folder and a song inside, the other way round.
    Component {
        id: sourcesArt
        Item {
            id: art
            property real side
            property bool running
            readonly property real outer: side * 0.45
            readonly property real inner: side * 0.28

            Disc {
                anchors.centerIn: parent
                side: art.outer * 2
                color: "transparent"
                border.color: page.glassEdge
                border.width: Theme.dp(1)
            }
            Disc {
                anchors.centerIn: parent
                side: art.inner * 2
                color: "transparent"
                border.color: page.glassEdge
                border.width: Theme.dp(1)
            }
            Disc {
                anchors.centerIn: parent
                side: art.side * 0.3
                color: page.glass
                border.color: page.glassEdge
                border.width: Theme.dp(1)
                Image {
                    anchors.centerIn: parent
                    width: parent.width * 0.6
                    height: width
                    source: "qrc:/icons/logo.svg"
                    sourceSize.width: width * 1.5
                    sourceSize.height: height * 1.5
                }
            }

            Item {
                id: outerOrbit
                anchors.fill: parent
                NumberAnimation on rotation {
                    running: art.running
                    from: 0
                    to: 360
                    duration: 48000
                    loops: Animation.Infinite
                }
                Repeater {
                    id: sources
                    model: page.backend ? page.backend.sources : null
                    delegate: Badge {
                        required property int index
                        required property var iconUrl
                        readonly property real angle: 2 * Math.PI * index / Math.max(1, sources.count) - Math.PI / 2
                        side: art.side * 0.18
                        x: art.width / 2 + Math.cos(angle) * art.outer - side / 2
                        y: art.height / 2 + Math.sin(angle) * art.outer - side / 2
                        rotation: -outerOrbit.rotation
                        iconSource: iconUrl ? iconUrl : "qrc:/icons/music_note.svg"
                    }
                }
            }
            Item {
                id: innerOrbit
                anchors.fill: parent
                NumberAnimation on rotation {
                    running: art.running
                    from: 360
                    to: 0
                    duration: 36000
                    loops: Animation.Infinite
                }
                Repeater {
                    model: ["smartphone", "folder", "music_note"]
                    delegate: Badge {
                        required property int index
                        required property string modelData
                        readonly property real angle: 2 * Math.PI * index / 3 + Math.PI / 6
                        side: art.side * 0.13
                        x: art.width / 2 + Math.cos(angle) * art.inner - side / 2
                        y: art.height / 2 + Math.sin(angle) * art.inner - side / 2
                        rotation: -innerOrbit.rotation
                        iconSource: "qrc:/icons/" + modelData + ".svg"
                    }
                }
            }
        }
    }

    // 3. Rings going out from the radio, on and on.
    Component {
        id: waveArt
        Item {
            id: art
            property real side
            property bool running
            property real phase: 0
            NumberAnimation on phase {
                running: art.running
                from: 0
                to: 1
                duration: 3000
                loops: Animation.Infinite
            }
            Repeater {
                model: 3
                delegate: Disc {
                    required property int index
                    readonly property real share: (art.phase + index / 3) % 1
                    anchors.centerIn: parent
                    side: art.side * 0.95
                    color: "transparent"
                    border.color: "#FFFFFF"
                    border.width: Theme.dp(2) / Math.max(0.3, scale)
                    scale: 0.36 + 0.64 * share
                    opacity: 0.75 * (1 - share)
                }
            }
            Disc {
                anchors.centerIn: parent
                side: art.side * 0.5
                color: page.glass
            }
            Badge {
                anchors.centerIn: parent
                side: art.side * 0.34
                iconShare: 0.5
                iconSource: "qrc:/icons/radio.svg"
            }
            // What plays, under it.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height / 2 + art.side * 0.28
                width: bars.width + Theme.dp(28)
                height: Theme.dp(34)
                radius: height / 2
                color: page.glass
                border.color: page.glassEdge
                border.width: Theme.dp(1)
                // In step with the rings: one clock for the whole picture.
                Row {
                    id: bars
                    anchors.centerIn: parent
                    height: Theme.dp(16)
                    spacing: Theme.dp(3)
                    Repeater {
                        model: [[2, 0.0], [3, 0.3], [2, 0.6], [1, 0.15]]
                        delegate: Rectangle {
                            required property var modelData
                            anchors.bottom: parent.bottom
                            width: Theme.dp(4)
                            radius: width / 2
                            color: "#FFFFFF"
                            height: bars.height * (0.3 + 0.7 * Math.abs(Math.sin(Math.PI * (art.phase * modelData[0] + modelData[1]))))
                        }
                    }
                }
            }
        }
    }

    // 4. Covers, the front one being saved.
    Component {
        id: offlineArt
        Item {
            id: art
            property real side
            property bool running
            readonly property real cover: side * 0.5

            Repeater {
                // x shift (of the side), rotation, fill.
                model: [[-0.17, -12, 0.20], [0.17, 11, 0.26]]
                delegate: Rectangle {
                    required property var modelData
                    width: art.cover
                    height: art.cover
                    radius: Theme.coverRadius
                    x: (art.width - width) / 2 + art.side * modelData[0]
                    y: (art.height - height) / 2 + Theme.dp(6)
                    rotation: modelData[1]
                    color: Qt.rgba(1, 1, 1, modelData[2])
                    border.color: page.glassEdge
                    border.width: Theme.dp(1)
                    IconImage {
                        anchors.centerIn: parent
                        source: "qrc:/icons/music_note.svg"
                        sourceSize.width: parent.width * 0.36
                        sourceSize.height: parent.width * 0.36
                        color: "#FFFFFF"
                        opacity: 0.8
                    }
                }
            }
            Float {
                width: art.cover
                height: art.cover
                x: (art.width - width) / 2
                y: (art.height - height) / 2 - Theme.dp(6)
                running: art.running
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.coverRadius
                    color: "#FFFFFF"
                    IconImage {
                        anchors.centerIn: parent
                        source: "qrc:/icons/album.svg"
                        sourceSize.width: parent.width * 0.46
                        sourceSize.height: parent.width * 0.46
                        color: Theme.accentOnAura
                    }
                }
                // Being saved: round and round, then again.
                Item {
                    width: art.side * 0.24
                    height: width
                    x: parent.width - width * 0.62
                    y: parent.height - height * 0.62
                    Disc {
                        anchors.centerIn: parent
                        side: parent.width
                        color: Theme.accentOnAura
                        border.color: "#FFFFFF"
                        border.width: Theme.dp(3)
                    }
                    ProgressRing {
                        id: ring
                        anchors.fill: parent
                        anchors.margins: Theme.dp(6)
                        color: "#FFFFFF"
                        lineWidth: Theme.dp(3)
                        progress: 0
                        SequentialAnimation on progress {
                            running: art.running
                            loops: Animation.Infinite
                            NumberAnimation { from: 0; to: 1; duration: 2200; easing.type: Easing.InOutQuad }
                            PauseAnimation { duration: 700 }
                        }
                    }
                    IconImage {
                        anchors.centerIn: parent
                        source: ring.progress >= 1 ? "qrc:/icons/check.svg" : "qrc:/icons/file_download.svg"
                        sourceSize.width: parent.width * 0.42
                        sourceSize.height: parent.width * 0.42
                        color: "#FFFFFF"
                    }
                }
            }
            // No internet, and it plays on.
            Rectangle {
                x: (art.width - art.cover) / 2 - art.side * 0.17 - width * 0.5
                y: (art.height - art.cover) / 2 - height * 0.6
                width: Theme.dp(44)
                height: Theme.dp(44)
                radius: width / 2
                color: page.glass
                border.color: page.glassEdge
                border.width: Theme.dp(1)
                IconImage {
                    anchors.centerIn: parent
                    source: "qrc:/icons/cloud_off.svg"
                    sourceSize.width: Theme.dp(22)
                    sourceSize.height: Theme.dp(22)
                    color: "#FFFFFF"
                }
            }
        }
    }

    // 5. The Library, in a phone: a source is tapped.
    Component {
        id: startArt
        Item {
            id: art
            property real side
            property bool running
            // 0..1 over a tap: the finger comes down, the row lights up, and
            // it all settles again.
            property real tap: 0
            SequentialAnimation on tap {
                running: art.running
                loops: Animation.Infinite
                PauseAnimation { duration: 600 }
                NumberAnimation { from: 0; to: 1; duration: 900; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 1200 }
                NumberAnimation { from: 1; to: 0; duration: 400 }
            }

            Float {
                id: phone
                anchors.centerIn: parent
                width: art.side * 0.6
                height: art.side * 0.95
                running: art.running
                amplitude: Theme.dp(4)
                readonly property real rowsTop: Theme.dp(42)
                readonly property real rowGap: Theme.dp(6)
                readonly property real rowHeight: (height - rowsTop - Theme.dp(14) - rowGap * 3) / 4
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.dp(22)
                    color: page.glass
                    border.color: page.glassEdge
                    border.width: Theme.dp(1.5)
                }
                // The Library's bar.
                Rectangle {
                    x: Theme.dp(14)
                    y: Theme.dp(18)
                    width: parent.width * 0.4
                    height: Theme.dp(8)
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.7)
                }
                Column {
                    x: Theme.dp(8)
                    y: phone.rowsTop
                    width: parent.width - Theme.dp(16)
                    spacing: phone.rowGap
                    Repeater {
                        model: ["smartphone", "cloud", "person", "folder"]
                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property string modelData
                            readonly property bool picked: index === 1
                            width: parent.width
                            height: phone.rowHeight
                            radius: Theme.dp(10)
                            color: picked ? Qt.rgba(1, 1, 1, 0.12 + 0.83 * art.tap) : Qt.rgba(1, 1, 1, 0.12)
                            Disc {
                                id: icon
                                x: Theme.dp(8)
                                anchors.verticalCenter: parent.verticalCenter
                                side: Math.min(parent.height - Theme.dp(12), Theme.dp(34))
                                color: row.picked && art.tap > 0.5 ? Theme.accentOnAura : "#FFFFFF"
                                IconImage {
                                    anchors.centerIn: parent
                                    source: row.modelData === "cloud" ? "qrc:/icons/cloud.svg"
                                                                      : "qrc:/icons/" + row.modelData + ".svg"
                                    sourceSize.width: parent.width * 0.56
                                    sourceSize.height: parent.width * 0.56
                                    color: row.picked && art.tap > 0.5 ? "#FFFFFF" : Theme.accentOnAura
                                }
                            }
                            Column {
                                anchors.left: icon.right
                                anchors.leftMargin: Theme.dp(8)
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.dp(10)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.dp(5)
                                Rectangle {
                                    width: parent.width * (0.8 - 0.12 * (row.index % 2))
                                    height: Theme.dp(6)
                                    radius: height / 2
                                    color: row.picked && art.tap > 0.5 ? Theme.accentOnAura : Qt.rgba(1, 1, 1, 0.75)
                                }
                                Rectangle {
                                    width: parent.width * 0.5
                                    height: Theme.dp(5)
                                    radius: height / 2
                                    color: row.picked && art.tap > 0.5 ? Qt.rgba(0.8, 0.3, 0.15, 0.5) : Qt.rgba(1, 1, 1, 0.4)
                                }
                            }
                        }
                    }
                }
                // The finger's touch, on the row picked.
                Disc {
                    side: art.side * 0.2
                    x: parent.width * 0.62 - side / 2
                    y: phone.rowsTop + phone.rowHeight * 1.5 + phone.rowGap - side / 2
                    color: "transparent"
                    border.color: "#FFFFFF"
                    border.width: Theme.dp(2.5)
                    scale: 0.4 + 0.8 * art.tap
                    opacity: art.tap > 0 ? 0.9 * (1 - art.tap) + 0.1 : 0
                }
            }
        }
    }

    readonly property var _arts: ({ logo: logoArt, sources: sourcesArt, wave: waveArt, offline: offlineArt, start: startArt })

    // Over to slide `index`, gliding: the list's own flick, made for long
    // lists, threw a whole screen past in a blink.
    function goTo(index) {
        current = Math.max(0, Math.min(slides.length - 1, index))
        const to = current * list.width
        const share = Math.min(1, Math.abs(to - list.contentX) / Math.max(1, list.width))
        settling.to = to
        settling.duration = Math.round(320 + 380 * share)
        settling.restart()
    }
    NumberAnimation {
        id: settling
        objectName: "onboardingSettling"
        target: list
        property: "contentX"
        easing.type: Easing.OutCubic
    }

    // The slides side by side, slide i at i × the width, the view `contentX`
    // along: no list of their own, whose origin moves as a turned screen lays
    // them out anew.
    Item {
        id: list
        objectName: "onboardingSlides"
        anchors.fill: parent
        property real contentX: 0
        readonly property int count: page.slides.length
        // A screen turned: the slide in view stays.
        onWidthChanged: if (!swipe.active) { settling.stop(); contentX = page.current * width }

        // The slides follow the finger; let go, on to the next one if it went
        // a third of the way or was thrown, else back.
        property real _dragFrom: 0
        DragHandler {
            id: swipe
            target: null
            yAxis.enabled: false
            onActiveChanged: {
                if (active) {
                    settling.stop()
                    list._dragFrom = list.contentX
                    return
                }
                const position = list.contentX / Math.max(1, list.width)
                const velocity = centroid.velocity.x
                let index = Math.round(position)
                if (Math.abs(velocity) > Theme.dp(350))
                    index = velocity < 0 ? Math.ceil(position) : Math.floor(position)
                else if (position - page.current > 0.33)
                    index = page.current + 1
                else if (page.current - position > 0.33)
                    index = page.current - 1
                page.goTo(index)
            }
            onTranslationChanged: {
                if (!active)
                    return
                const last = (page.slides.length - 1) * list.width
                let x = list._dragFrom - translation.x
                // Past either end it gives, but only a little.
                if (x < 0)
                    x *= 0.3
                else if (x > last)
                    x = last + (x - last) * 0.3
                list.contentX = x
            }
        }

        Repeater {
            model: page.slides
            delegate: Item {
                id: slide
                required property var modelData
                required property int index
                width: list.width
                height: list.height
                x: index * list.width - list.contentX
                // Off the middle, -1 .. 1: the picture lags behind the words.
                readonly property real offset: Math.max(-1, Math.min(1, x / Math.max(1, width)))
                // Only what's in view, or coming into it.
                visible: x > -width && x < width

                // Clear of the status bar and Skip above, the dots and the button
                // below; wide, the picture's half has the whole height.
                Item {
                    id: area
                    x: Theme.insetLeft + page._margin
                    y: page.wide ? Theme.insetTop + page._margin : Theme.insetTop + Theme.touch + Theme.spacing
                    width: page.wide ? page._rightX - x - page._margin : slide.width - x - Theme.insetRight - page._margin
                    height: page.wide ? slide.height - y - Theme.insetBottom - page._margin
                                      : controls.y - Theme.dp(24) - y
                }
                // Wide: the words' room, over the dots.
                readonly property real wordsTop: Theme.insetTop + Theme.touch + Theme.spacing
                readonly property real wordsBottom: controls.y - Theme.dp(28)

                readonly property real artSide: page.wide
                    ? Math.min(area.height * 0.86, area.width * 0.9, Theme.dp(420))
                    : Math.min(area.width * 0.86, area.height * 0.56, Theme.dp(360))

                Loader {
                    objectName: "onboardingArt_" + slide.modelData.art
                    width: slide.artSide
                    height: slide.artSide
                    // Further off than its slide: the neighbors' never peek in.
                    x: area.x + (area.width - width) / 2 + slide.offset * slide.width * 0.35
                    y: page.wide ? area.y + (area.height - height) / 2
                                 : area.y + (area.height * 0.62 - height) / 2
                    opacity: Math.max(0, 1 - Math.abs(slide.offset) * 1.4)
                    scale: 1 - Math.abs(slide.offset) * 0.15
                    sourceComponent: page._arts[slide.modelData.art]
                    onLoaded: {
                        item.side = Qt.binding(() => slide.artSide)
                        item.running = Qt.binding(() => slide.index === page.current && page.visible
                                                         && Qt.application.state === Qt.ApplicationActive)
                    }
                }

                Column {
                    id: words
                    spacing: Theme.dp(10)
                    width: page.wide ? Math.min(page._rightWidth, Theme.dp(460)) : Math.min(area.width, Theme.dp(420))
                    x: page.wide ? page._rightX : area.x + (area.width - width) / 2
                    // Wide, it sits low, just over the dots, whatever its length.
                    y: page.wide ? Math.max(slide.wordsTop, slide.wordsBottom - height
                                            - Math.max(0, (slide.wordsBottom - slide.wordsTop - height) * 0.35))
                                 : area.y + area.height * 0.64
                    opacity: Math.max(0, 1 - Math.abs(slide.offset) * 1.5)
                    ShadowText {
                        objectName: "onboardingTitle"
                        width: parent.width
                        text: slide.modelData.title
                        horizontalAlignment: page.wide ? Text.AlignLeft : Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.dp(28)
                        font.weight: Font.Bold
                    }
                    ShadowText {
                        width: parent.width
                        text: slide.modelData.text
                        horizontalAlignment: page.wide ? Text.AlignLeft : Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Qt.rgba(1, 1, 1, 0.9)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        lineHeight: 1.15
                    }
                }
            }
        }
    }

    // A white label on nothing: what's not the way on.
    component QuietButton: AbstractButton {
        id: quiet
        implicitHeight: Theme.touch
        implicitWidth: quietLabel.implicitWidth + Theme.dp(32)
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: height / 2
            color: quiet.down ? Qt.rgba(1, 1, 1, 0.18) : "transparent"
        }
        contentItem: ShadowText {
            id: quietLabel
            text: quiet.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: "#FFFFFF"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
        }
    }

    QuietButton {
        objectName: "onboardingSkip"
        anchors.right: parent.right
        anchors.rightMargin: Theme.insetRight + Theme.spacing
        y: Theme.insetTop + Theme.spacing
        text: page.onLast ? qsTr("Not now") : qsTr("Skip")
        onClicked: page.close()
    }

    Column {
        id: controls
        x: page.wide ? page._rightX : (parent.width - width) / 2
        y: parent.height - height - Theme.insetBottom - page._margin
        width: page.wide ? Math.min(page._rightWidth, Theme.dp(360))
                         : Math.min(parent.width - Theme.insetLeft - Theme.insetRight - page._margin * 2, Theme.dp(380))
        spacing: page.wide ? Theme.dp(18) : Theme.dp(22)

        // Where we are: the slide in view a longer dash.
        Row {
            objectName: "onboardingDots"
            x: page.wide ? 0 : (parent.width - width) / 2
            spacing: Theme.dp(8)
            Repeater {
                model: page.slides.length
                delegate: Rectangle {
                    required property int index
                    readonly property bool shown: index === page.current
                    width: shown ? Theme.dp(22) : Theme.dp(8)
                    height: Theme.dp(8)
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, shown ? 1 : 0.45)
                    Behavior on width { NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic } }
                    TapHandler { onTapped: page.goTo(index) }
                }
            }
        }

        TextButton {
            objectName: "onboardingNext"
            width: parent.width
            height: Theme.touchLarge
            tint: page.onLast ? Theme.accentOnAura : Qt.rgba(1, 1, 1, 0.22)
            text: page.onLast ? qsTr("Open the Library") : qsTr("Next")
            onClicked: page.next()
        }
    }
}
