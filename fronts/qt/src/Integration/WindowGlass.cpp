#include "WindowGlass.h"

#include <QGuiApplication>
#include <QHash>
#include <QLibrary>
#include <QLoggingCategory>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cstring>
#include <functional>
#include <vector>

#if defined(CLOUDMUS_BLUR_WAYLAND)
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

#include "ext-background-effect-v1-client-protocol.h"
#include "org-kde-kwin-blur-client-protocol.h"
#endif

#if defined(CLOUDMUS_BLUR_X11)
#include <xcb/xcb.h>
#endif

#if defined(Q_OS_WIN)
#include <QOperatingSystemVersion>
#include <QtMath>

#include <windows.h>

#include <dwmapi.h>
#endif

namespace Integration::WindowGlass {

namespace {

Q_LOGGING_CATEGORY(lcGlass, "cloudmus.integration.glass")

// KWin's own ways to ask for blur (its X11 property, KF6WindowSystem) are
// worth trying only there; elsewhere they'd just be ignored.
bool isKde()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
}

bool isWayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }
bool isX11() { return QGuiApplication::platformName() == QLatin1String("xcb"); }

// A way to ask the compositor for blur: apply() sets `region` (window coordinates;
// the whole window if empty) behind `window`, whose native window exists.
class Backend {
public:
    virtual ~Backend() = default;
    virtual void apply(QWindow* window, const QRegion& region) = 0;
    // See enableBlurBehindPanel().
    virtual void applyPanel(QWindow* window, const QRect& panel, qreal radius)
    {
        apply(window, roundedRegion(panel, radius));
    }
    // See blurClipsWindow().
    virtual bool clipsWindow() const { return false; }
};

#if defined(CLOUDMUS_BLUR_WAYLAND)
// Wayland: the compositor's blur protocol, spoken here directly so no
// desktop library has to load into this process (KDE's, built against the
// system's Qt, won't load next to the AppImage's own). Either the
// standard ext_background_effect_v1 — when the compositor says it can blur
// — or, before it, KWin's own org_kde_kwin_blur (what KWindowEffects
// speaks). The surface comes from Qt's platform plugin; our objects live
// on an event queue of our own, so binding the globals doesn't dispatch
// Qt's events out of turn.
class WaylandBackend : public Backend {
public:
    static WaylandBackend* create()
    {
        auto* app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (app == nullptr || app->display() == nullptr)
            return nullptr;
        auto* backend = new WaylandBackend(app->display(), app->compositor());
        if (backend->effectManager_ == nullptr && backend->kdeManager_ == nullptr) {
            delete backend;
            return nullptr;
        }
        return backend;
    }

    void apply(QWindow* window, const QRegion& region) override
    {
        auto* surface = static_cast<wl_surface*>(
            QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window));
        if (surface == nullptr)
            return;
        Blur& blur = blurs_[window];
        if (blur.surface != surface) {
            // A new surface (the window was hidden and shown again): the
            // old blur object went with the old one.
            release(blur);
            blur.surface = surface;
            if (effectManager_ != nullptr) {
                blur.effect = ext_background_effect_manager_v1_get_background_effect(effectManager_, surface);
                wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(blur.effect), queue_);
            } else {
                blur.kde = org_kde_kwin_blur_manager_create(kdeManager_, surface);
                wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(blur.kde), queue_);
            }
            QObject::connect(window, &QObject::destroyed, [this, window]() { forget(window); });
        }
        // The whole window if empty: a null region means that to KWin's
        // protocol, but no blur at all to the standard one — there, a
        // rectangle bigger than any window, which the compositor clips to
        // the surface, so it still covers it after a resize.
        wl_region* area = nullptr;
        if (!region.isEmpty() || blur.effect != nullptr) {
            area = wl_compositor_create_region(compositor_);
            if (region.isEmpty())
                wl_region_add(area, 0, 0, kWholeSurface, kWholeSurface);
            for (const QRect& rect : region)
                wl_region_add(area, rect.x(), rect.y(), rect.width(), rect.height());
        }
        if (blur.effect != nullptr) {
            ext_background_effect_surface_v1_set_blur_region(blur.effect, area);
        } else {
            org_kde_kwin_blur_set_region(blur.kde, area);
            org_kde_kwin_blur_commit(blur.kde);
        }
        if (area != nullptr)
            wl_region_destroy(area);
        // Takes effect with the surface's next commit — enableBlurBehind()
        // repaints the widget to make one.
        wl_display_flush(display_);
    }

