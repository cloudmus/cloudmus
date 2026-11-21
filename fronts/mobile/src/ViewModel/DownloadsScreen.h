#pragma once

#include <QObject>
#include <QString>

namespace App {
class Core;
}

namespace ViewModel {

// Saving tracks, as Settings shows it: whether it's allowed, and the
// subfolders a track goes into, as the desktop's Downloads settings. What is
// being saved shows on the tracks themselves; slots and plain values, for a
// replica.
class DownloadsScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    // A Config::Settings::DownloadLayout.
    Q_PROPERTY(int layout READ layout NOTIFY layoutChanged)

public:
    explicit DownloadsScreen(App::Core& core, QObject* parent = nullptr);

    bool enabled() const;
    QString directory() const;
    int layout() const;

public slots:
    void setEnabled(bool on);
    void setLayout(int layout);

signals:
    void enabledChanged();
    void directoryChanged();
    void layoutChanged();

private:
    App::Core& core_;
};

} // namespace ViewModel
