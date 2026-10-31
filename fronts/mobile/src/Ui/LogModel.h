#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace Ui {

// The log files of both processes as one list, oldest first, for the Log
// page: read once, then their new tails every second. Filtered by level,
// process and text; keeps the newest `kMaxEntries` so a weak head unit
// doesn't choke on a long log.
class LogModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QString minLevel READ minLevel WRITE setMinLevel NOTIFY filterChanged)
    Q_PROPERTY(QString process READ process WRITE setProcess NOTIFY filterChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY filterChanged)
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY filterChanged)
    // Only the lines from this boot of the system.
    Q_PROPERTY(bool thisBootOnly READ thisBootOnly WRITE setThisBootOnly NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TimeRole = Qt::UserRole + 1,
        UptimeRole,
        ProcessRole,
        ThreadRole,
        LevelRole,
        TagRole,
        MessageRole,
    };

    explicit LogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = { }) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString directory() const { return directory_; }
    void setDirectory(const QString& directory);
    QString minLevel() const { return minLevel_; }
    void setMinLevel(const QString& level);
    QString process() const { return process_; }
    void setProcess(const QString& process);
    QString text() const { return text_; }
    void setText(const QString& text);
    QString tag() const { return tag_; }
    void setTag(const QString& tag);
    bool thisBootOnly() const { return thisBootOnly_; }
    void setThisBootOnly(bool only);

    // The row where the newest session of the system (the latest boot)
    // begins, -1 if none is shown.
    Q_INVOKABLE int lastBootRow() const;

signals:
    void directoryChanged();
    void filterChanged();
    void countChanged();

private:
    struct Entry {
        QString time;
        double uptime = 0;
        // When the system this line was written on booted: lines of one
        // boot share it, give or take clock jitter.
        qint64 bootedAtMs = 0;
        QString process;
        QString thread;
        QChar level;
        QString tag;
        QString message;
    };
    struct FileState {
        qint64 offset = 0;
        QByteArray partial;
    };

    void poll();
    // Appends what's new in `path`; true if anything was.
    bool readTail(const QString& path, QVector<Entry>& into);
    bool accepts(const Entry& entry) const;
    void refilter();

    QString directory_;
    QString minLevel_ = QStringLiteral("D");
    QString process_;
    QString text_;
    QString tag_;
    bool thisBootOnly_ = false;
    QHash<QString, FileState> files_;
    QVector<Entry> all_;
    QVector<int> shown_;
    QTimer pollTimer_;
};

} // namespace Ui
