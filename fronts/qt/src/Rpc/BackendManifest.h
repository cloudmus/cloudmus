#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Rpc {

// Mirrors fronts/tui/cloudmus_tui/discovery.py's BackendManifest.
struct BackendManifest {
    QString id;
    QString name;
    QStringList argv;
    QString protocolVersion;
    QString manifestPath;
    // Absolute path to the optional monochrome sidebar icon SVG (manifest
    // "icon", resolved relative to the manifest file); empty if none/missing.
    QString iconPath;
};

// Scans ~/.config/cloudmus/backends.d/*.json for installed backend
// manifests, plus — if CLOUDMUS_DEV_BACKENDS is truthy (1/true/yes) —
// repo-relative backends/*/manifest.json (only filling in ids not already
// provided by the installed set), matching discovery.py's dev-mode
// fallback. The repo root for dev-mode is CLOUDMUS_DEV_REPO_ROOT, a
// compile-time constant set by CMakeLists.txt to the checkout's root.
QList<BackendManifest> discoverManifests();

} // namespace Rpc
