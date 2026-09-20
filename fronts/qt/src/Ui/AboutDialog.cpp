#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "Spacing.h"
#include "Typography.h"

namespace Ui {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About CloudMus"));
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why

    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(QIcon(QStringLiteral(":/icons/icons/logo.svg")).pixmap(64, 64));
    logoLabel->setAlignment(Qt::AlignCenter);

    auto* titleLabel = new QLabel(tr("CloudMus"), this);
    titleLabel->setFont(Theme::font(Theme::TextStyle::Display));
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* descriptionLabel = new QLabel(tr("A lightweight Qt frontend for cloudmus music sources."), this);
    descriptionLabel->setFont(Theme::font(Theme::TextStyle::Body));
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    QPushButton* okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setProperty("variant", "primary");
    okButton->setFont(Theme::font(Theme::TextStyle::Button));
    // See SettingsDialog.cpp's identical call for why: some platform
    // themes inject a standard icon onto Ok regardless of the active
    // QStyle.
    okButton->setIcon(QIcon());
    // See SettingsDialog.cpp's identical call for why: QDialogButtonBox
    // appears to polish its standard buttons before "variant" is set
    // above, so the QSS rule depending on it never gets re-evaluated
    // without this.
    okButton->style()->unpolish(okButton);
    okButton->style()->polish(okButton);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        Theme::Spacing::space5, Theme::Spacing::space5, Theme::Spacing::space5, Theme::Spacing::space5);
    root->setSpacing(Theme::Spacing::space3);
    root->addWidget(logoLabel);
    root->addWidget(titleLabel);
    root->addWidget(descriptionLabel);
    root->addWidget(buttons);
}

} // namespace Ui
