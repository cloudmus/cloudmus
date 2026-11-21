#include "AppIdentity.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <windows.h>

#include <propsys.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cwchar>

namespace Integration::AppIdentity {

namespace {
// What an unpackaged app registers instead of the Start Menu shortcut with
// System.AppUserModel.ID that the shell otherwise looks for — the way the
// Windows App SDK registers one for its toasts.
constexpr auto kRegistryKey = "HKEY_CURRENT_USER\\Software\\Classes\\AppUserModelId\\";

// Own literal copies, not the SDK's DEFINE_GUID'd ones: those need storage
// from a lib we don't otherwise link (as in TaskbarThumbButtons.cpp).
constexpr GUID kClsidShellLink = { 0x00021401, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
constexpr GUID kIidShellLinkW = { 0x000214f9, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
constexpr GUID kIidPersistFile = { 0x0000010b, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
constexpr GUID kIidPropertyStore = { 0x886d8eeb, 0x8cf2, 0x4446, { 0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99 } };
// PKEY_AppUserModel_ID
constexpr PROPERTYKEY kKeyAppUserModelId
    = { { 0x9f4c2855, 0x9f79, 0x4b39, { 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3 } }, 5 };

// The media flyout (SMTC) names a session's app only through a Start Menu
// shortcut carrying the same AppUserModelID — the registry entry above isn't
// enough there, it says "Unknown app". The installer's shortcut has no id
// (NSIS can't set one without a plugin, and rewrites the shortcut on every
// update), so the app tags it itself. Only the installer's shortcut, and
// only when it points at this exe: a dev build doesn't touch an installed
// copy's shortcut, and none is created where there was none.
void tagStartMenuShortcut()
{
    const QString lnkPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)
        + QStringLiteral("/CloudMus/CloudMus.lnk");
    if (!QFile::exists(lnkPath))
        return;
    const std::wstring lnk = QDir::toNativeSeparators(lnkPath).toStdWString();

    using Microsoft::WRL::ComPtr;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    ComPtr<IPropertyStore> store;
    HRESULT hr = CoCreateInstance(kClsidShellLink, nullptr, CLSCTX_INPROC_SERVER, kIidShellLinkW, &link);
    if (SUCCEEDED(hr))
        hr = link->QueryInterface(kIidPersistFile, &file);
    if (SUCCEEDED(hr))
        hr = file->Load(lnk.c_str(), STGM_READWRITE);
    if (FAILED(hr)) {
        qWarning() << "AppIdentity: can't open" << lnkPath << Qt::hex << hr;
        return;
    }

    wchar_t target[MAX_PATH] = { };
    if (FAILED(link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)))
        return;
    const QString targetPath = QFileInfo(QString::fromWCharArray(target)).canonicalFilePath();
    const QString selfPath = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    if (targetPath.compare(selfPath, Qt::CaseInsensitive) != 0)
        return;

    hr = link->QueryInterface(kIidPropertyStore, &store);
    if (FAILED(hr)) {
        qWarning() << "AppIdentity: no property store on" << lnkPath << Qt::hex << hr;
        return;
    }
    PROPVARIANT current;
    PropVariantInit(&current);
    const bool tagged = SUCCEEDED(store->GetValue(kKeyAppUserModelId, &current)) && current.vt == VT_LPWSTR
        && current.pwszVal && std::wcscmp(current.pwszVal, kAppUserModelId) == 0;
    PropVariantClear(&current);
    if (tagged)
        return;

    PROPVARIANT id;
    PropVariantInit(&id);
    id.vt = VT_LPWSTR;
    id.pwszVal = const_cast<wchar_t*>(kAppUserModelId); // read only: not cleared
    hr = store->SetValue(kKeyAppUserModelId, id);
    if (SUCCEEDED(hr))
        hr = store->Commit();
    if (SUCCEEDED(hr))
        hr = file->Save(nullptr, TRUE);
    if (FAILED(hr)) {
        qWarning() << "AppIdentity: can't set the AppUserModelID on" << lnkPath << Qt::hex << hr;
        return;
    }
    // Lets the shell re-read the shortcut now, not on its next scan.
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH, lnk.c_str(), nullptr);
}

// The shell takes the icon from a file only; written again only when the
// icon changed, not at every start.
QString writeIconFile(const QIcon& icon)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!icon.pixmap(QSize(256, 256), 1.0).save(&buffer, "PNG"))
        return { };
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString path = dir + QStringLiteral("/app-icon.png");
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == png)
        return path;
    existing.close();
    QDir().mkpath(dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) {
        qWarning() << "AppIdentity: can't write" << path << file.errorString();
        return { };
    }
    return path;
}
} // namespace

void registerAppUserModelId(const QIcon& icon)
{
    QSettings settings(
        QString::fromLatin1(kRegistryKey) + QString::fromWCharArray(kAppUserModelId), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("DisplayName"), QStringLiteral("CloudMus"));
    const QString iconPath = writeIconFile(icon);
    if (!iconPath.isEmpty())
        settings.setValue(QStringLiteral("IconUri"), QDir::toNativeSeparators(iconPath));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning() << "AppIdentity: registry update failed" << settings.status();
    tagStartMenuShortcut();
}

} // namespace Integration::AppIdentity
