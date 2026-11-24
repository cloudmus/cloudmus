#include "GlobalHotkeysX11.h"

#include <QGuiApplication>

#include <xcb/xcb.h>

#include "KeyNames.h"

namespace Integration {

namespace {
// Num Lock and Caps Lock change the modifiers a key comes with; a grab is
// for exactly one combination, so one is made for each of these.
constexpr quint16 kLockMasks[] = { 0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2 };
constexpr quint16 kRelevantMask = XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4;

xcb_connection_t* xcbConnection()
{
    auto* x11 = qGuiApp != nullptr ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    return x11 != nullptr ? x11->connection() : nullptr;
}

quint16 modifierMask(Qt::KeyboardModifiers modifiers)
{
    quint16 mask = 0;
    if (modifiers.testFlag(Qt::ShiftModifier))
        mask |= XCB_MOD_MASK_SHIFT;
    if (modifiers.testFlag(Qt::ControlModifier))
        mask |= XCB_MOD_MASK_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        mask |= XCB_MOD_MASK_1;
    if (modifiers.testFlag(Qt::MetaModifier))
        mask |= XCB_MOD_MASK_4;
    return mask;
}

// The keycode that types `keysym` with the keyboard layout now.
xcb_keycode_t keycodeFor(xcb_connection_t* connection, xcb_keysym_t keysym)
{
    const xcb_setup_t* setup = xcb_get_setup(connection);
    const int count = setup->max_keycode - setup->min_keycode + 1;
    xcb_get_keyboard_mapping_reply_t* mapping = xcb_get_keyboard_mapping_reply(
        connection, xcb_get_keyboard_mapping(connection, setup->min_keycode, count), nullptr);
    if (mapping == nullptr)
        return 0;
    const xcb_keysym_t* keysyms = xcb_get_keyboard_mapping_keysyms(mapping);
    const int perKeycode = mapping->keysyms_per_keycode;
    xcb_keycode_t found = 0;
    for (int i = 0; i < count * perKeycode && found == 0; ++i) {
        if (keysyms[i] == keysym)
            found = xcb_keycode_t(setup->min_keycode + i / perKeycode);
    }
    free(mapping);
    return found;
}
} // namespace

bool GlobalHotkeysX11::available() { return xcbConnection() != nullptr; }

GlobalHotkeysX11::GlobalHotkeysX11(QObject* parent)
    : GlobalHotkeys(parent)
{
    auto* connection = xcbConnection();
    connection_ = connection;
    root_ = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeysX11::~GlobalHotkeysX11()
{
    ungrabAll();
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void GlobalHotkeysX11::ungrabAll()
{
    auto* connection = static_cast<xcb_connection_t*>(connection_);
    for (const Grab& grab : std::as_const(grabs_)) {
        for (quint16 lock : kLockMasks)
            xcb_ungrab_key(connection, grab.keycode, root_, grab.modifiers | lock);
    }
    xcb_flush(connection);
    grabs_.clear();
}

void GlobalHotkeysX11::setEntries(const QList<Entry>& entries)
{
    auto* connection = static_cast<xcb_connection_t*>(connection_);
    // Grabbed afresh each time: a few keys, and a changed keyboard layout
    // then also gets caught up with.
    ungrabAll();

    QSet<QString> failed;
    for (const Entry& entry : entries) {
        if (!entry.global || entry.key.count() != 1)
            continue;
        const QKeyCombination combination = entry.key[0];
        const unsigned int keysym = keysymForKey(combination.key());
        const xcb_keycode_t keycode = keysym != 0 ? keycodeFor(connection, keysym) : 0;
        if (keycode == 0) {
            failed.insert(entry.id);
            continue;
        }
        const quint16 modifiers = modifierMask(combination.keyboardModifiers());
        bool grabbed = true;
        for (quint16 lock : kLockMasks) {
            xcb_generic_error_t* error = xcb_request_check(connection,
                xcb_grab_key_checked(
                    connection, 1, root_, modifiers | lock, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC));
            if (error != nullptr) {
                // BadAccess: another client has the key.
                grabbed = false;
                free(error);
                break;
            }
        }
        if (grabbed) {
            grabs_.append({ entry.id, keycode, modifiers });
        } else {
            for (quint16 lock : kLockMasks)
                xcb_ungrab_key(connection, keycode, root_, modifiers | lock);
            failed.insert(entry.id);
        }
    }
    xcb_flush(connection);
    emit failedChanged(failed);
}

bool GlobalHotkeysX11::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    const auto* event = static_cast<xcb_generic_event_t*>(message);
    if ((event->response_type & 0x7f) != XCB_KEY_PRESS)
        return false;
    const auto* press = reinterpret_cast<xcb_key_press_event_t*>(message);
    // Only the presses that are ours: a grab's, on the root window.
    if (press->event != root_)
        return false;
    for (const Grab& grab : std::as_const(grabs_)) {
        if (grab.keycode == press->detail && grab.modifiers == (press->state & kRelevantMask)) {
            emit activated(grab.id, QString());
            return true;
        }
    }
    return false;
}

} // namespace Integration
