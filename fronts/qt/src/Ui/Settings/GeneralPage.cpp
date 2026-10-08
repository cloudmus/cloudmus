#include "Settings/GeneralPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyleHints>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "Analytics.h"
#include "Autostart.h"
#include "Languages.h"
#include "Settings.h"
#include "Spacing.h"
#include "ThemeCrossfade.h"
#include "Tokens.h"
#include "Typography.h"
#include "WindowGlass.h"

namespace Ui::Settings {

GeneralPage::GeneralPage(Config::Settings& settings, App::Analytics& analytics, QObject* parent)
    : Page(parent)
    , settings_(settings)
    , analytics_(analytics)
{
}

QString GeneralPage::title() const { return tr("General"); }

QWidget* GeneralPage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    const auto makeCheck = [this, widget](const QString& text, bool checked) {
        auto* check = new QCheckBox(text, widget);
        check->setChecked(checked);
        check->setFont(Theme::font(Theme::TextStyle::Body));
        connect(check, &QCheckBox::toggled, this, &Page::dirtyChanged);
        return check;
    };

    launchAtLogin_ = Integration::Autostart::isEnabled();
    launchAtLoginCheck_ = makeCheck(tr("Launch CloudMus when you log in"), launchAtLogin_);
    startHiddenCheck_ = makeCheck(tr("Start hidden in the tray"), settings_.startHiddenAtLogin());
    startHiddenCheck_->setEnabled(launchAtLogin_);
    connect(launchAtLoginCheck_, &QCheckBox::toggled, startHiddenCheck_, &QCheckBox::setEnabled);
    resumePlaybackCheck_
        = makeCheck(tr("Resume playback on start if it was playing at exit"), settings_.resumePlaybackAtStartup());
    closeToTrayCheck_ = makeCheck(
        tr("Closing the window minimizes to the tray instead of quitting"), settings_.closeMinimizesToTray());
    trackNotificationsCheck_
        = makeCheck(tr("Show a notification when the track changes"), settings_.trackNotifications());

    using ColorScheme = Config::Settings::ColorScheme;
    colorSchemeCombo_ = new QComboBox(widget);
    colorSchemeCombo_->setFont(Theme::font(Theme::TextStyle::Body));
    // See DownloadsPage: only a QStyledItemDelegate honors the ::item QSS.
    colorSchemeCombo_->setItemDelegate(new QStyledItemDelegate(colorSchemeCombo_));
    colorSchemeCombo_->addItem(tr("System"), int(ColorScheme::System));
    colorSchemeCombo_->addItem(tr("Light"), int(ColorScheme::Light));
    colorSchemeCombo_->addItem(tr("Dark"), int(ColorScheme::Dark));
    colorSchemeCombo_->setCurrentIndex(colorSchemeCombo_->findData(int(settings_.colorScheme())));
    connect(colorSchemeCombo_, &QComboBox::currentIndexChanged, this, &Page::dirtyChanged);
    auto* colorSchemeRow = new QHBoxLayout;
    colorSchemeRow->setSpacing(Theme::Spacing::space3);
    auto* colorSchemeLabel = new QLabel(tr("Theme:"), widget);
    colorSchemeLabel->setFont(Theme::font(Theme::TextStyle::Body));
    colorSchemeRow->addWidget(colorSchemeLabel);
    colorSchemeRow->addWidget(colorSchemeCombo_);
    colorSchemeRow->addStretch(1);

    languageCombo_ = new QComboBox(widget);
    languageCombo_->setFont(Theme::font(Theme::TextStyle::Body));
    languageCombo_->setItemDelegate(new QStyledItemDelegate(languageCombo_));
    languageCombo_->addItem(tr("Automatic"), QString());
    // Each in its own language, so one can be found whatever is shown now.
    for (const I18n::Language& language : I18n::supportedLanguages())
        languageCombo_->addItem(language.nativeName, language.code);
    languageCombo_->setCurrentIndex(languageCombo_->findData(settings_.language()));
    connect(languageCombo_, &QComboBox::currentIndexChanged, this, &Page::dirtyChanged);
    auto* languageRow = new QHBoxLayout;
    languageRow->setSpacing(Theme::Spacing::space3);
    auto* languageLabel = new QLabel(tr("Language:"), widget);
    languageLabel->setFont(Theme::font(Theme::TextStyle::Body));
    languageRow->addWidget(languageLabel);
    languageRow->addWidget(languageCombo_);
    languageRow->addStretch(1);

    glassCheck_ = makeCheck(tr("Glass background: blur what's behind the window"), glassWanted());
    // Where the app can't ask for the blur itself, the window can still be
    // made see-through for a desktop extension to blur — said so, since
    // without one it's just see-through. Where it can't be translucent at
    // all, the window stays opaque whatever this says.
    using Integration::WindowGlass::Support;
    const Support glassSupport = Integration::WindowGlass::support();
    glassCheck_->setEnabled(glassSupport != Support::None);
    auto* glassHint = new QLabel(widget);
    glassHint->setProperty("hint", true);
    glassHint->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    glassHint->setWordWrap(true);
    if (glassSupport == Support::SeeThrough)
        glassHint->setText(tr("This desktop doesn't blur behind windows by itself, so without help the window is "
                              "just see-through. On GNOME, install the Blur my Shell extension and add "
                              "\"cloudmus-qt\" to its application blur list."));
    else
        glassHint->setText(tr("Not supported by this desktop."));
    glassHint->setVisible(glassSupport != Support::Blur);

