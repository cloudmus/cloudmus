import QtQuick
import QtQuick.Window

// The toasts, stacked at the bottom — the newest lowest, the older ones
// pushed up — as on the desktop. On a tall screen they span the width and
// slide in from below; on a wide one (a head unit, a phone on its side) they
// keep to the right side and slide in from there. A toast leaves the way it
// came, and the rest glide to close the gap. Fill the item you want them over.
Item {
    id: stack

    property QtObject platform
    // The same cut as Main's `narrow`.
    readonly property bool landscape: width > height * 1.1
    // Toasts in the stack, the leaving ones included until they're gone.
    readonly property int count: toastModel.count
    readonly property int maxToasts: 4
    // From the screen's edges (past the system bars), the same on every side.
    readonly property int margin: Theme.padding * 3
    readonly property int spacing: Theme.dp(8)
    readonly property real slide: landscape ? Theme.dp(96) : Theme.dp(48)
    readonly property real cardWidth: landscape
        ? Math.min(Theme.dp(420), width - margin * 2 - Theme.insetLeft - Theme.insetRight)
        : width - margin * 2 - Theme.insetLeft - Theme.insetRight
    readonly property real cardX: landscape
        ? width - margin - Theme.insetRight - cardWidth
        : margin + Theme.insetLeft
    // Above the mini player when it's over the pages, not over it.
    readonly property real bottomY: height - margin - Theme.pageInsetBottom

    property int _nextUid: 0

    function show(text, error, detail, success) {
        toastModel.append({
            "uid": _nextUid++,
            "message": text,
            "isError": error === true,
            "isSuccess": success === true,
            "details": detail === undefined ? "" : detail
        })
        // Too many: the oldest still there makes room.
        let staying = 0
        for (let i = cells.count - 1; i >= 0; --i) {
            const cell = cells.itemAt(i)
            if (cell === null || cell.leaving)
                continue
            if (++staying > maxToasts)
                cell.leave()
        }
        layout()
    }

    // Bottom-up, newest first; the leaving ones keep where they are.
    function layout() {
        let offset = 0
        for (let i = cells.count - 1; i >= 0; --i) {
            const cell = cells.itemAt(i)
            if (cell === null || cell.leaving)
                continue
            cell.offset = offset
            cell.placed = true // from now on it glides
            offset += cell.height + spacing
        }
    }

    function _remove(uid) {
        for (let i = 0; i < toastModel.count; ++i) {
            if (toastModel.get(i).uid === uid) {
                toastModel.remove(i)
                return
            }
        }
    }

    ListModel { id: toastModel }

    Repeater {
        id: cells
        model: toastModel
        delegate: Item {
            id: cell
            required property int uid
            required property string message
            required property string details
            required property bool isError
            required property bool isSuccess

            // How far its bottom sits above the stack's bottom.
            property real offset: 0
            property bool placed: false
            property bool leaving: false
            // 0 = off to the side and clear, 1 = in place.
            property real presence: 0

            function leave() {
                if (leaving)
                    return
                leaving = true
                card.stopCountdown()
                enter.stop()
                exit.start()
                stack.layout()
            }

            x: stack.cardX
            y: stack.bottomY - offset - height
            width: stack.cardWidth
            height: card.height
            opacity: presence
            transform: Translate {
                x: stack.landscape ? (1 - cell.presence) * stack.slide : 0
                y: stack.landscape ? 0 : (1 - cell.presence) * stack.slide
            }
            Behavior on offset {
                enabled: cell.placed
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }

            onHeightChanged: stack.layout() // unfolded or folded
            Component.onCompleted: enter.start()

            NumberAnimation {
                id: enter
                target: cell; property: "presence"; to: 1
                duration: 260; easing.type: Easing.OutCubic
            }
            NumberAnimation {
                id: exit
                target: cell; property: "presence"; to: 0
                duration: 200; easing.type: Easing.InCubic
                onFinished: stack._remove(cell.uid)
            }

            Toast {
                id: card
                width: parent.width
                platform: stack.platform
                message: cell.message
                details: cell.details
                isError: cell.isError
                isSuccess: cell.isSuccess
                onCloseRequested: cell.leave()
            }
        }
    }
}
