#pragma once

#include <QSvgRenderer>
#include <QVariantAnimation>
#include <QWidget>

namespace ViewModel {
class AudioPulse;
class LogoSound;
}

namespace Ui {

// The CloudMus logo that swells on each beat of the music. Painted from the
// SVG at the current scale (sharp, not a stretched pixmap), inside a widget
// with room for the largest swell — so growing neither clips it nor moves
// the layout around it. A click plays the logo's sound, and it swells hard
// on each of the sound's strikes.
class PulsingLogo : public QWidget {
    Q_OBJECT

public:
    // `side`: the logo's resting size.
    PulsingLogo(ViewModel::AudioPulse& pulse, ViewModel::LogoSound& sound, int side, QWidget* parent = nullptr);

    // Where the logo's center is, in this widget's parent's coordinates.
    QPoint centerInParent() const { return geometry().center(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    ViewModel::AudioPulse& pulse_;
    ViewModel::LogoSound& sound_;
    QSvgRenderer renderer_;
    int side_;
    QVariantAnimation swell_; // 0..1 after a beat, settling back to 0
    QVariantAnimation strike_; // the same after a strike of the logo's sound
};

} // namespace Ui
