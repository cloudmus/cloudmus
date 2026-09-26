#include "WindowGlass.h"

#include <QGuiApplication>
#include <QLibrary>
#include <QPainterPath>
#include <QWidget>
#include <QWindow>

namespace Integration::WindowGlass {

namespace {

#if defined(Q_OS_LINUX)
// KWindowEffects::enableBlurBehind(QWindow*, bool, const QRegion&) — a
// static member function, so a plain function pointer to its mangled name.
using EnableBlurBehindFn = void (*)(QWindow*, bool, const QRegion&);

EnableBlurBehindFn resolveKdeBlur()
{
    // Only KWin implements the protocol KWindowEffects speaks; elsewhere
    // the call would silently do nothing and leave an unblurred see-through
    // window.
    if (!qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QStringLiteral("KDE"), Qt::CaseInsensitive))
        return nullptr;
    // Leaked on purpose: resolved function pointers must stay valid for the
    // whole run.
    auto* library = new QLibrary(QStringLiteral("KF6WindowSystem"), 6);
    if (!library->load())
        return nullptr;
    return reinterpret_cast<EnableBlurBehindFn>(
        library->resolve("_ZN14KWindowEffects16enableBlurBehindEP7QWindowbRK7QRegion"));
}

EnableBlurBehindFn kdeBlur()
{
    static const EnableBlurBehindFn fn = resolveKdeBlur();
    return fn;
}
#endif

} // namespace

bool available()
{
#if defined(Q_OS_LINUX)
    return kdeBlur() != nullptr;
#else
    return false;
#endif
}

void enableBlurBehind(QWidget* window, const QRegion& region)
{
    if (window == nullptr || !available())
        return;
    // The native window has to exist for the compositor to be told about it.
    window->winId();
    QWindow* handle = window->windowHandle();
    if (handle == nullptr)
        return;
#if defined(Q_OS_LINUX)
    kdeBlur()(handle, true, region);
#else
    Q_UNUSED(region);
#endif
}

QRegion roundedRegion(const QRect& rect, qreal radius)
{
    // Inset: a region is whole pixels, so its rounded corners are stepped,
    // and a fractional display scale rounds it further — at the panel's
    // own size, bits of blur stuck out past its antialiased edge. Kept a
    // pixel inside, the panel's edge covers it. The radius shrinks by the
    // same pixel so the corners follow the panel's at that same distance
    // (the same radius left a visibly wider gap at the corners).
    constexpr int kInset = 1;
    QPainterPath path;
    const qreal innerRadius = qMax<qreal>(0, radius - kInset);
    path.addRoundedRect(QRectF(rect.adjusted(kInset, kInset, -kInset, -kInset)), innerRadius, innerRadius);
    return QRegion(path.toFillPolygon().toPolygon());
}

} // namespace Integration::WindowGlass
