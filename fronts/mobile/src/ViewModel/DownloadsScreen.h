#pragma once

#include <QObject>
#include <QString>

#include "RowsModel.h"

namespace App {
class Core;
}

namespace ViewModel {

// Saving tracks: whether it's allowed and the subfolders a track goes into
// (Settings, as the desktop's Downloads settings), and what's being saved
// (the Downloads page, the indicator that leads there, the notification).
// Slots and plain values, for a replica.
class DownloadsScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    // A Config::Settings::DownloadLayout.
    Q_PROPERTY(int layout READ layout NOTIFY layoutChanged)
    // Anything queued or being saved.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // All of what's under way, 0..1; -1 while there's nothing to measure it by.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // Jobs listed, over ones included until cleared.
    Q_PROPERTY(int jobCount READ jobCount NOTIFY jobCountChanged)
    Q_PROPERTY(bool canClear READ canClear NOTIFY canClearChanged)
    // What's under way, in a line: "Saving “Mix”: 3 of 40 tracks"; "" with
    // nothing under way.
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)

public:
    explicit DownloadsScreen(App::Core& core, QObject* parent = nullptr);

    // What the system's notification says of the downloads: while any are
    // under way, the one running and how far all of it has got; then how the
    // last one ended, until another starts. Nothing (`shown` false) when
    // there's nothing to tell, or it was all cancelled.
    struct Notice {
        bool shown = false;
        bool ongoing = false;
        bool failed = false;
        QString title;
        QString text;
        // 0..1000, or -1 while it can't be measured.
        int permille = -1;
        bool operator==(const Notice&) const = default;
    };
    Notice notice() const;

    // One row a job: jobId, title, status, detail (a running playlist's
    // track), progress, isRunning, isOver, hasFailed, isCancelled, isPlaylist.
    RowsModel& jobRows() { return rows_; }

    bool enabled() const;
    QString directory() const;
    int layout() const;
    bool active() const;
    double progress() const;
    int jobCount() const;
    bool canClear() const;
    QString summary() const;

public slots:
    void setEnabled(bool on);
    void setLayout(int layout);
    void cancel(int jobId);
    void cancelAll();
    void clearFinished();

signals:
    void enabledChanged();
    void directoryChanged();
    void layoutChanged();
    void activeChanged();
    void progressChanged();
    void jobCountChanged();
    void canClearChanged();
    void summaryChanged();

private:
    void rebuild();

    App::Core& core_;
    RowsModel rows_;
    // As last told, so that only what changed is told again.
    bool active_ = false;
    double progress_ = -1;
    int jobCount_ = 0;
    bool canClear_ = false;
    QString summary_;
};

} // namespace ViewModel