private:
    static constexpr int32_t kWholeSurface = 1 << 20;

    // One of the two, per the protocol in use.
    struct Blur {
        wl_surface* surface = nullptr;
        ext_background_effect_surface_v1* effect = nullptr;
        org_kde_kwin_blur* kde = nullptr;
    };

    WaylandBackend(wl_display* display, wl_compositor* compositor)
        : display_(display)
        , compositor_(compositor)
        , queue_(wl_display_create_queue(display))
    {
        wl_registry* registry = wl_display_get_registry(display_);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(registry), queue_);
        static const wl_registry_listener listener = {
            [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
                auto* self = static_cast<WaylandBackend*>(data);
                if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
                    self->effectManager_ = static_cast<ext_background_effect_manager_v1*>(
                        wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
                    static const ext_background_effect_manager_v1_listener effectListener = {
                        [](void* data, ext_background_effect_manager_v1*, uint32_t flags) {
                            static_cast<WaylandBackend*>(data)->effectCapabilities_ = flags;
                        },
                    };
                    ext_background_effect_manager_v1_add_listener(self->effectManager_, &effectListener, self);
                } else if (std::strcmp(interface, org_kde_kwin_blur_manager_interface.name) == 0) {
                    self->kdeManager_ = static_cast<org_kde_kwin_blur_manager*>(
                        wl_registry_bind(registry, name, &org_kde_kwin_blur_manager_interface, 1));
                }
            },
            [](void*, wl_registry*, uint32_t) { },
        };
        wl_registry_add_listener(registry, &listener, this);
        // The globals, then what the bound ones say about themselves.
        wl_display_roundtrip_queue(display_, queue_);
        wl_display_roundtrip_queue(display_, queue_);
        wl_registry_destroy(registry);
        // Advertised but unable to blur (e.g. KWin's blur effect turned
        // off): no use — though the capability can come back later, we
        // only look once, like the rest of the app does.
        if (effectManager_ != nullptr
            && (effectCapabilities_ & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) == 0) {
            ext_background_effect_manager_v1_destroy(effectManager_);
            effectManager_ = nullptr;
        }
        qCDebug(lcGlass) << "Wayland blur via"
                         << (effectManager_ != nullptr       ? "ext_background_effect_v1"
                                    : kdeManager_ != nullptr ? "org_kde_kwin_blur"
                                                             : "nothing");
    }

    void release(Blur& blur)
    {
        if (blur.effect != nullptr)
            ext_background_effect_surface_v1_destroy(blur.effect);
        if (blur.kde != nullptr)
            org_kde_kwin_blur_release(blur.kde);
        blur.effect = nullptr;
        blur.kde = nullptr;
    }

    void forget(QWindow* window)
    {
        Blur blur = blurs_.take(window);
        release(blur);
    }

    wl_display* display_;
    wl_compositor* compositor_;
    wl_event_queue* queue_;
    ext_background_effect_manager_v1* effectManager_ = nullptr;
    uint32_t effectCapabilities_ = 0;
    org_kde_kwin_blur_manager* kdeManager_ = nullptr;
    QHash<QWindow*, Blur> blurs_;
};
#endif

#if defined(CLOUDMUS_BLUR_X11)
// X11: KWin's _KDE_NET_WM_BLUR_BEHIND_REGION window property — rectangles
// (x, y, width, height) in device pixels; present but empty, the whole
// window.
class X11Backend : public Backend {
public:
    static X11Backend* create()
    {
        auto* app = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (app == nullptr || app->connection() == nullptr)
            return nullptr;
        return new X11Backend(app->connection());
    }

    void apply(QWindow* window, const QRegion& region) override
    {
        const qreal scale = window->devicePixelRatio();
        std::vector<uint32_t> data;
        for (const QRect& rect : region) {
            data.push_back(uint32_t(qRound(rect.x() * scale)));
            data.push_back(uint32_t(qRound(rect.y() * scale)));
            data.push_back(uint32_t(qRound(rect.width() * scale)));
            data.push_back(uint32_t(qRound(rect.height() * scale)));
        }
        xcb_change_property(connection_, XCB_PROP_MODE_REPLACE, xcb_window_t(window->winId()), atom_, XCB_ATOM_CARDINAL,
            32, uint32_t(data.size()), data.data());
        xcb_flush(connection_);
    }

private:
    explicit X11Backend(xcb_connection_t* connection)
        : connection_(connection)
    {
        const char name[] = "_KDE_NET_WM_BLUR_BEHIND_REGION";
        xcb_intern_atom_reply_t* reply
            = xcb_intern_atom_reply(connection_, xcb_intern_atom(connection_, false, sizeof(name) - 1, name), nullptr);
        atom_ = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
        free(reply);
    }

    xcb_connection_t* connection_;
    xcb_atom_t atom_ = XCB_ATOM_NONE;
};
#endif

