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

public:
    explicit MainScreen(App::Core& core, QObject* parent = nullptr);

    TrackFilterModel& rows() { return filter_; }

public slots:
    void setFilter(const QString& text);
    // `row`: of the filtered rows.
    void playRow(int row);

private:
    App::Core& core_;
    TrackFilterModel filter_;
};

} // namespace ViewModel
