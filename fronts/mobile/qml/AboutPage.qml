import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts

// About CloudMus, as the desktop's About dialog (Ui::AboutDialog): the logo,
// name and version, where the project lives, and its authors and license.
// The logo and the clouds behind move with what plays.
Page {
    id: page
    bottomPadding: Theme.pageInsetBottom

    property QtObject backend
    property QtObject platform
    signal closeRequested()

    readonly property QtObject pulse: backend ? backend.pulse : null
    // 0: Authors, 1: Contributors, 2: Technologies, 3: License.
    property int tab: 0
    readonly property var _panes: [authorsPane, contributorsPane, technologiesPane, licensePane]
    // A tab has been switched to: from then on the panes' room follows them
    // smoothly, not on the first show.
    property bool _panesMoved: false
    // Over to tab `index`: its pane comes from the side it's on, the one left
    // goes to the other.
    function select(index) {
        if (index === tab || index < 0 || index >= _panes.length)
            return
        const direction = index > tab ? 1 : -1
        _panesMoved = true
        _panes[tab].leave(direction)
        _panes[index].enter(direction)
        tab = index
    }

    // One tab's content: shown at once if it's the tab picked as the page
    // opens, then in and out by enter() and leave().
    component TabPane: Item {
        id: pane
        required property int index
        default property alias contents: column.data
        property alias spacing: column.spacing

        readonly property real _shift: Theme.dp(32)
        width: parent ? parent.width : 0
        implicitHeight: column.implicitHeight
        opacity: 0
        visible: opacity > 0
        transform: Translate { id: shift }
        Component.onCompleted: if (index === page.tab) opacity = 1

        function enter(direction) {
            leaving.stop()
            entering.direction = direction
            entering.restart()
        }
        function leave(direction) {
            entering.stop()
            leaving.direction = direction
            leaving.restart()
        }
        // The one left is gone at once, fading fastest at the start: anything
        // slower trails over the one coming in.
        ParallelAnimation {
            id: entering
            property int direction: 1
            NumberAnimation { target: pane; property: "opacity"; to: 1; duration: 260; easing.type: Easing.OutCubic }
            NumberAnimation {
                target: shift; property: "x"; from: pane._shift * entering.direction; to: 0
                duration: 260; easing.type: Easing.OutCubic
            }
        }
        ParallelAnimation {
            id: leaving
            property int direction: 1
            NumberAnimation { target: pane; property: "opacity"; to: 0; duration: 70; easing.type: Easing.OutQuad }
            NumberAnimation {
                target: shift; property: "x"; to: -pane._shift * leaving.direction
                duration: 70; easing.type: Easing.OutQuad
            }
        }

        ColumnLayout {
            id: column
            width: parent.width
            spacing: Theme.spacing
        }
    }

    // The music is analyzed only while the page is in view, the app in front.
    readonly property bool watching: visible && Qt.application.state === Qt.ApplicationActive
    property bool _holding: false
    function _hold(on) {
        if (on === _holding || !backend)
            return
        _holding = on
        if (on)
            backend.holdPulse()
        else
            backend.releasePulse()
    }
    onWatchingChanged: _hold(watching)
    Component.onCompleted: _hold(watching)
    Component.onDestruction: _hold(false)

    background: Rectangle { color: Theme.surface0 }

    header: PageBar {
        title: qsTr("About CloudMus")
        onBackRequested: page.closeRequested()
    }

    function _link(href, text) {
        return "<a href=\"" + href + "\" style=\"color:" + Theme.accent + "; text-decoration:none;\">" + text + "</a>"
    }

    CloudsBackdrop {
        id: backdrop
        objectName: "cloudsBackdrop"
        anchors.fill: parent
        origin: logo
        pulse: page.pulse
        running: page.watching
        onStepped: (dtMs) => logo.step(dtMs)
        lean: finger.active ? backdrop.mapFromItem(null, finger.point.scenePosition) : undefined
    }

    ScrollArea {
        id: area
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.padding * 2
        clip: true

        ColumnLayout {
            id: content
            objectName: "aboutContent"
            y: Theme.padding
            // A column to read on a wide screen, not lines across it.
            width: Math.min(area.width - Theme.padding * 2, Theme.dp(560))
            x: (area.width - width) / 2
            spacing: Theme.spacing

            // The logo beside the name and the version, the group centered.
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.padding
                spacing: Theme.padding
                PulsingLogo {
                    id: logo
                    objectName: "aboutLogo"
                    pulse: page.pulse
                }
                ColumnLayout {
                    spacing: 0
                    Text {
                        text: "CloudMus"
                        color: Theme.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.dp(32)
                        font.weight: Font.Bold
                    }
                    RowLayout {
                        spacing: Theme.dp(4)
                        Text {
                            objectName: "aboutVersion"
                            text: qsTr("Version %1").arg(page.platform ? page.platform.version : "")
                            color: Theme.inkSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }
                        IconButton {
                            id: copyButton
                            objectName: "copyVersion"
                            property bool copied: false
                            size: Theme.dp(36)
                            iconName: copied ? "check" : "content_copy"
                            color: copied ? Theme.accent : Theme.inkSecondary
                            onClicked: {
                                page.platform.copyText(page.platform.version)
                                // Brief confirmation right on the button.
                                copied = true
                                copiedTimer.restart()
                            }
                            Timer {
                                id: copiedTimer
                                interval: 1500
                                onTriggered: copyButton.copied = false
                            }
                        }
                    }
                }
            }

            BodyText {
                Layout.topMargin: Theme.padding
                text: qsTr("Enjoy listening to your clouds")
                horizontalAlignment: Text.AlignHCenter
            }
            BodyText {
                objectName: "aboutLinks"
                textFormat: Text.RichText
                horizontalAlignment: Text.AlignHCenter
                text: [
                    page._link("https://cloudmus.app", "cloudmus.app"),
                    page._link("https://github.com/cloudmus/cloudmus", "GitHub"),
                    page._link("https://t.me/cloudmus_app", qsTr("Telegram chat")),
                ].join(" · ")
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }

            // On one line, scrolled sideways where the screen is too narrow for
            // it, and dissolving at the edge with more beyond, as lists do.
            Flickable {
                id: tabStrip
                objectName: "aboutTabs"
                Layout.fillWidth: true
                Layout.topMargin: Theme.padding
                implicitHeight: tabRow.height
                contentWidth: tabRow.width
                contentHeight: tabRow.height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickMaxVelocity
                flickDeceleration: Theme.flickDeceleration
                clip: true

                // The tab picked, all in view.
                function reveal(index) {
                    const chip = tabRepeater.itemAt(index)
                    if (!chip || contentWidth <= width)
                        return
                    const end = contentWidth - width
                    if (chip.x < contentX)
                        revealing.to = Math.max(0, chip.x - Theme.padding)
                    else if (chip.x + chip.width > contentX + width)
                        revealing.to = Math.min(end, chip.x + chip.width - width + Theme.padding)
                    else
                        return
                    revealing.restart()
                }
                NumberAnimation {
                    id: revealing
                    target: tabStrip
                    property: "contentX"
                    duration: Theme.durationPage
                    easing.type: Easing.OutCubic
                }
                Connections {
                    target: page
                    function onTabChanged() { tabStrip.reveal(page.tab) }
                }

                Row {
                    id: tabRow
                    spacing: Theme.spacing
                    Repeater {
                        id: tabRepeater
                        model: [qsTr("Authors"), qsTr("Contributors"), qsTr("Technologies"), qsTr("License")]
                        delegate: Chip {
                            required property int index
                            required property string modelData
                            objectName: ["authorsTab", "contributorsTab", "technologiesTab", "licenseTab"][index]
                            text: modelData
                            checked: page.tab === index
                            onClicked: page.select(index)
                        }
                    }
                }

                ScrollEdgeFade {
                    // The strip's, not its content's: it stays put as that scrolls.
                    parent: tabStrip
                    view: tabStrip
                    orientation: Qt.Horizontal
                }
            }

            // The tabs' panes, one over another. The one picked slides in from
            // its side and fades in as the one left slides away and fades out
            // (page.select()); the room they take follows the one picked.
            Item {
                id: tabPanes
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing
                implicitHeight: page._panes[page.tab] ? page._panes[page.tab].implicitHeight : 0
                Behavior on implicitHeight {
                    enabled: page._panesMoved
                    NumberAnimation { duration: Theme.durationPage; easing.type: Easing.OutCubic }
                }

                // A card each, one under another: the page's column is narrow
                // for two side by side even on a head unit.
                component People: Repeater {
                    delegate: PersonCard {
                        required property var modelData
                        person: modelData
                    }
                }
                TabPane {
                    id: authorsPane
                    objectName: "authors"
                    index: 0
                    People { model: page.platform ? page.platform.authors : [] }
                }
                TabPane {
                    id: contributorsPane
                    objectName: "contributors"
                    index: 1
                    People { model: page.platform ? page.platform.contributors : [] }
                }

                // The AI agents that wrote code for it, and what it's made with,
                // each by its logo: the agents' mascots (the ones they have in their
                // own apps, fronts/qt/resources/agents/make_sprites.py) wave.
                TabPane {
                    id: technologiesPane
                    objectName: "technologies"
                    index: 2
                    spacing: Theme.dp(12)
                    Repeater {
                        model: [
                            { strip: "qrc:/about/clawd-waving.png", frames: 9, frameMs: 220, width: 20, height: 14,
                              pixelArt: true, name: "Claude", url: "https://claude.com/product/claude-code",
                              what: qsTr("AI coding agent by Anthropic") },
                            { strip: "qrc:/about/codex-waving.png", frames: 10, frameMs: 140, width: 72, height: 89,
                              name: "Codex", url: "https://openai.com/codex", what: qsTr("AI coding agent by OpenAI") },
                            { logo: "qrc:/about/qt.svg", name: "Qt", url: "https://www.qt.io",
                              what: qsTr("The interface, on the desktop and on phones") },
                            { logo: "qrc:/about/mpv.svg", name: "mpv", url: "https://mpv.io",
                              what: qsTr("Plays the music") },
                            { logo: "qrc:/about/cpp.svg", name: "C++", url: "https://isocpp.org",
                              what: qsTr("The app itself") },
                            // Black: the text's color, for the dark theme.
                            { logo: "qrc:/about/rust.svg", tinted: true, name: "Rust", url: "https://www.rust-lang.org",
                              what: qsTr("The music services' backends") },
                            { logo: "qrc:/about/python.svg", name: "Python", url: "https://www.python.org",
                              what: qsTr("The music services' backends") },
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.padding
                            Item {
                                implicitWidth: Theme.dp(48)
                                implicitHeight: Theme.dp(48)
                                ColorImage {
                                    objectName: "technologyLogo"
                                    visible: !!modelData.logo
                                    anchors.fill: parent
                                    source: modelData.logo || ""
                                    sourceSize.width: width
                                    sourceSize.height: height
                                    fillMode: Image.PreserveAspectFit
                                    color: modelData.tinted ? Theme.ink : "transparent"
                                }
                                AnimatedSprite {
                                    objectName: "agentMascot"
                                    visible: !!modelData.strip
                                    anchors.centerIn: parent
                                    // Kept to the frame's proportions.
                                    readonly property real _scale: !visible ? 1
                                        : Math.min(parent.width / frameWidth, parent.height / frameHeight)
                                    width: frameWidth * _scale
                                    height: frameHeight * _scale
                                    source: modelData.strip || ""
                                    frameCount: modelData.frames || 1
                                    frameDuration: modelData.frameMs || 100
                                    frameWidth: modelData.width || 1
                                    frameHeight: modelData.height || 1
                                    // One frame, then the next: not blended between.
                                    interpolate: false
                                    smooth: !modelData.pixelArt
                                    loops: AnimatedSprite.Infinite
                                    running: visible && page.watching && page.tab === 2
                                }
                            }
                            BodyText {
                                textFormat: Text.RichText
                                text: page._link(modelData.url, modelData.name) + " · " + modelData.what
                                onLinkActivated: (link) => Qt.openUrlExternally(link)
                            }
                        }
                    }
                }

                // A paragraph to a line (Ui::Platform::licenseText), a gap after each.
                TabPane {
                    id: licensePane
                    objectName: "license"
                    index: 3
                    Repeater {
                        model: page.platform ? page.platform.licenseText.split("\n") : []
                        delegate: BodyText {
                            required property string modelData
                            text: modelData
                            color: Theme.inkSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }

    // Over everything, and taking nothing: only to know where a finger is, for
    // the clouds to lean to it.
    Item {
        anchors.fill: parent
        PointHandler {
            id: finger
            acceptedButtons: Qt.LeftButton
        }
    }
}
