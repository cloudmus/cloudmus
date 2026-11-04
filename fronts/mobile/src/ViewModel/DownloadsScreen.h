#pragma once

#include <QObject>
#include <QString>

#include "RowsModel.h"

namespace App {
class Core;
}

namespace ViewModel {

// The Downloads screen: ViewModel::Downloads' jobs as rows of plain values, and
// its actions as slots, for a replica.
class DownloadsScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    Q_PROPERTY(bool canClear READ canClear NOTIFY canClearChanged)
    Q_PROPERTY(bool canSavePlaying READ canSavePlaying NOTIFY canSavePlayingChanged)

public:
    explicit DownloadsScreen(App::Core& core, QObject* parent = nullptr);

    RowsModel& rows() { return rows_; }

    bool enabled() const;
    QString directory() const;
    bool canClear() const;
    bool canSavePlaying() const;

public slots:
    void setEnabled(bool on);
    void cancel(int jobId);
    void clearFinished();
    void savePlaying();

signals:
    void enabledChanged();
    void directoryChanged();
    void canClearChanged();
    void canSavePlayingChanged();

private:
    void rebuild();

    App::Core& core_;
    RowsModel rows_;
};

} // namespace ViewModel
