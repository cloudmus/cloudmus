#include "LogModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <ctime>

namespace Ui {

namespace {

constexpr int kMaxEntries = 20000;
constexpr int kPollIntervalMs = 1000;
// More than this back is too old to matter when a fresh session starts.
constexpr qint64 kMaxInitialBytes = 4 * 1024 * 1024;

int levelRank(QChar level)
{
    switch (level.toLatin1()) {
        case 'V':
            return 0;
        case 'D':
            return 1;
        case 'I':
            return 2;
        case 'W':
            return 3;
        case 'E':
            return 4;
        case 'F':
            return 5;
        default:
            return 2;
    }
}

// Lines whose boot times are this close came from the same boot.
constexpr qint64 kSameBootToleranceMs = 5000;

double secondsSinceBoot()
{
    timespec ts { };
#ifdef CLOCK_BOOTTIME
    clock_gettime(CLOCK_BOOTTIME, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return double(ts.tv_sec) + ts.tv_nsec / 1e9;
}

} // namespace

LogModel::LogModel(QObject* parent)
    : QAbstractListModel(parent)
{
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &LogModel::poll);
}

int LogModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(shown_.size()); }

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= shown_.size())
        return { };
    const Entry& entry = all_[shown_[index.row()]];
    switch (role) {
        case TimeRole:
            return entry.time;
        case UptimeRole:
            return entry.uptime;
        case ProcessRole:
            return entry.process;
        case ThreadRole:
            return entry.thread;
        case LevelRole:
            return QString(entry.level);
        case TagRole:
            return entry.tag;
        case Qt::DisplayRole:
        case MessageRole:
            return entry.message;
        default:
            return { };
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        { TimeRole, "time" },
        { UptimeRole, "uptime" },
        { ProcessRole, "process" },
        { ThreadRole, "thread" },
        { LevelRole, "level" },
        { TagRole, "tag" },
        { MessageRole, "message" },
    };
}

void LogModel::setDirectory(const QString& directory)
{
    if (directory_ == directory)
        return;
    directory_ = directory;
    beginResetModel();
    files_.clear();
    all_.clear();
    shown_.clear();
    endResetModel();
    emit directoryChanged();
    emit countChanged();
    poll();
    pollTimer_.start();
}

void LogModel::setMinLevel(const QString& level)
{
    if (minLevel_ == level)
        return;
    minLevel_ = level;
    refilter();
}

void LogModel::setProcess(const QString& process)
{
    if (process_ == process)
        return;
    process_ = process;
    refilter();
}

void LogModel::setText(const QString& text)
{
    if (text_ == text)
        return;
    text_ = text;
    refilter();
}

void LogModel::setTag(const QString& tag)
{
    if (tag_ == tag)
        return;
    tag_ = tag;
    refilter();
}

void LogModel::setThisBootOnly(bool only)
{
    if (thisBootOnly_ == only)
        return;
    thisBootOnly_ = only;
    refilter();
}

int LogModel::lastBootRow() const
{
    if (shown_.isEmpty())
        return -1;
    const qint64 lastBoot = all_[shown_.last()].bootedAtMs;
    int row = int(shown_.size()) - 1;
    while (row > 0 && qAbs(all_[shown_[row - 1]].bootedAtMs - lastBoot) < kSameBootToleranceMs)
        --row;
    return row;
}

