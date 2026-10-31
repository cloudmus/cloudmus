import QtQuick

// A list that scrolls like Android's own: a fast swipe flies far, the ends
// stop it without a bounce. Every list of the front is one of these.
ListView {
    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.StopAtBounds
    maximumFlickVelocity: Theme.flickMaxVelocity
    flickDeceleration: Theme.flickDeceleration
    pixelAligned: true
}
