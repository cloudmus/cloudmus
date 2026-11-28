// Measures how fast Net::ParallelDownload gets a file over a number of
// connections, through the proxy the app would use — to tell what more
// connections are worth on a given line (docs/development.md):
//
//   cloudmus-download-bench <url> <connections> [--direct] [--seconds N] [--verbose]
//
// --verbose prints the download's debug log (cloudmus.net.download): each
// range asked for and done, and how fast.
//
// The file goes to a temporary one and is deleted afterwards.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "ParallelDownload.h"
#include "ProxyRouting.h"

int main(int argc, char** argv)
{
    // Qt would send the log to journald when stderr isn't a terminal.
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    QTextStream out(stdout);
    const bool direct = args.removeAll(QStringLiteral("--direct")) > 0;
    QLoggingCategory::setFilterRules(args.removeAll(QStringLiteral("--verbose")) > 0
            ? QStringLiteral("cloudmus.*.debug=true")
            : QStringLiteral("cloudmus.*.debug=false"));
    int seconds = 0;
    if (const qsizetype at = args.indexOf(QStringLiteral("--seconds")); at >= 0 && at + 1 < args.size()) {
        seconds = args.at(at + 1).toInt();
        args.remove(at, 2);
    }
    bool ok = false;
    const int connections = args.value(1).toInt(&ok);
    const QUrl url(args.value(0));
    if (args.size() != 2 || !ok || connections < 1 || !url.isValid()) {
        out << "usage: cloudmus-download-bench <url> <connections> [--direct] [--seconds N] [--verbose]\n";
        return 2;
    }

    QNetworkAccessManager network;
    network.setProxy(direct ? QNetworkProxy(QNetworkProxy::NoProxy) : Net::systemProxy(url));
    QTemporaryDir dir;
    Net::ParallelDownload download(dir.filePath(QStringLiteral("download")));
    download.setSource(&network, url);
    download.setConnections(connections);

    QElapsedTimer clock;
    const auto report = [&](const char* what) {
        const double s = qMax<qint64>(clock.elapsed(), 1) / 1000.0;
        const double mib = download.receivedBytes() / 1048576.0;
        out << what
            << QStringLiteral(" %1 MiB in %2 s: %3 MiB/s").arg(mib, 0, 'f', 1).arg(s, 0, 'f', 1).arg(mib / s, 0, 'f', 2)
            << Qt::endl;
    };
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&]() { report("  "); });
    QObject::connect(&download, &Net::ParallelDownload::completed, [&]() {
        report("done");
        app.exit(0);
    });
    QObject::connect(&download, &Net::ParallelDownload::failed, [&]() {
        out << "failed: " << download.errorString() << Qt::endl;
        app.exit(1);
    });
    if (seconds > 0) {
        QTimer::singleShot(seconds * 1000, [&]() {
            report("stopped");
            app.exit(0);
        });
    }
    out << url.host() << ", " << connections << " connection(s), "
        << (network.proxy().type() == QNetworkProxy::NoProxy
                   ? QStringLiteral("direct")
                   : QStringLiteral("proxy %1").arg(network.proxy().hostName()))
        << Qt::endl;
    clock.start();
    ticker.start(2000);
    download.start();
    return app.exec();
}
