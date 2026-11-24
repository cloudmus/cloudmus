#pragma once

#include <QObject>
#include <QString>

class QDBusVariant;

namespace Integration {

// The desktop's light/dark preference as the XDG desktop portal reports it
// (org.freedesktop.appearance color-scheme). Qt's own colorScheme() only
// knows it when its platform theme plugin does: under qt6ct, for one, it
// stays Unknown whatever the desktop is set to.
class DesktopColorScheme : public QObject {
    Q_OBJECT

public:
    explicit DesktopColorScheme(QObject* parent = nullptr);

    // The one instance everything shares: a single D-Bus watch.
    static DesktopColorScheme& instance();

    // Unknown: no portal, no answer yet, or the desktop has no preference.
    Qt::ColorScheme scheme() const { return scheme_; }

    // The scheme to follow: Qt's own when its platform theme knows it, the
    // portal's otherwise. Changes announce themselves on both
    // QStyleHints::colorSchemeChanged and changed().
    static Qt::ColorScheme resolved();

signals:
    void changed();

private slots:
    void onSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value);

private:
    void read(const QString& method);
    void set(const QVariant& value);

    Qt::ColorScheme scheme_ = Qt::ColorScheme::Unknown;
};

} // namespace Integration
