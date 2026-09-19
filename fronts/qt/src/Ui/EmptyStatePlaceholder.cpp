#include "EmptyStatePlaceholder.h"

#include <QIcon>
#include <QLabel>
#include <QVBoxLayout>

#include "Typography.h"

namespace Ui {

namespace {
constexpr int kLogoSize = 96;
}

EmptyStatePlaceholder::EmptyStatePlaceholder(QWidget* parent)
    : QWidget(parent)
{
    auto* logoLabel = new QLabel(this);
    // Same resource path main.cpp already uses for the window icon — the
    // brand mark, unaffected by the Material Icons icon migration.
    logoLabel->setPixmap(QIcon(QStringLiteral(":/icons/icons/logo.svg")).pixmap(kLogoSize, kLogoSize));
    logoLabel->setAlignment(Qt::AlignHCenter);

    auto* titleLabel = new QLabel(tr("CloudMus"), this);
    titleLabel->setFont(Theme::font(Theme::TextStyle::Display));
    titleLabel->setAlignment(Qt::AlignHCenter);

    auto* hintLabel = new QLabel(tr("Select a playlist"), this);
    hintLabel->setObjectName(QStringLiteral("secondaryLabel")); // ink-secondary — see Theme::StyleSheet
    hintLabel->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    hintLabel->setAlignment(Qt::AlignHCenter);

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
