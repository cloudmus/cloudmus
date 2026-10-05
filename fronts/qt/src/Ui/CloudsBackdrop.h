#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace ViewModel {
class AudioPulse;
}

namespace Ui {

// Faint clouds, in the logo's shape, flying out of the logo toward the
// viewer behind a dialog's content. They swell with the music's loudness,
// rush forward on its hits, and the vanishing point leans toward the
// mouse. Runs only while shown, and holds the AudioPulse only then.
class CloudsBackdrop : public QWidget {
    Q_OBJECT

public:
    // `origin`: the widget the clouds fly out of (its center is the
    // vanishing point and where the glow sits). A sibling of this one.
    CloudsBackdrop(ViewModel::AudioPulse& pulse, QWidget* origin, QWidget* parent);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Cloud {
        QPointF direction; // where it heads on screen, in half-widths
        qreal depth; // 1 far (at the vanishing point) .. 0 near (flown past)
        bool flipped;
    };

    void tick();
    void respawn(Cloud& cloud, bool anywhere);
    void rebuildSprite();
    QPointF vanishingPoint() const;

    ViewModel::AudioPulse& pulse_;
    QPointer<QWidget> origin_;
    QPixmap sprite_;
    QVector<Cloud> clouds_;
    QTimer timer_;
    QElapsedTimer clock_;
    qreal time_ = 0;
    qreal rush_ = 0; // 0..1 after a beat, falling
    qreal flash_ = 0; // the glow's flash, same
    QPointF lean_; // smoothed mouse offset, -1..1
};

} // namespace Ui
