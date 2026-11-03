import QtQuick

// ScrollList's physics and edges for content that isn't a list.
Flickable {
    id: area

    // What's behind it, for the content to dissolve into.
    property alias fadeColor: edgeFade.color

    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.StopAtBounds
    maximumFlickVelocity: Theme.flickMaxVelocity
    flickDeceleration: Theme.flickDeceleration
    pixelAligned: true

    ScrollEdgeFade {
        id: edgeFade
        // The view's, not its content's: it stays put as that scrolls.
        parent: area
        view: area
    }
}
