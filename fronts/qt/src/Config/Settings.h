#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>

namespace Config {

// QSettings-backed, ~/.config/cloudmus/fronts/qt/config.ini
class Settings {
public:
    Settings();

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);

    int sidebarWidth() const;
    void setSidebarWidth(int width);

    int volume() const;
    void setVolume(int volume0To100);

    QString lastSourceId() const;
    void setLastSourceId(const QString& sourceId);

    bool closeMinimizesToTray() const;
    void setCloseMinimizesToTray(bool value);

    QString downloadDirectory() const;
    void setDownloadDirectory(const QString& path);

private:
    QSettings settings_;
};

} // namespace Config
