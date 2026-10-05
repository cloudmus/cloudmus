#pragma once

#include <QSvgRenderer>
#include <QVariantAnimation>
#include <QWidget>

namespace ViewModel {
class AudioPulse;
}

namespace Ui {

// The CloudMus logo that swells on each beat of the music. Painted from the
// SVG at the current scale (sharp, not a stretched pixmap), inside a widget
// with room for the largest swell — so growing neither clips it nor moves
// the layout around it.
class PulsingLogo : public QWidget {
    Q_OBJECT

public:
    // `side`: the logo's resting size.
    PulsingLogo(ViewModel::AudioPulse& pulse, int side, QWidget* parent = nullptr);

    // Where the logo's center is, in this widget's parent's coordinates.
    QPoint centerInParent() const { return geometry().center(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ViewModel::AudioPulse& pulse_;
    QSvgRenderer renderer_;
    int side_;
    QVariantAnimation swell_; // 0..1 after a beat, settling back to 0
};

} // namespace Ui
