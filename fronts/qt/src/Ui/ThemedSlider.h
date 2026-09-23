#pragma once

#include <QSlider>

class QVariantAnimation;

namespace Ui {

// A horizontal QSlider that paints itself from Theme::palette() instead of
// relying on QSS — QSS can't animate anything, and binding the handle's
// visibility to the widget's own hover state needed a dynamic-property
// re-polish hack. All input handling (dragging, click-to-position via
// Theme::CloudMusStyle's SH_Slider_AbsoluteSetButtons, keyboard, wheel) is
// still QSlider's own; only the visuals are replaced. The groove and
// handle positions come from the style's own subControlRect(), so what's
// painted always lines up with what QSlider hit-tests against.
//
// Hovering (or dragging) animates the slider into its "engaged" look: the
// groove and fill brighten, and an OnHover handle grows in from nothing.
class ThemedSlider : public QSlider {
    Q_OBJECT

public:
    // Accent: accent-colored fill and handle (e.g. the seek bar).
    // Neutral: ink-colored fill and handle (e.g. the volume bar).
    enum class Scheme {
        Accent,
        Neutral
    };

    // Always: the handle is shown at rest too, so the current value reads
    // at a glance. OnHover: only shown while hovered or dragged.
    enum class HandleVisibility {
        Always,
        OnHover
    };

    explicit ThemedSlider(Scheme scheme, HandleVisibility handleVisibility, QWidget* parent = nullptr);

    void setScheme(Scheme scheme);
    void setHandleVisibility(HandleVisibility handleVisibility);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void refreshEngaged();

    Scheme scheme_;
    HandleVisibility handleVisibility_;
    // 0 = at rest, 1 = hovered/dragged; animated between the two.
    qreal engagedProgress_ = 0.0;
    QVariantAnimation* engagedAnim_ = nullptr;
};

} // namespace Ui
