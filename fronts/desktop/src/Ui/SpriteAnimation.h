#pragma once

#include <QPixmap>
#include <QTimer>
#include <QWidget>

namespace Ui {

// A loop of frames from one strip of them side by side (a sprite sheet's
// row), each shown `frameMs`: the About dialog's waving AI agents. Runs only
// while shown.
class SpriteAnimation : public QWidget {
    Q_OBJECT

public:
    // `size`: what it's drawn at. `pixelArt`: scaled without smoothing, each
    // pixel a sharp block.
    SpriteAnimation(
        const QString& strip, int frameCount, int frameMs, QSize size, bool pixelArt, QWidget* parent = nullptr);

    QSize sizeHint() const override { return size_; }
    int frame() const { return frame_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QPixmap strip_;
    int frameCount_;
    QSize size_;
    bool pixelArt_;
    int frame_ = 0;
    QTimer timer_;
};

} // namespace Ui