#if defined(Q_OS_LINUX)
// KWindowEffects::enableBlurBehind(QWindow*, bool, const QRegion&) from
// the system's KF6WindowSystem, loaded at runtime — for a build without
// the native backends above (no Qt private headers to build them with).
// Only loads next to the Qt it was built against, i.e. not in the
// AppImage: its Wayland plugin uses Qt's private API.
class KdeLibraryBackend : public Backend {
    using EnableBlurBehindFn = void (*)(QWindow*, bool, const QRegion&);

public:
    static KdeLibraryBackend* create()
    {
        // Leaked on purpose: the resolved function must stay valid for the
        // whole run.
        auto* library = new QLibrary(QStringLiteral("KF6WindowSystem"), 6);
        if (!library->load()) {
            qCDebug(lcGlass) << "KF6WindowSystem didn't load:" << library->errorString();
            return nullptr;
        }
        auto fn = reinterpret_cast<EnableBlurBehindFn>(
            library->resolve("_ZN14KWindowEffects16enableBlurBehindEP7QWindowbRK7QRegion"));
        return fn != nullptr ? new KdeLibraryBackend(fn) : nullptr;
    }

    void apply(QWindow* window, const QRegion& region) override { fn_(window, true, region); }

private:
    explicit KdeLibraryBackend(EnableBlurBehindFn fn)
        : fn_(fn)
    {
    }
    EnableBlurBehindFn fn_;
};
#endif

#if defined(Q_OS_WIN)
// Windows: DWM blurs behind a whole window only, no region — a popup's
// window is clipped to its panel instead (a window region), which the
// accent's blur follows. Windows 11 22H2 and later: the documented system backdrop,
// acrylic, behind the client area as well once the frame extends over
// it; its light/dark tint follows the frame's, which follows the app's
// scheme (Ui::Settings::GeneralPage::applyColorScheme()). Before that
// (Windows 10, Windows 11 21H2): the undocumented accent policy the shell
// itself uses — acrylic too since Windows 10 1803, as in the system's own
// apps (plain blur, much weaker, before). Acrylic through it has been
// known to lag while a window is dragged or resized.
class WindowsBackend : public Backend {
public:
    static Backend* create()
    {
        const auto os = QOperatingSystemVersion::current();
        if (os < QOperatingSystemVersion::Windows10)
            return nullptr;
        constexpr int kWindows10_1803 = 17134;
        const int accentState = os.microVersion() >= kWindows10_1803 ? kAccentAcrylic : kAccentBlur;
        const auto setAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
        if (setAttribute == nullptr) {
            qCWarning(lcGlass) << "no SetWindowCompositionAttribute in user32";
            return nullptr;
        }
        return new WindowsBackend(setAttribute, accentState, os >= QOperatingSystemVersion::Windows11_22H2);
    }

    void apply(QWindow* window, const QRegion& region) override
    {
        if (!region.isEmpty())
            return; // a popup's panel: can't be blurred alone
        const auto hwnd = reinterpret_cast<HWND>(window->winId());
        if (systemBackdrop_) {
            const MARGINS wholeWindow = { -1, -1, -1, -1 };
            DwmExtendFrameIntoClientArea(hwnd, &wholeWindow);
            const int acrylic = 3; // DWMSBT_TRANSIENTWINDOW
            DwmSetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &acrylic, sizeof(acrylic));
            return;
        }
        setAccent(hwnd);
    }

    // A popup (frameless and translucent, so a layered window, which DWM's
    // system backdrop doesn't cover even on Windows 11): the accent, on the
    // window clipped to the panel's rounded shape. The region is binary, so
    // its corners are stepped — the panel's own antialiased edge sits on top.
    void applyPanel(QWindow* window, const QRect& panel, qreal radius) override
    {
        const auto hwnd = reinterpret_cast<HWND>(window->winId());
        const qreal dpr = window->devicePixelRatio();
        const int left = qFloor(panel.left() * dpr);
        const int top = qFloor(panel.top() * dpr);
        const int right = qCeil((panel.right() + 1) * dpr);
        const int bottom = qCeil((panel.bottom() + 1) * dpr);
        const int diameter = qRound(2 * radius * dpr);
        // Right/bottom exclusive, and one more: CreateRoundRectRgn leaves
        // out its last row and column.
        HRGN shape = CreateRoundRectRgn(left, top, right + 1, bottom + 1, diameter, diameter);
        if (SetWindowRgn(hwnd, shape, TRUE) == 0)
            DeleteObject(shape); // else the system owns it
        setAccent(hwnd);
    }

    bool clipsWindow() const override { return true; }

