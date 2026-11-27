#pragma once

#include <QQuickImageProvider>

namespace Ui {

// `image://qr/<percent-encoded text>`: the text as a QR code of round dots on a
// white rounded square, sharp at the size asked for. Views don't use it
// directly: QrCode.qml is the code as shown.
class QrImageProvider : public QQuickImageProvider {
public:
    QrImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace Ui
