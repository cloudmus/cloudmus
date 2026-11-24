import QtQuick

// A drag that goes one way only, chosen once as it starts: across or up and
// down, whichever the finger had gone further when it was taken for a drag.
// Until it's lifted the other way does nothing, neither here nor in a view
// under it (`view`). What the finger had gone before that isn't jumped to: it
// comes in over a moment (`distance`).
//
//   AxisSwipe {
//       horizontal: true; vertical: true
//       accepts: (axis, scenePress) => axis === Qt.Horizontal || onSomething(scenePress)
//       onMoved: (distance) => ...
//       onReleased: (velocity) => ...
//   }
DragHandler {
    id: swipe

    // The ways it may take.
    property bool horizontal: true
    property bool vertical: false
    // Whether to take the way chosen, for where the finger came down (in the
    // scene): `(axis, scenePress) => bool`. Refused, it keeps the finger and
    // does nothing, so nothing else takes it the other way either. Unset: yes.
    property var accepts: null
    // A view that scrolls under it, the other way: still while this has the
    // finger, and the finger is left to it once it scrolls.
    property Flickable view: null
    // As `enabled`, which this sets itself.
    property bool allowed: true
    // How long what the finger had gone before comes in.
    property int catchUpDuration: 160

    // Qt.Horizontal or Qt.Vertical while it has the finger and has taken that
    // way; 0 otherwise.
    readonly property int axis: _axis > 0 ? _axis : 0
    // How far that way, from where the finger came down.
    readonly property real distance: _axis === Qt.Horizontal ? activeTranslation.x + _before.x * _catchUp
        : _axis === Qt.Vertical ? activeTranslation.y + _before.y * _catchUp : 0
    // Where it came down, in the scene.
    readonly property point scenePress: centroid.scenePressPosition

    // A way taken.
    signal began(int axis)
    signal moved(real distance)
    // Let go: how fast it was thrown that way, 0 by a finger that stopped first.
    signal released(real velocity)

    target: null
    xAxis.enabled: horizontal
    yAxis.enabled: vertical
    enabled: allowed && !(view && view.dragging)

    // 0 none yet, -1 refused.
    property int _axis: 0
    property point _before: Qt.point(0, 0)
    property real _catchUp: 1
    property real _velocity: 0
    property double _movedAt: 0
    property bool _viewWasInteractive: true

    function _choose() {
        // From the finger itself: `translation` is nothing until it's active.
        _before = Qt.point(centroid.scenePosition.x - centroid.scenePressPosition.x,
                           centroid.scenePosition.y - centroid.scenePressPosition.y)
        const way = horizontal && vertical
            ? (Math.abs(_before.x) >= Math.abs(_before.y) ? Qt.Horizontal : Qt.Vertical)
            : horizontal ? Qt.Horizontal : Qt.Vertical
        if (typeof accepts === "function" && !accepts(way, centroid.scenePressPosition))
            return -1
        return way
    }

    onActiveChanged: {
        if (active) {
            _axis = _choose()
            if (_axis <= 0)
                return
            if (view) {
                _viewWasInteractive = view.interactive
                view.cancelFlick()
                view.interactive = false
            }
            _velocity = 0
            _catchUp = 0
            catchUp.restart()
            began(_axis)
            return
        }
        catchUp.stop()
        // `axis` is still the way taken, for those told.
        if (_axis > 0) {
            if (view)
                view.interactive = _viewWasInteractive
            released(Date.now() - _movedAt < 100 ? _velocity : 0)
        }
        _axis = 0
    }
    onActiveTranslationChanged: {
        if (!active || _axis <= 0)
            return
        _velocity = _axis === Qt.Horizontal ? centroid.velocity.x : centroid.velocity.y
        _movedAt = Date.now()
        moved(distance)
    }
    on_CatchUpChanged: if (active && _axis > 0) moved(distance)

    property NumberAnimation _catchUpAnimation: NumberAnimation {
        id: catchUp
        target: swipe
        property: "_catchUp"
        from: 0
        to: 1
        duration: swipe.catchUpDuration
        easing.type: Easing.OutCubic
    }
}
