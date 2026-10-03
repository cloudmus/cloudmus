#pragma once

#include <QString>

#include "ReleaseFeed.h"

// Installing a downloaded update — per platform: InstallerPosix.cpp
// replaces the AppImage this runs from, InstallerWin.cpp runs the NSIS
// setup (packaging/windows/cloudmus.nsi's /UPDATE mode). Either way the
// new version starts by itself once this process has exited.
namespace Update::Installer {

// Why this run can't install an update by itself, for the user — empty if
// it can. Then the user is sent to the release page instead.
QString unavailableReason();

// Where `release`'s file is downloaded to: for the AppImage, next to it,
// so putting it in place is a rename on the same file system.
QString downloadPath(const Release& release);

// Puts the downloaded file in place and starts what finishes the job once
// this process has exited — the caller quits right after. On failure
// nothing was started, and `error` says why.
bool launch(const QString& downloadedPath, const Release& release, QString* error);

#ifndef Q_OS_WIN
// The path the new AppImage goes to: its own versioned name in the old
// one's folder, unless the user renamed the old one — then that name.
QString appImageTarget(const QString& currentPath, const QString& assetName);
// Makes the downloaded file executable and moves it to `target`, removing
// the old AppImage if that's elsewhere. The running AppImage doesn't mind:
// its mount keeps the old file open.
bool replaceAppImage(const QString& downloadedPath, const QString& currentPath, const QString& target, QString* error);
#endif

} // namespace Update::Installer
