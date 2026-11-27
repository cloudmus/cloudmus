#pragma once

#include <QAbstractButton>
#include <QPixmap>
#include <QPointer>

class QVariantAnimation;

namespace Ui {

class QrOverlay;

// A text as a QR code (Qr::picture, as on mobile) for another device's camera.
// A click grows it out of its place over the whole of its window, and another,
// or Escape, takes it back. All of that is the widget's own, wherever it's put.
class QrCodeView : public QAbstractButton {
    Q_OBJECT

public:
    explicit QrCodeView(QWidget* parent = nullptr);
    ~QrCodeView() override;

    // What the code says; empty shows nothing.
    void setContent(const QString& content);
    const QString& content() const { return content_; }
    // Over the window, from the click until it starts back.
    bool isExpanded() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Where the code sits in this widget; the corner badge sticks out of it.
    QRect pictureRect() const;
    void expand();
    void collapse();

    QString content_;
    // The code as last drawn: again only for another text, size or screen
    // density, not at each repaint (scrolling).
    QPixmap picture_;
    qreal pressScale_ = 1.0;
    QVariantAnimation* press_ = nullptr;
    QPointer<QrOverlay> overlay_;
};

} // namespace Ui
