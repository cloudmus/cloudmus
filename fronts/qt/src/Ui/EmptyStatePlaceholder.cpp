#include "EmptyStatePlaceholder.h"

#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QVBoxLayout>

namespace Ui {

namespace {
constexpr int kLogoSize = 96;
}

EmptyStatePlaceholder::EmptyStatePlaceholder(QWidget* parent)
    : QWidget(parent)
{
    auto* logoLabel = new QLabel(this);
    // Same resource path main.cpp already uses for the window icon.
    logoLabel->setPixmap(QIcon(QStringLiteral(":/icons/icons/logo.svg")).pixmap(kLogoSize, kLogoSize));
    logoLabel->setAlignment(Qt::AlignHCenter);

    auto* titleLabel = new QLabel(tr("CloudMus"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignHCenter);

    auto* hintLabel = new QLabel(tr("Select a playlist"), this);
    hintLabel->setAlignment(Qt::AlignHCenter);
    // Muted secondary text via a real QPalette role — same convention
    // SourcePanel::capabilitiesLabel_ uses.
    hintLabel->setForegroundRole(QPalette::PlaceholderText);

    auto* column = new QVBoxLayout;
    column->addWidget(logoLabel);
    column->addWidget(titleLabel);
    column->addWidget(hintLabel);

    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    outer->addLayout(column);
    outer->addStretch(1);
}

} // namespace Ui
