#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Rpc {

// Mirrors fronts/tui/cloudmus_tui/discovery.py's BackendManifest.
struct BackendManifest {
    QString id;
    QString name;
    // Manifest keys "name[<language>]" (as in a KDE .desktop file): the name
    // in other languages, by language code; `name` is the English fallback.
    QHash<QString, QString> localizedNames;
    QStringList argv;
    QString protocolVersion;
    QString manifestPath;
    // Absolute path to the optional monochrome sidebar icon SVG (manifest
    // "icon", resolved relative to the manifest file); empty if none/missing.
    QString iconPath;
    // Absolute path to the optional full-color logo SVG (manifest "logo",
    // resolved the same way) shown large on the source's page; empty if
    // none/missing.
    QString logoPath;

    // The name in `locale` ("ru", "pt-BR", "pt_BR"): the exact tag, then its
    // language; empty if the manifest has none.
    QString localizedName(const QString& locale) const;
    // That, else `name`.
    QString displayName(const QString& locale) const;
};

// Scans ~/.config/cloudmus/backends.d/*.json for installed backend
// manifests, plus — if CLOUDMUS_DEV_BACKENDS is truthy (1/true/yes) —
// repo-relative backends/*/manifest.json (only filling in ids not already
// provided by the installed set), matching discovery.py's dev-mode
// fallback. The repo root for dev-mode is CLOUDMUS_DEV_REPO_ROOT, a
// compile-time constant set by CMakeLists.txt to the checkout's root.
// Also the *.json in $CLOUDMUS_BACKENDS_DIR, with "python3" in their argv
// replaced by $CLOUDMUS_BACKENDS_PYTHON.
QList<BackendManifest> discoverManifests();

} // namespace Rpc
