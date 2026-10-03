#pragma once

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QUrl>

#include <compare>
#include <optional>

namespace Update {

// A version as the app and its releases name it: an "x.y.z" tag, or what
// cmake/Version.cmake makes of a build between tags — "x.y.z-5-g1a2b3c4"
// after the tag, "x.y.z+g1a2b3c4" with no tag yet, either "-dirty" too.
// Such a dev build sorts after its own x.y.z and before the next one.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool dev = false;

    auto operator<=>(const Version&) const = default;
    QString toString() const;
};

// Leading "v" allowed; nullopt for anything not starting with x.y.z.
std::optional<Version> parseVersion(const QString& text);

// One published release with a file this platform can install.
struct Release {
    Version version;
    QString name; // the tag, e.g. "0.6.0"
    QDate published; // invalid if the feed gave none
    QString notes; // Markdown, from the annotated tag (see docs/releasing.md)
    QUrl pageUrl;
    QString assetName;
    QUrl assetUrl;
    qint64 assetSize = 0;
};

// What the user is offered: the newest release, and every release since
// the running version, newest first, for its notes.
struct PendingUpdate {
    QList<Release> releases;
    // When the running version was released, if the feed knows it (not
    // for a dev build).
    QDate currentPublished;

    const Release& latest() const { return releases.first(); }
};

// The asset this platform installs from: the AppImage or the NSIS setup.
QString platformAssetSuffix();

// GitHub's GET /repos/{owner}/{repo}/releases response. Drafts,
// prereleases, tags that aren't x.y.z and releases without an asset named
// *`assetSuffix` are left out. Newest first; nullopt if `json` isn't such
// a response at all.
std::optional<QList<Release>> parseReleases(const QByteArray& json, const QString& assetSuffix);

// The release `current` is, or — for a dev build — was built on top of.
std::optional<Release> releaseOf(const QList<Release>& releases, const Version& current);

// The releases newer than `current`, if any.
std::optional<PendingUpdate> pendingUpdate(const QList<Release>& releases, const Version& current);

} // namespace Update
