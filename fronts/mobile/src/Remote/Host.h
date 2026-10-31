#pragma once

#include <QObject>
#include <QRemoteObjectHost>

namespace App {
class MobileCore;
}

namespace Remote {

// Offers the core's view models to views in another process (the Android UI
// process; `--split` on the desktop) over a local socket at `socketPath`.
class Host : public QObject {
    Q_OBJECT

public:
    Host(App::MobileCore& core, const QString& socketPath, QObject* parent = nullptr);

    bool listening() const { return listening_; }

private:
    QRemoteObjectHost host_;
    bool listening_ = false;
};

} // namespace Remote
