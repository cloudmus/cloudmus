#include "ThemeCrossfade.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

namespace Ui {

namespace {

constexpr int kFadeMs = 500;

// The old look of one window, fading out over its new one. A child of the
// window, so it moves and goes away with it; transparent for input, so a
// click during the fade reaches what's under it.
class Snapshot : public QWidget {
public:
    Snapshot(QWidget* window, QPixmap pixmap)
        : QWidget(window)
        , pixmap_(std::move(pixmap))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(window->rect());
        raise();
        show();
    }

    void fadeOut()
    {
        auto* animation = new QVariantAnimation(this);
        animation->setDuration(kFadeMs);
        animation->setEasingCurve(QEasingCurve::InOutCubic);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            opacity_ = value.toReal();
            update();
        });
        connect(animation, &QVariantAnimation::finished, this, &QObject::deleteLater);
        animation->start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setOpacity(opacity_);
        painter.drawPixmap(rect(), pixmap_);
    }

private:
    QPixmap pixmap_;
    qreal opacity_ = 1.0;
};

} // namespace

void crossfadeThemeChange(const std::function<void()>& change)
{
    QList<Snapshot*> snapshots;
    for (QWidget* window : QApplication::topLevelWidgets()) {
        // Popups and tooltips are gone at the next click anyway.
        if (!window->isVisible() || window->windowType() == Qt::Popup || window->windowType() == Qt::ToolTip)
            continue;
        snapshots.append(new Snapshot(window, window->grab()));
    }
    change();
    for (Snapshot* snapshot : snapshots)
        snapshot->fadeOut();
}

} // namespace Ui
