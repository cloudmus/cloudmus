import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Signing in to any source, by the methods it offers (docs/protocol.md §10).
// Those this device can carry out are offered to choose from, the best one
// for it marked and chosen to begin with: in the built-in browser; on a phone
// that signs in for this device, or here in CloudMus Sign-in, a helper app
// with a modern browser (both for a device whose own browser can't, such as a
// head unit's); a code to enter on a web page, with where to enter it also as a QR
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
    // The ways are still to come (LibraryScreen::authPreparing), the source
    // starting up or being asked.
    property bool preparing: false
    property bool starting: false
    property string error
    property QtObject platform: null
    // The download of CloudMus Sign-in (ViewModel::SignInHelperDownload).
    property QtObject helper: null
    // The QR code beside the rest rather than under it.
    property bool wide: false
    signal signInRequested()
    // On screen with no way to offer: asks for them (prepareSignIn()).
    signal prepareRequested()
    signal browserRequested()
    // `browser`, in CloudMus Sign-in.
    signal helperRequested()
    signal formSubmitted(var values)

    readonly property bool _hasBrowser: browser !== "" && !!platform && platform.hasSignInBrowser === true
    // An old WebView can't be sent through a proxy, and going past the one
    // the user chose isn't an option.
    readonly property bool _proxyInTheWay: _hasBrowser && browserNeedsProxy
                                           && platform.signInBrowserTakesProxy !== true
    readonly property bool _canBrowse: _hasBrowser && !_proxyInTheWay
    readonly property bool _browserOutdated: !!platform && platform.signInBrowserOutdated === true
    readonly property bool _canUsePhone: browser !== "" && !!platform && platform.canSignInByPhone === true
    // "installed", "foreign" or "" (Platform::signInHelper).
    readonly property string _helperState: platform && platform.signInHelper ? platform.signInHelper : ""
    // Offered where the built-in browser won't do, and wherever it's there:
    // a phone has no use for fetching 100 MB.
    readonly property bool _canUseHelper: browser !== "" && !!platform && platform.canSignInByPhone === true
                                          && (_helperState !== "" || !_canBrowse || _browserOutdated)
    // What can be chosen, the best for this device first: the source's order,
    // but where the built-in browser is missing, too old or can't go through
    // the source's proxy, ahead of it the helper, then a phone.
    readonly property var _options: {
        const found = []
        for (let i = 0; i < (kinds ? kinds.length : 0); ++i) {
            const kind = kinds[i]
            if (kind === "browser") {
                const poor = !_canBrowse || _browserOutdated
                const ways = []
                if (_canBrowse)
                    ways.push("browser")
                if (_canUsePhone)
                    ways.splice(poor ? 0 : ways.length, 0, "phone")
                // Where the built-in browser won't do, the helper comes first
                // — unless it can't be put here but by hand.
                const helperFirst = _helperState === "installed"
                                    || (!!platform && platform.canInstallSignInHelper === true)
                if (_canUseHelper)
                    ways.splice(!poor ? ways.length : helperFirst || !_canUsePhone ? 0 : 1, 0, "device")
                found.push(...ways)
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
        "device": qsTr("On this device"),
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

    // Nothing to press until there's a way: it's asked for by itself, and
    // again as the source comes up.
    readonly property bool _waiting: _options.length === 0 && preparing
    function _prepare() {
        if (visible && _options.length === 0 && preparing)
            prepareRequested()
    }
    onPreparingChanged: Qt.callLater(_prepare)
    onStartingChanged: Qt.callLater(_prepare)
    onVisibleChanged: Qt.callLater(_prepare)

    // The helper's download, step by step (SignInHelperDownload::Step).
    readonly property int _helperStep: helper && helper.step ? helper.step : 0
    readonly property string _helperFile: helper && helper.file ? helper.file : ""
    // Android lets CloudMus install apps: asked again whenever the app comes
    // to the front (from Android's settings).
    property bool _mayInstall: false
    // The file is with Android's installer.
    property bool _installing: false
    property string _installError
    readonly property bool _active: Qt.application.state === Qt.ApplicationActive
    on_ActiveChanged: if (_active) _installIfReady(false)
    on_HelperStepChanged: _installIfReady(false)
    on_HelperFileChanged: _installIfReady(false)
    // The file handed over by itself once; again only when asked.
    property string _handedOver: ""
    function _installIfReady(asked) {
        if (!platform || _helperStep !== 2 || _helperFile === "" || _helperState !== "")
            return
        _mayInstall = platform.canInstallPackages()
        if (!_mayInstall || (!asked && _helperFile === _handedOver))
            return
        _handedOver = _helperFile
        _installError = ""
        _installing = true
        platform.installSignInHelper(_helperFile)
    }

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
        _installIfReady(false)
        Qt.callLater(_prepare)
    }
    Component.onDestruction: _stopPhone()
    onSourceIdChanged: {
        _stopPhone()
        _chosen = ""
        _shownFields = []
        _takeFields()
        Qt.callLater(_startPhone)
        Qt.callLater(_prepare)
    }
    // Once all that changed together has.
    on_OfferPhoneChanged: Qt.callLater(_startPhone)
    onBrowserChanged: Qt.callLater(_startPhone)
    Connections {
        target: card.platform
        ignoreUnknownSignals: true
        function onSignInHelperInstallFinished(error) {
            card._installing = false
            card._installError = error
        }
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
            // CloudMus Sign-in: there, or to be put there first.
            BodyText {
                objectName: "authDeviceHint"
                visible: card._selected === "device"
                text: card._helperState === "installed"
                      ? qsTr("Signs in on this device, in CloudMus Sign-in's browser.")
                      : card._helperState === "foreign"
                        ? qsTr("The CloudMus Sign-in installed here is signed with another key than this CloudMus. Remove it, then install the one from the same release.")
                        : qsTr("This takes CloudMus Sign-in, a separate app with a modern browser (about 100 MB).")
            }
            // Before it's put there: what that brings, and how to be rid of it.
            BodyText {
                objectName: "authDeviceWarning"
                visible: card._selected === "device" && card._helperState !== "installed"
                color: Theme.inkSecondary
                text: qsTr("Installing it adds a second app to this device, taking about 200 MB. Once signed in, you can remove it: CloudMus Settings → CloudMus Sign-in, or Android's settings → Apps. Signing in again later needs it again.")
            }
            BodyText {
                objectName: "authDeviceError"
                visible: card._selected === "device" && card._helperState === "" && text !== ""
                text: card._helperStep === 3 && card.helper.error ? card.helper.error : card._installError
                color: Theme.danger
            }
            TextButton {
                objectName: "authDeviceSignIn"
                visible: card._selected === "device" && card._helperState === "installed"
                Layout.fillWidth: true
                text: qsTr("Sign in")
                tint: Theme.accent
                busy: card.busy
                enabled: !card.busy
                onClicked: card.helperRequested()
            }
            TextButton {
                objectName: "authDeviceDownload"
                visible: card._selected === "device" && card._helperState === "" && !!card.helper
                         && card.platform.canInstallSignInHelper === true
                         && (card._helperStep === 0 || card._helperStep === 3)
                Layout.fillWidth: true
                text: qsTr("Download and install")
                tint: Theme.accent
                onClicked: {
                    card._installError = ""
                    card._handedOver = ""
                    card.helper.download()
                }
            }
            BodyText {
                objectName: "authDeviceProgress"
                visible: card._selected === "device" && card._helperState === "" && card._helperStep === 1
                text: card.helper && card.helper.progress >= 0
                      ? qsTr("Downloading… %1%").arg(Math.round(card.helper.progress * 100))
                      : qsTr("Downloading…")
            }
            TextButton {
                objectName: "authDeviceCancel"
                visible: card._selected === "device" && card._helperState === "" && card._helperStep === 1
                Layout.fillWidth: true
                text: qsTr("Cancel")
                onClicked: card.helper.cancel()
            }
            // Downloaded: Android's permission, then its installer.
            TextButton {
                objectName: "authDeviceAllow"
                visible: card._selected === "device" && card._helperState === "" && card._helperStep === 2
                         && !card._mayInstall
                Layout.fillWidth: true
                text: qsTr("Allow CloudMus to install apps")
                tint: Theme.accent
                onClicked: card.platform.requestInstallPermission()
            }
            TextButton {
                objectName: "authDeviceInstall"
                visible: card._selected === "device" && card._helperState === "" && card._helperStep === 2
                         && card._mayInstall
                Layout.fillWidth: true
                text: qsTr("Install")
                tint: Theme.accent
                busy: card._installing
                enabled: !card._installing
                onClicked: card._installIfReady(true)
            }
            BodyText {
                objectName: "authDeviceByHand"
                visible: card._selected === "device" && card._helperState !== "installed"
                // The release's file, for a device with no internet or whose
                // installer CloudMus can't use.
                text: qsTr("You can also install it yourself: the file %1 from the same release as this CloudMus, at %2.")
                          .arg("CloudMus-" + (card.platform && card.platform.version ? card.platform.version : "")
                               + (card.helper && card.helper.assetSuffix ? card.helper.assetSuffix : "-signin.apk"))
                          .arg("github.com/cloudmus/cloudmus/releases")
                color: Theme.inkSecondary
                font.pixelSize: Theme.fontSmall
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
            RowLayout {
                objectName: "authPreparing"
                visible: card._waiting
                Layout.fillWidth: true
                spacing: Theme.dp(12)
                Spinner {
                    implicitWidth: Theme.dp(32)
                    implicitHeight: Theme.dp(32)
                    running: card._waiting
                }
                BodyText {
                    Layout.fillWidth: true
                    color: Theme.inkSecondary
                    text: card.starting ? qsTr("%1 is starting…").arg(card.sourceName)
                                        : qsTr("Getting %1 ready to sign in…").arg(card.sourceName)
                }
            }
            // While a code is shown the source is waiting for it: asking again
            // would only change the code.
            TextButton {
                objectName: "signIn"
                // With no way offered and none coming, they can be asked for
                // again.
                visible: card._selected === "browser" || (card._selected === "" && !card.preparing)
                Layout.fillWidth: true
                text: card._selected === "browser" ? qsTr("Sign in") : qsTr("Try again")
                tint: Theme.accent
                busy: card.busy
                enabled: !card.busy
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
