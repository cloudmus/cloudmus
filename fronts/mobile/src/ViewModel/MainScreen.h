#pragma once

#include <QObject>
#include <QString>

#include "TrackFilterModel.h"

namespace App {
class Core;
}

namespace ViewModel {

// The main screen's list: the active playlist's rows, filtered where they are.
// A row here is not a row of the playlist, which is what this minds.
class MainScreen : public QObject {
    Q_OBJECT
    // No network: said here, for all to see, not as an error each time.
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)
    // Too little room on the disk, and what that stopped; empty otherwise.
    Q_PROPERTY(QString spaceWarning READ spaceWarning NOTIFY spaceWarningChanged)

public:
    explicit MainScreen(App::Core& core, QObject* parent = nullptr);

    TrackFilterModel& rows() { return filter_; }

    bool offline() const;
    QString spaceWarning() const;

public slots:
    void setFilter(const QString& text);
    // `row`: of the filtered rows.
    void playRow(int row);

signals:
    // What the app has to tell the user (ViewModel::Messages), in plain types.
    void message(int kind, const QString& text);
    void offlineChanged();
    void spaceWarningChanged();

private:
    App::Core& core_;
    TrackFilterModel filter_;
};

} // namespace ViewModel
