#pragma once

#include <QPointer>
#include <QSlider>

#include <functional>

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

    // Turns such a slider's value bubble on: while the handle is dragged,
    // a small label with `format(value)` floats above it, fading/popping
    // in on press and out on release. An empty function turns it off.
    using BubbleFormatter = std::function<QString(int value)>;
    void setValueBubble(BubbleFormatter format);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void refreshEngaged();
    // Handle center in this widget's coordinates — shared by paintEvent()
    // and the bubble so the bubble's pointer sits exactly on the handle.
    QPointF handleCenter() const;
    void showBubble();
    void updateBubble();
    void hideBubble();

    Scheme scheme_;
    HandleVisibility handleVisibility_;
    // 0 = at rest, 1 = hovered/dragged; animated between the two.
    qreal engagedProgress_ = 0.0;
    QVariantAnimation* engagedAnim_ = nullptr;

    BubbleFormatter bubbleFormat_;
    // A child of window(), not of this slider: it floats above the slider,
    // outside its own (handle-high) bounds. QPointer — the window owns it.
    QPointer<QWidget> bubble_;
};

} // namespace Ui
