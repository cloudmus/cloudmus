#pragma once

#include <QImage>
#include <QString>

namespace Qr {

// `text` as a QR code the way both fronts show it, for another device's camera
// (signing in): rounded modules flowing together, on a white rounded square of
// `side` pixels (at least a pixel per module). Null if it's too long for a code.
QImage picture(const QString& text, int side);

} // namespace Qr
