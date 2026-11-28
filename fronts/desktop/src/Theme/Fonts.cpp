#include "Fonts.h"

#include <QFontDatabase>
#include <QLoggingCategory>
#include <QString>

namespace Theme {

namespace {
Q_LOGGING_CATEGORY(lcTheme, "cloudmus.theme")

void addOrWarn(const char* resourcePath)
{
    if (QFontDatabase::addApplicationFont(QString::fromLatin1(resourcePath)) == -1) {
        qCWarning(lcTheme) << "failed to register application font:" << resourcePath;
    }
}
} // namespace

void registerApplicationFonts()
{
    addOrWarn(":/fonts/fonts/Manrope-Regular.ttf");
    addOrWarn(":/fonts/fonts/Manrope-Medium.ttf");
    addOrWarn(":/fonts/fonts/Manrope-SemiBold.ttf");
    addOrWarn(":/fonts/fonts/Manrope-Bold.ttf");
}

} // namespace Theme