void LogModel::poll()
{
    if (directory_.isEmpty())
        return;
    QDir dir(directory_);
    // Names carry the start time: sorted by name, the files go oldest first.
    const QStringList names = dir.entryList({ QStringLiteral("*.log") }, QDir::Files, QDir::Name);
    QVector<Entry> fresh;
    for (const QString& name : names)
        readTail(dir.filePath(name), fresh);
    if (fresh.isEmpty())
        return;
    // Two processes write side by side: merged by time of day (same clock).
    std::stable_sort(fresh.begin(), fresh.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });

    const int dropped = std::max(0, int(all_.size() + fresh.size()) - kMaxEntries);
    if (dropped > 0) {
        beginResetModel();
        all_.remove(0, std::min(dropped, int(all_.size())));
        all_ += fresh;
        if (all_.size() > kMaxEntries)
            all_.remove(0, all_.size() - kMaxEntries);
        shown_.clear();
        for (int i = 0; i < all_.size(); ++i) {
            if (accepts(all_[i]))
                shown_.append(i);
        }
        endResetModel();
    } else {
        QVector<int> rows;
        const int first = int(all_.size());
        all_ += fresh;
        for (int i = first; i < all_.size(); ++i) {
            if (accepts(all_[i]))
                rows.append(i);
        }
        if (!rows.isEmpty()) {
            beginInsertRows({ }, int(shown_.size()), int(shown_.size() + rows.size()) - 1);
            shown_ += rows;
            endInsertRows();
        }
    }
    emit countChanged();
}

bool LogModel::readTail(const QString& path, QVector<Entry>& into)
{
    static const QRegularExpression line(QStringLiteral(
        R"(^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) \[\s*([\d.]+)\] (\S+?)/(\S+) ([VDIWEF]) ([^:]*): ?(.*)$)"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    FileState& state = files_[path];
    if (file.size() < state.offset)
        state = { }; // rotated or cleared
    if (state.offset == 0 && file.size() > kMaxInitialBytes)
        state.offset = file.size() - kMaxInitialBytes;
    if (file.size() == state.offset)
        return false;
    file.seek(state.offset);
    QByteArray chunk = state.partial + file.readAll();
    state.offset = file.pos();
    const qsizetype lastNewline = chunk.lastIndexOf('\n');
    state.partial = chunk.mid(lastNewline + 1);
    chunk.truncate(lastNewline + 1);

    const int before = int(into.size());
    for (const QByteArray& raw : chunk.split('\n')) {
        if (raw.isEmpty())
            continue;
        const QString text = QString::fromUtf8(raw);
        if (text.startsWith(QStringLiteral("    "))) {
            // A continuation (a stack trace, a multi-line message).
            if (into.size() > before)
                into.last().message += QLatin1Char('\n') + text.mid(4);
            else if (!all_.isEmpty())
                all_.last().message += QLatin1Char('\n') + text.mid(4);
            continue;
        }
        const QRegularExpressionMatch match = line.match(text);
        if (!match.hasMatch())
            continue;
        Entry entry;
        entry.time = match.captured(1);
        entry.uptime = match.captured(2).toDouble();
        const QDateTime wall = QDateTime::fromString(entry.time, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        entry.bootedAtMs = wall.toMSecsSinceEpoch() - qint64(entry.uptime * 1000);
        entry.process = match.captured(3);
        entry.thread = match.captured(4);
        entry.level = match.captured(5).at(0);
        entry.tag = match.captured(6);
        entry.message = match.captured(7);
        into.append(entry);
    }
    return into.size() > before;
}

bool LogModel::accepts(const Entry& entry) const
{
    if (levelRank(entry.level) < levelRank(minLevel_.isEmpty() ? QChar('V') : minLevel_.at(0)))
        return false;
    if (!process_.isEmpty() && entry.process != process_)
        return false;
    if (!tag_.isEmpty() && entry.tag != tag_)
        return false;
    if (thisBootOnly_) {
        const qint64 bootedAtMs = QDateTime::currentMSecsSinceEpoch() - qint64(secondsSinceBoot() * 1000);
        if (qAbs(entry.bootedAtMs - bootedAtMs) >= kSameBootToleranceMs)
            return false;
    }
    if (!text_.isEmpty() && !entry.message.contains(text_, Qt::CaseInsensitive)
        && !entry.tag.contains(text_, Qt::CaseInsensitive))
        return false;
    return true;
}

void LogModel::refilter()
{
    beginResetModel();
    shown_.clear();
    for (int i = 0; i < all_.size(); ++i) {
        if (accepts(all_[i]))
            shown_.append(i);
    }
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

} // namespace Ui
