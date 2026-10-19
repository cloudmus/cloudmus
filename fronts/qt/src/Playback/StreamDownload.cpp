#include "StreamDownload.h"

#include "NetworkPool.h"

namespace Playback {

StreamDownload::StreamDownload(NetworkPool& network, const QString& filePath, QObject* parent)
    : QObject(parent)
    , network_(network)
    , filePath_(filePath)
    , download_(filePath)
{
    connect(&download_, &Net::ParallelDownload::metaReady, this, &StreamDownload::metaReady);
    connect(&download_, &Net::ParallelDownload::progressed, this, &StreamDownload::progressed);
    connect(&download_, &Net::ParallelDownload::completed, this, &StreamDownload::completed);
    connect(&download_, &Net::ParallelDownload::failed, this, &StreamDownload::failed);
}

StreamDownload::~StreamDownload() = default;

void StreamDownload::start()
{
    if (isComplete() || isActive())
        return;
    download_.setSource(network_.managerFor(source_.proxy), source_.url, source_.headers);
    download_.start();
}

void StreamDownload::pause() { download_.pause(); }

void StreamDownload::giveUp()
{
    pause();
    if (abandoned_ || isComplete())
        return;
    abandoned_ = true;
    emit abandoned();
}

QByteArray StreamDownload::read(qint64 offset, qint64 maxBytes)
{
    const qint64 available = bytesAvailable();
    if (offset >= available || maxBytes <= 0)
        return { };
    if (!readFile_.isOpen()) {
        readFile_.setFileName(filePath_);
        if (!readFile_.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
            return { };
    }
    if (!readFile_.seek(offset))
        return { };
    return readFile_.read(qMin(maxBytes, available - offset));
}

void StreamDownload::relocate(const QString& filePath)
{
    readFile_.close();
    filePath_ = filePath;
    download_.relocate(filePath);
}

} // namespace Playback