private:
    // user32's undocumented SetWindowCompositionAttribute() and its data.
    static constexpr int kAccentBlur = 3; // ACCENT_ENABLE_BLURBEHIND
    static constexpr int kAccentAcrylic = 4; // ACCENT_ENABLE_ACRYLICBLURBEHIND
    struct AccentPolicy {
        int state;
        int flags;
        DWORD gradientColor;
        int animationId;
    };
    struct CompositionAttributeData {
        int attribute;
        void* data;
        SIZE_T size;
    };
    using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, CompositionAttributeData*);

    WindowsBackend(SetWindowCompositionAttributeFn setAttribute, int accentState, bool systemBackdrop)
        : setAttribute_(setAttribute)
        , accentState_(accentState)
        , systemBackdrop_(systemBackdrop)
    {
    }

    void setAccent(HWND hwnd)
    {
        // The acrylic's own tint: all but transparent, since the app paints
        // its own (Theme::glass()) — not fully, which breaks acrylic on some
        // Windows 10 builds. AABBGGRR.
        constexpr DWORD kTint = 0x01000000;
        AccentPolicy accent = { accentState_, 0, kTint, 0 };
        CompositionAttributeData data = { 19 /* WCA_ACCENT_POLICY */, &accent, sizeof(accent) };
        setAttribute_(hwnd, &data);
    }

    SetWindowCompositionAttributeFn setAttribute_;
    int accentState_;
    // Windows 11 22H2+: the main window gets DWM's system backdrop.
    bool systemBackdrop_;
};
#endif

Backend* createBackend()
{
#if defined(Q_OS_WIN)
    return WindowsBackend::create();
#endif
#if defined(CLOUDMUS_BLUR_WAYLAND)
    // Any compositor: the standard protocol isn't KDE's alone.
    if (isWayland()) {
        if (Backend* backend = WaylandBackend::create())
            return backend;
    }
#endif
    if (!isKde())
        return nullptr;
#if defined(CLOUDMUS_BLUR_X11)
    if (isX11())
        return X11Backend::create();
#endif
#if defined(Q_OS_LINUX)
    return KdeLibraryBackend::create();
#else
    return nullptr;
#endif
}

Backend* backend()
{
    // Leaked on purpose: lives as long as the app.
    static Backend* const instance = createBackend();
    return instance;
}

// Whether a translucent window is composited over what's behind it — so
// something outside the app (a desktop extension) could blur it — rather
// than drawn over black.
bool compositing()
{
#if defined(Q_OS_LINUX)
    if (isWayland())
        return true;
#if defined(CLOUDMUS_BLUR_X11)
    if (isX11()) {
        // X11: only while a compositing manager holds _NET_WM_CM_S<screen>.
        auto* app = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (app == nullptr || app->connection() == nullptr)
            return false;
        xcb_connection_t* connection = app->connection();
        const QByteArray name = "_NET_WM_CM_S" + QByteArray::number(0);
        xcb_intern_atom_reply_t* atom = xcb_intern_atom_reply(
            connection, xcb_intern_atom(connection, false, uint16_t(name.size()), name.constData()), nullptr);
        if (atom == nullptr)
            return false;
        xcb_get_selection_owner_reply_t* owner
            = xcb_get_selection_owner_reply(connection, xcb_get_selection_owner(connection, atom->atom), nullptr);
        const bool owned = owner != nullptr && owner->owner != XCB_NONE;
        free(owner);
        free(atom);
        return owned;
    }
#endif
    return isX11();
#else
    return false;
#endif
}

} // namespace

Support support()
{
    static const Support value = backend() != nullptr ? Support::Blur
        : compositing()                               ? Support::SeeThrough
                                                      : Support::None;
    return value;
}

bool blurClipsWindow() { return backend() != nullptr && backend()->clipsWindow(); }

namespace {
// Runs `apply` with `window`'s native window once it's on screen.
void applyWhenShown(QWidget* window, std::function<void(QWindow*)> apply)
{
    if (window == nullptr || backend() == nullptr)
        return;
    // The native window has to exist for the compositor to be told about it.
    window->winId();
    QWindow* handle = window->windowHandle();
    if (handle == nullptr)
        return;
    // Once it's on screen: a window being shown gets its native surface
    // only as it's mapped, after the show event this is usually called
    // from.
    QPointer<QWindow> target(handle);
    QPointer<QWidget> widget(window);
    QTimer::singleShot(0, handle, [target, widget, apply]() {
        if (!target || !target->isVisible())
            return;
        apply(target);
        // Wayland applies the blur region with the surface's next commit,
        // and a widget window commits only when it repaints: a just-shown
        // menu has nothing left to paint, so it stayed unblurred until the
        // mouse moved over an item. QWindow::requestUpdate() isn't enough —
        // with nothing dirty, the widget window paints nothing and commits
        // nothing.
        if (widget)
            widget->update();
    });
}
} // namespace

void enableBlurBehind(QWidget* window, const QRegion& region)
{
    applyWhenShown(window, [region](QWindow* target) { backend()->apply(target, region); });
}

void enableBlurBehindPanel(QWidget* window, const QRect& panel, qreal radius)
{
    applyWhenShown(window, [panel, radius](QWindow* target) { backend()->applyPanel(target, panel, radius); });
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