    analyticsCheck_ = makeCheck(tr("Send usage statistics to Google Analytics"), settings_.analyticsEnabled());
    crashReportsCheck_ = makeCheck(tr("Send crash reports to Sentry"), settings_.crashReportsEnabled());
    // Sentry is started once, at launch.
    crashReportsCheck_->setToolTip(tr("Takes effect the next time CloudMus starts."));

    // "Start hidden" only applies to a login launch — indented under it.
    auto* startHiddenRow = new QHBoxLayout;
    startHiddenRow->setContentsMargins(Theme::Spacing::space5, 0, 0, 0);
    startHiddenRow->addWidget(startHiddenCheck_);

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space2);
    layout->addWidget(launchAtLoginCheck_);
    layout->addLayout(startHiddenRow);
    layout->addWidget(resumePlaybackCheck_);
    layout->addWidget(closeToTrayCheck_);
    layout->addWidget(trackNotificationsCheck_);
    layout->addSpacing(Theme::Spacing::space3);
    layout->addLayout(colorSchemeRow);
    layout->addLayout(languageRow);
    layout->addWidget(glassCheck_);
    layout->addWidget(glassHint);
    layout->addSpacing(Theme::Spacing::space3);
    layout->addWidget(analyticsCheck_);
    layout->addWidget(crashReportsCheck_);
    return widget;
}

bool GeneralPage::glassWanted() const { return Theme::glassWanted(settings_.glassBackground()); }

bool GeneralPage::isDirty() const
{
    return launchAtLoginCheck_
        && (launchAtLoginCheck_->isChecked() != launchAtLogin_
            || startHiddenCheck_->isChecked() != settings_.startHiddenAtLogin()
            || resumePlaybackCheck_->isChecked() != settings_.resumePlaybackAtStartup()
            || closeToTrayCheck_->isChecked() != settings_.closeMinimizesToTray()
            || trackNotificationsCheck_->isChecked() != settings_.trackNotifications()
            || colorSchemeCombo_->currentData().toInt() != int(settings_.colorScheme())
            || languageCombo_->currentData().toString() != settings_.language()
            || glassCheck_->isChecked() != glassWanted() || analyticsCheck_->isChecked() != settings_.analyticsEnabled()
            || crashReportsCheck_->isChecked() != settings_.crashReportsEnabled());
}

Rpc::Task<bool> GeneralPage::apply()
{
    if (!launchAtLoginCheck_)
        co_return true;
    if (launchAtLoginCheck_->isChecked() != launchAtLogin_
        && Integration::Autostart::setEnabled(launchAtLoginCheck_->isChecked()))
        launchAtLogin_ = launchAtLoginCheck_->isChecked();
    settings_.setStartHiddenAtLogin(startHiddenCheck_->isChecked());
    settings_.setResumePlaybackAtStartup(resumePlaybackCheck_->isChecked());
    settings_.setCloseMinimizesToTray(closeToTrayCheck_->isChecked());
    if (trackNotificationsCheck_->isChecked() != settings_.trackNotifications())
        settings_.setTrackNotifications(trackNotificationsCheck_->isChecked());
    const auto scheme = Config::Settings::ColorScheme(colorSchemeCombo_->currentData().toInt());
    if (scheme != settings_.colorScheme()) {
        settings_.setColorScheme(scheme);
        crossfadeThemeChange([scheme]() { applyColorScheme(scheme); });
    }
    // Only stored: the language is switched once the Settings window
    // closes — see Ui::MainWindow::showSettingsDialog().
    settings_.setLanguage(languageCombo_->currentData().toString());
    // Stored only once the user goes against the default, so an untouched
    // setting keeps following the desktop's light/dark scheme.
    if (glassCheck_->isChecked() != glassWanted())
        settings_.setGlassBackground(glassCheck_->isChecked());
    if (analyticsCheck_->isChecked() != settings_.analyticsEnabled())
        analytics_.setEnabled(analyticsCheck_->isChecked());
    if (crashReportsCheck_->isChecked() != settings_.crashReportsEnabled())
        settings_.setCrashReportsEnabled(crashReportsCheck_->isChecked());
    // Switched once the Settings window closes — see
    // Ui::MainWindow::showSettingsDialog().
    co_return true;
}

void GeneralPage::applyColorScheme(Config::Settings::ColorScheme scheme)
{
    using ColorScheme = Config::Settings::ColorScheme;
    if (scheme == ColorScheme::System)
        Theme::setModeOverride(std::nullopt);
    else
        Theme::setModeOverride(scheme == ColorScheme::Dark ? Theme::Mode::Dark : Theme::Mode::Light);
#ifdef Q_OS_WIN
    // Windows draws the title bar light or dark after Qt's color scheme,
    // which otherwise follows the system's — a white title bar over a dark
    // app. Setting it recolors every window's frame, open ones included.
    // Not elsewhere: there the scheme is also what Qt's platform theme and
    // the desktop's decorations go by, and those already look right.
    QGuiApplication::styleHints()->setColorScheme(scheme == ColorScheme::System ? Qt::ColorScheme::Unknown
            : scheme == ColorScheme::Dark                                       ? Qt::ColorScheme::Dark
                                                                                : Qt::ColorScheme::Light);
#endif
}

} // namespace Ui::Settings
