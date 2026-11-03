import QtQuick

// A list that scrolls like Android's own: a fast swipe flies far, the ends
// stop it without a bounce, and its items dissolve at an edge with more beyond
// it. Every list of the front is one of these.
ListView {
    id: list

    // What's behind it, for the items to dissolve into.
    property alias fadeColor: edgeFade.color

    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.StopAtBounds
    maximumFlickVelocity: Theme.flickMaxVelocity
    flickDeceleration: Theme.flickDeceleration
    pixelAligned: true

    ScrollEdgeFade {
        id: edgeFade
        // The view's, not its content's: it stays put as that scrolls.
        parent: list
        view: list
    }
}
