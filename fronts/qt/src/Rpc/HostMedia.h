#pragma once

#include <QObject>
#include <QString>

namespace Rpc {

// The media library the front's platform keeps, such as Android's
// MediaStore, for the sources that ask for it (capability hostMedia,
// docs/protocol.md §7.10). The desktop has none.
class HostMedia : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // The file media.setLibrary names, written anew for each revision;
    // empty while there's none yet.
    virtual QString libraryPath() const = 0;
    virtual qint64 revision() const = 0;
    // What a source asked for with media/wanted: everything on the device,
    // or only what this app saved. The platform may have to ask the user
    // for the first; changed() follows either way.
    virtual void want(const QString& sourceId, bool all) = 0;
    // Everything was asked for, and the platform waits for the user to
    // allow it.
    virtual bool permissionNeeded() const { return false; }

signals:
    // A new revision of the library is written.
    void changed();
    void permissionNeededChanged();
};

} // namespace Rpc
