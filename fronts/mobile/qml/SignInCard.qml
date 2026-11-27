import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Signing in to any source, by the methods it offers (docs/protocol.md §10).
// Those this device can carry out are offered to choose from, the best one
// for it marked and chosen to begin with: in the built-in browser; on a phone
// that signs in for this device (for one whose browser can't, such as a head
// unit's); a code to enter on a web page, with where to enter it also as a QR
// code; or a form.
Panel {
    id: card

    // The source asking: another one starts the card over.
    property string sourceId
    property string sourceName
    // The kinds of the methods offered, the source's preferred first.
    property var kinds: []
    // `code`: the page to open and the code to enter there.
    property string url
    property string code
    // `browser`: the method as JSON, and whether it has to go through a proxy.
    property string browser
    property bool browserNeedsProxy: false
    // `form`: its fields and what the source says about them.
    property var fields: []
    property string formMessage
    property bool busy: false
    property string error
    property QtObject platform: null
    // The QR code beside the rest rather than under it.
    property bool wide: false
    signal signInRequested()
    signal browserRequested()
    signal formSubmitted(var values)

    readonly property bool _hasBrowser: browser !== "" && !!platform && platform.hasSignInBrowser === true
    // An old WebView can't be sent through a proxy, and going past the one
    // the user chose isn't an option.
    readonly property bool _proxyInTheWay: _hasBrowser && browserNeedsProxy
                                           && platform.signInBrowserTakesProxy !== true
    readonly property bool _canBrowse: _hasBrowser && !_proxyInTheWay
    readonly property bool _browserOutdated: !!platform && platform.signInBrowserOutdated === true
    readonly property bool _canUsePhone: browser !== "" && !!platform && platform.canSignInByPhone === true
    // What can be chosen, the best for this device first: the source's order,
    // but a phone ahead of a browser that's missing or too old.
    readonly property var _options: {
        const found = []
        for (let i = 0; i < (kinds ? kinds.length : 0); ++i) {
            const kind = kinds[i]
            if (kind === "browser") {
                const phoneFirst = _canUsePhone && (!_canBrowse || _browserOutdated)
                if (phoneFirst)
                    found.push("phone")
                if (_canBrowse)
                    found.push("browser")
                if (_canUsePhone && !phoneFirst)
                    found.push("phone")
            } else if (kind === "code" ? url !== "" : kind === "form" && _shownFields.length > 0) {
                found.push(kind)
            }
        }
        return found
    }
    // The user's choice; the first of the options until one is made.
    property string _chosen
    readonly property string _selected: _options.indexOf(_chosen) >= 0 ? _chosen
                                        : _options.length > 0 ? _options[0] : ""
    readonly property var _names: ({
        "browser": qsTr("In the browser"),
        "phone": qsTr("With an Android phone"),
        "code": qsTr("With a code"),
        "form": qsTr("Manually")
    })
    // The fields as drawn: only another list makes new inputs, which would
    // drop what's typed.
    property var _shownFields: []
    // What the phone may open, one per network of this device's, the
    // likeliest first; none while not offered. Which network the phone is on
    // isn't known: the QR code shows one, and the user can move to the next.
    property var _phoneLinks: []
    property int _phoneIndex: 0
    readonly property string _phoneLink: _phoneLinks.length > 0 ? _phoneLinks[_phoneIndex % _phoneLinks.length] : ""
    // The address in the QR code, for the user to tell the networks apart.
    readonly property string _phoneAddress: {
        const match = /^http:\/\/([^/]+)\//.exec(_phoneLink)
        return match ? match[1] : ""
    }
    property bool _phoneOffered: false
    readonly property bool _offerPhone: _selected === "phone" && visible && !busy

    function _takeFields() {
        const list = fields ? fields : []
        if (JSON.stringify(list) !== JSON.stringify(_shownFields))
            _shownFields = list
    }
    function _submit() {
        const values = {}
        for (let i = 0; i < inputs.count; ++i)
            values[_shownFields[i].name] = inputs.itemAt(i).value
        formSubmitted(values)
    }
    function _stopPhone() {
        if (_phoneOffered)
            platform.stopPhoneSignIn()
        _phoneOffered = false
        _phoneLinks = []
        _phoneIndex = 0
    }
    function _startPhone() {
        _stopPhone()
        if (!_offerPhone)
            return
        _phoneLinks = platform.startPhoneSignIn(sourceId, browser, sourceName)
        _phoneOffered = _phoneLinks.length > 0
    }

    onFieldsChanged: _takeFields()
    Component.onCompleted: {
        _takeFields()
        _startPhone()
    }
    Component.onDestruction: _stopPhone()
    onSourceIdChanged: {
        _stopPhone()
        _chosen = ""
        _shownFields = []
        _takeFields()
        Qt.callLater(_startPhone)
    }
    // Once all that changed together has.
    on_OfferPhoneChanged: Qt.callLater(_startPhone)
    onBrowserChanged: Qt.callLater(_startPhone)
    Connections {
        target: card.platform
        ignoreUnknownSignals: true
        function onPhoneSignInEnded() {
            card._phoneOffered = false
            card._phoneLinks = []
            card._phoneIndex = 0
        }
    }

    GridLayout {
        id: grid
        Layout.fillWidth: true
        // The panel's width, whatever's in it would like.
        Layout.minimumWidth: 0
        columns: card.wide && qr.visible ? 2 : 1
        columnSpacing: Theme.padding * 2
        rowSpacing: Theme.padding
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.dp(12)
            BodyText {
                text: qsTr("Sign in to see the playlists")
                font.weight: Font.DemiBold
            }
            BodyText {
                visible: text !== ""
                text: card.error
                color: Theme.danger
            }
            Flow {
                Layout.fillWidth: true
                visible: card._options.length > 1
                spacing: Theme.spacing
                Repeater {
                    model: card._options
                    Chip {
                        required property string modelData
                        required property int index
                        objectName: "authWay_" + modelData
                        text: (index === 0 ? "★ " : "") + card._names[modelData]
                        checked: card._selected === modelData
                        onClicked: {
                            card._chosen = modelData
                            checked = Qt.binding(() => card._selected === modelData)
                        }
                    }
                }
            }
            BodyText {
                objectName: "authRecommended"
                visible: card._options.length > 1
                text: qsTr("★ Recommended for this device")
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
            BodyText {
                objectName: "authNoProxy"
                visible: card._proxyInTheWay
                text: qsTr("This device's built-in browser can't go through the source's proxy.")
                color: Theme.inkSecondary
            }
            BodyText {
                visible: card._selected === "browser" && card._browserOutdated
                text: qsTr("This device's browser is old: the sign-in page may turn it away.")
                color: Theme.inkSecondary
            }
            BodyText {
                objectName: "authPhoneHint"
                visible: card._selected === "phone" && !card.busy
                text: card._phoneLink !== ""
                      ? qsTr("Scan the code with your phone's camera and sign in on the phone. Both devices have to be on one network.")
                      : qsTr("To sign in with a phone, connect this device to the phone's network — its hotspot, say — and show the code.")
            }
            BodyText {
                objectName: "authPhoneAndroidOnly"
                visible: card._selected === "phone" && !card.busy
                text: qsTr("Only an Android phone with CloudMus installed will do: an iPhone can't sign in for this device.")
                color: Theme.inkSecondary
            }
            BodyText {
                objectName: "authPhoneAddress"
                visible: card._selected === "phone" && card._phoneLink !== "" && !card.busy
                text: card._phoneLinks.length > 1
                      ? qsTr("Address in the code: %1 (network %2 of %3). If the phone can't open it, try another network.")
                            .arg(card._phoneAddress).arg(card._phoneIndex + 1).arg(card._phoneLinks.length)
                      : qsTr("Address in the code: %1").arg(card._phoneAddress)
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
            }
            TextButton {
                objectName: "authPhoneNextNetwork"
                visible: card._selected === "phone" && card._phoneLinks.length > 1 && !card.busy
                Layout.fillWidth: true
                text: qsTr("Another network")
                onClicked: card._phoneIndex = (card._phoneIndex + 1) % card._phoneLinks.length
            }
            TextButton {
                objectName: "authShowPhoneCode"
                visible: card._selected === "phone" && card._phoneLink === "" && !card.busy
                Layout.fillWidth: true
                text: qsTr("Show the QR code")
                tint: Theme.accent
                onClicked: card._startPhone()
            }
            BodyText {
                objectName: "authLink"
                visible: card._selected === "code"
                textFormat: Text.StyledText
                linkColor: Theme.accent
                text: card.url !== ""
                      ? qsTr("Open %1 on any device and enter this code:")
                            .arg("<a href=\"" + card.url + "\">" + card.url + "</a>")
                      : ""
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
            RowLayout {
                visible: card._selected === "code" && card.code !== ""
                spacing: Theme.dp(8)
                Text {
                    objectName: "authCode"
                    text: card.code
                    color: Theme.accent
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontHeadline
                    font.weight: Font.Bold
                    TapHandler { onTapped: copyCode.clicked() }
                }
                CopyButton {
                    id: copyCode
                    objectName: "copyAuthCode"
                    content: card.code
                    platform: card.platform
                }
            }
            // While a code is shown the source is waiting for it: asking again
            // would only change the code.
            TextButton {
                objectName: "signIn"
                visible: card._selected === "browser" || card._selected === ""
                Layout.fillWidth: true
                text: qsTr("Sign in")
                tint: Theme.accent
                busy: card.busy
                enabled: !card.busy
                // With nothing offered yet, it's asked for; then the ways
                // are there to choose from.
                onClicked: card._selected === "browser" ? card.browserRequested() : card.signInRequested()
            }
            // The phone's sign-in on its way to the source.
            Spinner {
                visible: card._selected === "phone" && card.busy
                Layout.alignment: Qt.AlignHCenter
            }
            BodyText {
                objectName: "authFormMessage"
                visible: card._selected === "form" && text !== ""
                textFormat: Text.StyledText
                linkColor: Theme.accent
                text: card.formMessage
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
            Repeater {
                id: inputs
                model: card._shownFields
                delegate: ColumnLayout {
                    id: input
                    required property var modelData
                    readonly property string value: modelData.multiline ? area.text : line.text
                    Layout.fillWidth: true
                    visible: card._selected === "form"
                    InputField {
                        id: line
                        objectName: "authField_" + input.modelData.name
                        visible: !input.modelData.multiline
                        placeholderText: input.modelData.label || input.modelData.name
                        echoMode: input.modelData.secret ? TextInput.Password : TextInput.Normal
                    }
                    // Long pasted text: a box that scrolls within itself.
                    ScrollView {
                        visible: input.modelData.multiline
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.dp(140)
                        TextArea {
                            id: area
                            objectName: "authArea_" + input.modelData.name
                            padding: Theme.padding
                            wrapMode: TextEdit.WrapAnywhere
                            placeholderText: input.modelData.label || qsTr("Paste here")
                            placeholderTextColor: Theme.inkTertiary
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.inkOnAccent
                            cursorDelegate: TextCursor { }
                            color: Theme.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                            background: Rectangle {
                                radius: Theme.dp(10)
                                color: Theme.surface300
                                border.width: area.activeFocus ? Theme.dp(2) : 0
                                border.color: Theme.accent
                            }
                        }
                    }
                }
            }
            TextButton {
                objectName: "authSubmit"
                visible: card._selected === "form"
                Layout.fillWidth: true
                text: qsTr("Submit")
                tint: Theme.accent
                busy: card.busy
                enabled: !card.busy
                onClicked: card._submit()
            }
        }
        QrCode {
            id: qr
            objectName: "authQr"
            readonly property real side: Math.min(card.width - card.padding * 2, Theme.dp(card.wide ? 240 : 300))
            readonly property string _content: card._selected === "code" ? card.url
                                               : card._selected === "phone" && !card.busy ? card._phoneLink : ""
            visible: _content !== ""
            text: _content
            platform: card.platform
            Layout.preferredWidth: side
            Layout.preferredHeight: side
            Layout.alignment: card.wide ? Qt.AlignTop : Qt.AlignHCenter
            // Room for the corner button, which sticks out.
            Layout.bottomMargin: Theme.dp(8)
        }
    }
}
