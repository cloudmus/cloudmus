#include "DesktopColorScheme.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace Integration {

// macOS has no portal to ask, and needs none: Qt's Cocoa platform plugin
// always knows the system appearance and reports its changes on
// QStyleHints::colorSchemeChanged. So scheme() stays Unknown and changed()
// never fires.

DesktopColorScheme::DesktopColorScheme(QObject* parent)
    : QObject(parent)
{
}

DesktopColorScheme& DesktopColorScheme::instance()
{
    static DesktopColorScheme instance;
    return instance;
}

Qt::ColorScheme DesktopColorScheme::resolved() { return QGuiApplication::styleHints()->colorScheme(); }

// The D-Bus signal's slot is in the header, so moc refers to it.
void DesktopColorScheme::onSettingChanged(const QString&, const QString&, const QDBusVariant&) { }

} // namespace Integration
