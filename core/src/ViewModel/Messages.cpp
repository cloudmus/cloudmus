#include "Messages.h"

#include "RpcClient.h"

namespace ViewModel {

namespace {
// Longer than this, or on several lines, is no one-line message.
constexpr int kMaxHeadline = 140;

QString withSource(const QString& sourceName, const QString& text)
{
    return sourceName.isEmpty() ? text : Messages::tr("%1: %2").arg(sourceName, text);
}
}

Messages::Messages(QObject* parent)
    : QObject(parent)
{
}

void Messages::failure(const QString& sourceName, const std::exception& e, bool quietOffline)
{
    const auto* call = dynamic_cast<const Rpc::RpcCallException*>(&e);
    failure(sourceName, QString::fromStdString(e.what()), call != nullptr && call->isOffline(), quietOffline);
}

void Messages::failure(const QString& sourceName, const QString& message, bool isOffline, bool quietOffline)
{
    const QString whole = withSource(sourceName, message);
    if (isOffline) {
        const QString headline
            = sourceName.isEmpty() ? tr("No network connection") : tr("%1: no network connection").arg(sourceName);
        if (quietOffline)
            offline(headline, whole);
        else
            error(headline, whole);
        return;
    }
    const int lineEnd = whole.indexOf(QLatin1Char('\n'));
    if (lineEnd < 0 && whole.size() <= kMaxHeadline) {
        error(whole);
        return;
    }
    QString headline = whole.left(lineEnd < 0 ? kMaxHeadline : qMin(lineEnd, kMaxHeadline)).trimmed();
    if (headline.size() < whole.size())
        headline += QStringLiteral("…");
    error(headline, whole);
}

} // namespace ViewModel
