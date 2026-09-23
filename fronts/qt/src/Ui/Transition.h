#pragma once

#include <QEasingCurve>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QPainter;
class QPointF;

namespace Ui {

// How a layer is drawn at a given moment. {1, 1} is the resting state a
// layer settles into once it has fully entered.
struct LayerState {
    qreal opacity = 1.0;
    qreal scale = 1.0;
};

// One direction of a transition. An enter effect animates a new layer
// FROM `state` into the resting state; an exit effect animates a leaving
// layer from wherever it currently is (normally resting, but possibly
// still mid-entrance) TO `state` — so a layer retired halfway through its
// own entrance leaves smoothly instead of jumping first.
struct TransitionEffect {
    int durationMs = 250;
    QEasingCurve easing = QEasingCurve::OutCubic;
    LayerState state;
};

LayerState lerp(const LayerState& from, const LayerState& to, qreal t);

// Sets the painter's opacity and scales around `origin` (typically the
// layer rect's center). Call between painter.save() and painter.restore().
void applyLayerState(QPainter& painter, const LayerState& state, const QPointF& origin);

// Animated replacement of one visual element of a custom-painted widget
// (a background, a cover, a line of text, ...). Holds the current value
// plus any values still animating out, each with its own animation, so
// rapid successive changes overlap cleanly instead of cutting each other
// off. The widget owns one of these per element, calls show()/hide() when
// the element's content changes, and draws every live layer from its
// paintEvent() via forEach(); the transition repaints `target` on every
// animation frame by itself.
//
// Values are copied into the layer, so a leaving layer keeps drawing its
// own old content (and old geometry, if T carries a rect) regardless of
// what the widget shows next — update the current value in place through
// current() for changes that should NOT animate (a relayout, a re-render
// at a new size).
template <typename T>
class LayerTransition {
public:
    LayerTransition(QWidget* target, TransitionEffect enter, TransitionEffect exit)
        : target_(target)
        , enter_(std::move(enter))
        , exit_(std::move(exit))
    {
    }

    LayerTransition(const LayerTransition&) = delete;
    LayerTransition& operator=(const LayerTransition&) = delete;

    // The current value (if any) starts leaving; `value` enters.
    void show(T value, bool animate = true)
    {
        retireCurrent(animate);
        Layer layer { std::move(value), nullptr, enter_.state, LayerState { } };
        if (animate)
            layer.anim = startAnimation(enter_, /*leaving=*/false);
        else
            layer.from = layer.to;
        current_.emplace(std::move(layer));
        target_->update();
    }

    // The current value (if any) starts leaving; nothing replaces it.
    void hide(bool animate = true)
    {
        retireCurrent(animate);
        target_->update();
    }

    T* current() { return current_ ? &current_->value : nullptr; }
    const T* current() const { return current_ ? &current_->value : nullptr; }

    // Calls fn(const T&, LayerState) for every live layer, bottom to top:
    // leaving layers oldest first, then the current one.
    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const Layer& layer : leaving_)
            fn(layer.value, stateOf(layer));
        if (current_)
            fn(current_->value, stateOf(*current_));
    }

private:
    struct Layer {
        T value;
        std::unique_ptr<QVariantAnimation> anim;
        LayerState from;
        LayerState to;
    };

    static LayerState stateOf(const Layer& layer)
    {
        if (!layer.anim)
            return layer.to;
        return lerp(layer.from, layer.to, layer.anim->currentValue().toReal());
    }

    void retireCurrent(bool animate)
    {
        if (!current_)
            return;
        Layer layer = std::move(*current_);
        current_.reset();
        if (!animate)
            return;
        layer.from = stateOf(layer);
        layer.to = exit_.state;
        layer.anim = startAnimation(exit_, /*leaving=*/true);
        leaving_.push_back(std::move(layer));
    }

    std::unique_ptr<QVariantAnimation> startAnimation(const TransitionEffect& effect, bool leaving)
    {
        auto anim = std::make_unique<QVariantAnimation>();
        anim->setDuration(effect.durationMs);
        anim->setEasingCurve(effect.easing);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        QVariantAnimation* raw = anim.get();
        QObject::connect(raw, &QVariantAnimation::valueChanged, target_, [this]() { target_->update(); });
        QObject::connect(raw, &QVariantAnimation::finished, target_, [this, raw, leaving]() { finish(raw, leaving); });
        anim->start();
        return anim;
    }

    // The finished animation is the sender of the signal being handled,
    // so it's handed off to deleteLater() rather than destroyed right here.
    void finish(QVariantAnimation* anim, bool leaving)
    {
        if (leaving) {
            const auto it = std::find_if(
                leaving_.begin(), leaving_.end(), [anim](const Layer& layer) { return layer.anim.get() == anim; });
            if (it != leaving_.end()) {
                it->anim.release()->deleteLater();
                leaving_.erase(it);
            }
        } else if (current_ && current_->anim.get() == anim) {
            current_->from = current_->to;
            current_->anim.release()->deleteLater();
        }
        target_->update();
    }

    QWidget* target_;
    TransitionEffect enter_;
    TransitionEffect exit_;
    std::optional<Layer> current_;
    std::vector<Layer> leaving_;
};

} // namespace Ui
