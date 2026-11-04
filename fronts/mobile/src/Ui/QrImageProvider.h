#pragma once

#include <QQuickImageProvider>

namespace Ui {

// `image://qr/<percent-encoded text>`: the text as a QR code, sharp at the size
// asked for. A head unit may have no browser, and typing an address into a
// phone in a car is no fun: signing in shows one.
class QrImageProvider : public QQuickImageProvider {
public:
    QrImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace Ui
