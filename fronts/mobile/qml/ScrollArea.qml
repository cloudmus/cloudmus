import QtQuick

// ScrollList's physics for content that isn't a list.
Flickable {
    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.StopAtBounds
    maximumFlickVelocity: Theme.flickMaxVelocity
    flickDeceleration: Theme.flickDeceleration
    pixelAligned: true
}
