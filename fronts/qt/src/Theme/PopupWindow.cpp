#include "PopupWindow.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "Shadow.h"
#include "Tokens.h"
#include "WindowGlass.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Theme {

namespace {

constexpr char kClickThroughProperty[] = "cloudmusClickThrough";
constexpr char kShadowWindowProperty[] = "cloudmusShadowWindow";

#ifdef Q_OS_WIN
// A click-through Native popup: WM_NCHITTEST answers HTTRANSPARENT, which
// hands the mouse to the window under it — ours, same thread.
class ClickThroughFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
    {
        if (eventType != "windows_generic_MSG")
            return false;
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message != WM_NCHITTEST)
            return false;
        const QWidget* widget = QWidget::find(reinterpret_cast<WId>(msg->hwnd));
        if (widget == nullptr || !widget->property(kClickThroughProperty).toBool())
            return false;
        *result = HTTRANSPARENT;
        return true;
    }
};

void installClickThroughFilter()
{
    static ClickThroughFilter* filter = nullptr;
    if (filter == nullptr) {
        filter = new ClickThroughFilter; // lives as long as the app
        QCoreApplication::instance()->installNativeEventFilter(filter);
    }
}
#endif

// A Clipped popup's shadow: its own window, just under the popup, a
// reach bigger on every side, following it — shown, hidden, moved, resized
// and faded with it. Not blurred: only the popup's window is.
class ShadowWindow : public QWidget {
public:
    explicit ShadowWindow(QWidget* target)
        : QWidget(nullptr,
              Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
        , target_(target)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        target->installEventFilter(this);
        connect(target, &QObject::destroyed, this, &QObject::deleteLater);
    }

    void setUp(int radius, const PopupShadow& shadow)
    {
        radius_ = radius;
        shadow_ = shadow;
        follow();
    }

    void setOpacity(qreal opacity)
    {
        opacity_ = opacity;
        update();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == target_) {
            switch (event->type()) {
                case QEvent::Show:
                case QEvent::Move:
                case QEvent::Resize:
                    follow();
                    break;
                case QEvent::Hide:
                    hide();
                    break;
                default:
                    break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(opacity_);
        const int reach = shadow_.reach;
        paintSoftShadow(
            &painter, rect().adjusted(reach, reach, -reach, -reach), reach, shadow_.offsetY, shadow_.maxAlpha, radius_);
    }

private:
    void follow()
    {
        // Not yet when set up before the popup shows: its show event comes.
        if (!target_ || !target_->isVisible())
            return;
        const int reach = shadow_.reach;
        setGeometry(target_->geometry().adjusted(-reach, -reach, reach, reach));
        if (isHidden())
            show();
        // Right under the popup, once both windows are up.
        QTimer::singleShot(0, this, [this]() {
#ifdef Q_OS_WIN
            if (target_ && target_->isVisible() && isVisible()) {
                SetWindowPos(reinterpret_cast<HWND>(winId()), reinterpret_cast<HWND>(target_->winId()), 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
#endif
        });
    }

    QPointer<QWidget> target_;
    int radius_ = 0;
    PopupShadow shadow_ { 0, 0, 0 };
    qreal opacity_ = 1.0;
};

ShadowWindow* shadowWindowOf(const QWidget* popup)
{
    return dynamic_cast<ShadowWindow*>(popup->property(kShadowWindowProperty).value<QObject*>());
}

} // namespace

PopupLook popupLook()
{
    if (Integration::WindowGlass::nativePopups())
        return PopupLook::Native;
    if (glassEnabled() && Integration::WindowGlass::fallbackBlur())
        return PopupLook::Clipped;
    return PopupLook::Painted;
}

int popupShadowMargin(int reach) { return popupLook() == PopupLook::Painted ? reach : 0; }

Qt::WindowFlags popupWindowFlags(Qt::WindowFlags flags)
{
    if (popupLook() == PopupLook::Native)
        return flags & ~(Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);
    return flags | Qt::NoDropShadowWindowHint;
}

void preparePopup(QWidget* popup, bool clickThrough)
{
    if (popupLook() != PopupLook::Native)
        return;
    // DWM's shadow comes with CS_DROPSHADOW, which Qt gives Qt::Popup
    // windows by itself — a Qt::ToolTip one only when asked.
    popup->setProperty("_q_windowsDropShadow", true);
#ifdef Q_OS_WIN
    if (clickThrough) {
        popup->setProperty(kClickThroughProperty, true);
        installClickThroughFilter();
    }
#else
    Q_UNUSED(clickThrough);
#endif
}

PopupFade popupFade()
{
    switch (popupLook()) {
        case PopupLook::Native:
            return PopupFade::Window;
        case PopupLook::Painted:
            // No blur protocol has an opacity: painted fading left the blur
            // at full strength until the popup hid, then it vanished at once.
            if (glassEnabled() && Integration::WindowGlass::support() == Integration::WindowGlass::Support::Blur)
                return PopupFade::None;
            break;
        case PopupLook::Clipped:
            break;
    }
    return PopupFade::Painted;
}

void setUpPopup(QWidget* popup, const QRect& panel, int radius, const PopupShadow& shadow)
{
    switch (popupLook()) {
        case PopupLook::Native:
            Integration::WindowGlass::setUpNativePopup(popup, radius, glassEnabled());
            break;
        case PopupLook::Clipped: {
            Integration::WindowGlass::enableBlurBehindPanel(popup, panel, radius);
            ShadowWindow* window = shadowWindowOf(popup);
            if (window == nullptr) {
                window = new ShadowWindow(popup);
                popup->setProperty(kShadowWindowProperty, QVariant::fromValue<QObject*>(window));
            }
            window->setUp(radius, shadow);
            break;
        }
        case PopupLook::Painted:
            if (glassEnabled())
                Integration::WindowGlass::enableBlurBehindPanel(popup, panel, radius);
            break;
    }
}

void setPopupOpacity(QWidget* popup, qreal opacity)
{
    if (popupFade() == PopupFade::Window)
        popup->setWindowOpacity(opacity);
    if (ShadowWindow* window = shadowWindowOf(popup))
        window->setOpacity(opacity);
}

} // namespace Theme
