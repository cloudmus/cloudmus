#include "InfoDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "DialogButtons.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui {

namespace {
constexpr int kDialogWidth = 460;
} // namespace

InfoDialog::InfoDialog(const QString& title, const QString& heading, const QString& text, const QString& actionText,
    QWidget* parent, const QString& closeText)
    : QDialog(parent)
{
    setWindowTitle(title);
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why
    setWindowModality(Qt::ApplicationModal);

    auto* headingLabel = new QLabel(heading, this);
    headingLabel->setFont(Theme::font(Theme::TextStyle::Display)); // as UpdateDialog's
    headingLabel->setWordWrap(true);

    auto* textLabel = new QLabel(text, this);
    textLabel->setFont(Theme::font(Theme::TextStyle::Body));
    textLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (!closeText.isEmpty())
        buttons->button(QDialogButtonBox::Close)->setText(closeText);
    if (actionText.isEmpty()) {
        styleDialogButton(buttons->button(QDialogButtonBox::Close), "primary");
    } else {
        styleDialogButton(buttons->button(QDialogButtonBox::Close), "secondary");
        QPushButton* action = buttons->addButton(actionText, QDialogButtonBox::AcceptRole);
        styleDialogButton(action, "primary");
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space5);
    root->setSpacing(Theme::Spacing::space3);
    root->addWidget(headingLabel);
    root->addWidget(textLabel);
    root->addSpacing(Theme::Spacing::space3);
    root->addWidget(buttons);

    // Sized here rather than by show() — see AboutDialog's constructor
    // (Wayland replaying a stale 100×30 size with word-wrapped labels).
    resize(sizeHint());
}

QSize InfoDialog::sizeHint() const { return { kDialogWidth, heightForWidth(kDialogWidth) }; }

} // namespace Ui
