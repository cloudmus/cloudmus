#include "ReleaseFeed.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace Update {

QString Version::toString() const
{
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch) + (dev ? QStringLiteral("+dev") : QString());
}

std::optional<Version> parseVersion(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^v?(\d+)\.(\d+)\.(\d+)(.*)$)"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    Version version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).toInt();
    version.patch = match.captured(3).toInt();
    version.dev = !match.captured(4).isEmpty();
    return version;
}

QString platformAssetSuffix()
{
#ifdef Q_OS_WIN
    return QStringLiteral("-x86_64-Setup.exe");
#else
    return QStringLiteral("-x86_64.AppImage");
#endif
}

std::optional<QList<Release>> parseReleases(const QByteArray& json, const QString& assetSuffix)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray())
        return std::nullopt;
    QList<Release> releases;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        if (object.value(QLatin1String("draft")).toBool() || object.value(QLatin1String("prerelease")).toBool())
            continue;
        Release release;
        release.name = object.value(QLatin1String("tag_name")).toString();
        const std::optional<Version> version = parseVersion(release.name);
        // A tag like "1.2.0-rc1" isn't a release to offer either.
        if (!version || version->dev)
            continue;
        release.version = *version;
        release.published = QDateTime::fromString(object.value(QLatin1String("published_at")).toString(), Qt::ISODate)
                                .toLocalTime()
                                .date();
        release.notes = object.value(QLatin1String("body")).toString().trimmed();
        release.pageUrl = QUrl(object.value(QLatin1String("html_url")).toString());
        for (const QJsonValue& assetValue : object.value(QLatin1String("assets")).toArray()) {
            const QJsonObject asset = assetValue.toObject();
            const QString name = asset.value(QLatin1String("name")).toString();
            if (!name.endsWith(assetSuffix))
                continue;
            release.assetName = name;
            release.assetUrl = QUrl(asset.value(QLatin1String("browser_download_url")).toString());
            release.assetSize = asset.value(QLatin1String("size")).toInteger();
            break;
        }
        if (release.assetUrl.isValid() && !release.assetUrl.isEmpty())
            releases.append(release);
    }
    std::stable_sort(
        releases.begin(), releases.end(), [](const Release& a, const Release& b) { return a.version > b.version; });
    return releases;
}

std::optional<Release> releaseOf(const QList<Release>& releases, const Version& current)
{
    for (const Release& release : releases) {
        if (release.version.major == current.major && release.version.minor == current.minor
            && release.version.patch == current.patch)
            return release;
    }
    return std::nullopt;
}

std::optional<PendingUpdate> pendingUpdate(const QList<Release>& releases, const Version& current)
{
    PendingUpdate update;
    for (const Release& release : releases) {
        if (release.version > current)
            update.releases.append(release);
    }
    if (const std::optional<Release> own = releaseOf(releases, current))
        update.currentPublished = own->published;
    if (update.releases.isEmpty())
        return std::nullopt;
    return update;
}

} // namespace Update
