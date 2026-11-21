#include "Autostart.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#import <Foundation/Foundation.h>

// Built with ARC (see CMakeLists.txt).

// A launchd agent in ~/Library/LaunchAgents, not SMAppService's login
// item: that one starts the bundle with no arguments and gives the app no
// way to tell a login launch from any other, while startHiddenAtLogin
// needs exactly that. An agent passes kLaunchedAtLoginArgument the way the
// XDG entry does, and works on macOS 12 too. macOS 13+ lists it under
// System Settings → General → Login Items → "Allow in the Background",
// named and drawn after the bundle (AssociatedBundleIdentifiers).
//
// Only written and removed, never loaded with launchctl: launchd loads
// the folder at login by itself, and loading it now would run it (RunAtLoad)
// — a second CloudMus.

namespace Integration::Autostart {

namespace {

    constexpr auto kLabel = "io.github.cloudmus.CloudMus.autostart";

    QString agentPath()
    {
        return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + QLatin1String(kLabel)
            + QStringLiteral(".plist");
    }

    NSURL* agentUrl() { return [NSURL fileURLWithPath:agentPath().toNSString()]; }

    NSDictionary* readAgent() { return [NSDictionary dictionaryWithContentsOfURL:agentUrl()]; }

    // The executable the agent starts, empty if it names none.
    QString agentProgram(NSDictionary* agent)
    {
        id arguments = agent[@"ProgramArguments"];
        if (![arguments isKindOfClass:[NSArray class]] || [arguments count] == 0
            || ![[arguments firstObject] isKindOfClass:[NSString class]])
            return QString();
        return QString::fromNSString([arguments firstObject]);
    }

    bool writeAgent()
    {
        const QString folder = QFileInfo(agentPath()).absolutePath();
        if (!QDir().mkpath(folder)) {
            qWarning() << "Autostart: can't create" << folder;
            return false;
        }
        NSMutableDictionary* agent = [@{
            @"Label" : @(kLabel),
            @"ProgramArguments" : @[
                QCoreApplication::applicationFilePath().toNSString(), @(kLaunchedAtLoginArgument)
            ],
            @"RunAtLoad" : @YES,
            // The user's own app, not a background daemon: not throttled.
            @"ProcessType" : @"Interactive",
            @"LimitLoadToSessionType" : @"Aqua",
        } mutableCopy];
        if (NSString* bundleId = [[NSBundle mainBundle] bundleIdentifier])
            agent[@"AssociatedBundleIdentifiers"] = @[ bundleId ];
        NSError* error = nil;
        if (![agent writeToURL:agentUrl() error:&error]) {
            qWarning() << "Autostart: can't write" << agentPath() << QString::fromNSString(error.localizedDescription);
            return false;
        }
        return true;
    }

} // namespace

bool isEnabled()
{
    NSDictionary* agent = readAgent();
    return agent && ![agent[@"Disabled"] boolValue];
}

bool setEnabled(bool enabled)
{
    if (enabled)
        return writeAgent();
    const QString path = agentPath();
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        qWarning() << "Autostart: can't remove" << path;
        return false;
    }
    return true;
}

void refresh()
{
    NSDictionary* agent = readAgent();
    if (!agent || [agent[@"Disabled"] boolValue])
        return;
    // Only when the app it names is gone (moved to another folder,
    // renamed): a build run from its build directory must not take over
    // the installed app's agent. An update replaces the bundle in place.
    const QString target = agentProgram(agent);
    if (target.isEmpty() || !QFileInfo::exists(target)) {
        qInfo() << "Autostart: agent pointed at" << target << "(missing) - re-pointing at"
                << QCoreApplication::applicationFilePath();
        writeAgent();
    }
}

} // namespace Integration::Autostart
