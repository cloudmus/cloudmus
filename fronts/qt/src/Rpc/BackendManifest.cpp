#include "BackendManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>

#ifndef CLOUDMUS_DEV_REPO_ROOT
#define CLOUDMUS_DEV_REPO_ROOT ""
#endif

namespace Rpc {

namespace {

bool envFlagTruthy(const QString& value)
{
    const QString v = value.trimmed().toLower();
    return v == QStringLiteral("1") || v == QStringLiteral("true") || v == QStringLiteral("yes");
}

// Dev-mode manifests carry a bare "python"/"python3" in argv[0], resolved
// via PATH — correct for fronts/tui, whose own process only runs at all
// once its venv is already activated (activation prepends .venv/bin to
// PATH, so the *same* PATH is inherited by the subprocess it spawns). A
// compiled front like this one has no such activation step, so a bare
// interpreter name on PATH silently resolves to the system Python instead
// — which doesn't have the backend packages installed (they're only
// `pip install -e`'d into the repo's .venv) and fails at import time. Only
// applied to *dev-mode* discovery (repo-relative backends/*/manifest.json);
// installed manifests (~/.config/cloudmus/backends.d/) are a packaging
// concern and are left untouched.
void preferRepoVenvInterpreter(BackendManifest& manifest, const QString& repoRoot)
{
    if (manifest.argv.isEmpty())
        return;
    const QString& interpreter = manifest.argv.first();
    if (interpreter != QStringLiteral("python") && interpreter != QStringLiteral("python3"))
        return;
    const QString venvPython = repoRoot + QStringLiteral("/.venv/bin/python3");
    if (QFile::exists(venvPython)) {
        manifest.argv[0] = venvPython;
    }
}

bool parseManifest(const QString& path, BackendManifest& out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("id")) || !obj.contains(QStringLiteral("argv")))
        return false;

    out.id = obj.value(QStringLiteral("id")).toString();
    out.name = obj.value(QStringLiteral("name")).toString();
    out.protocolVersion = obj.value(QStringLiteral("protocolVersion")).toString();
    out.manifestPath = path;
    out.iconPath.clear();
    const QString icon = obj.value(QStringLiteral("icon")).toString();
    if (!icon.isEmpty()) {
        const QString resolved = QDir(QFileInfo(path).absolutePath()).absoluteFilePath(icon);
        if (QFileInfo::exists(resolved))
            out.iconPath = resolved;
    }
    out.argv.clear();
    for (const QJsonValue& v : obj.value(QStringLiteral("argv")).toArray()) {
        out.argv.append(v.toString());
    }
    return !out.id.isEmpty() && !out.argv.isEmpty();
}

void scanDir(const QDir& dir, QList<BackendManifest>& out, QSet<QString>& seenIds)
{
    if (!dir.exists())
        return;
    const QStringList jsonFiles = dir.entryList(QStringList { QStringLiteral("*.json") }, QDir::Files);
    for (const QString& fileName : jsonFiles) {
        BackendManifest manifest;
        if (!parseManifest(dir.filePath(fileName), manifest))
            continue;
        if (seenIds.contains(manifest.id))
            continue;
        seenIds.insert(manifest.id);
        out.append(manifest);
    }
}

} // namespace

QList<BackendManifest> discoverManifests()
{
    QList<BackendManifest> result;
    QSet<QString> seenIds;

    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    scanDir(QDir(configHome + QStringLiteral("/cloudmus/backends.d")), result, seenIds);

    const QString devFlag = QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLOUDMUS_DEV_BACKENDS"));
    if (envFlagTruthy(devFlag)) {
        const QString repoRoot = QStringLiteral(CLOUDMUS_DEV_REPO_ROOT);
        if (!repoRoot.isEmpty()) {
            QDir backendsDir(repoRoot + QStringLiteral("/backends"));
            const QStringList subdirs = backendsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& subdir : subdirs) {
                QDir dir(backendsDir.filePath(subdir));
                if (!dir.exists(QStringLiteral("manifest.json")))
                    continue;
                BackendManifest manifest;
                if (!parseManifest(dir.filePath(QStringLiteral("manifest.json")), manifest))
                    continue;
                if (seenIds.contains(manifest.id))
                    continue;
                preferRepoVenvInterpreter(manifest, repoRoot);
                seenIds.insert(manifest.id);
                result.append(manifest);
            }
        }
    }

    return result;
}

} // namespace Rpc
