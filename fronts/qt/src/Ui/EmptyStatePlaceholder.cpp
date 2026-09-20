#include "EmptyStatePlaceholder.h"

#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "Tokens.h"
#include "Typography.h"

namespace Ui {

namespace {
// Bigger than the design system's own Display/BodySecondary text-style
// pixel sizes (22px/12px) on purpose: this is the one place in the app
// that's ever just a bare, empty content area with nothing else on
// screen, so it reads as a deliberate branding moment rather than an
// ordinary heading — same reasoning that already justifies a logo this
// large only here.
constexpr int kLogoSize = 140;
constexpr int kTitlePixelSize = 32;
constexpr int kHintPixelSize = 15;
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
    QFont titleFont = Theme::font(Theme::TextStyle::Display);
    titleFont.setPixelSize(kTitlePixelSize);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignHCenter);

    auto* hintLabel = new QLabel(tr("Select a playlist"), this);
    hintLabel->setObjectName(QStringLiteral("secondaryLabel")); // ink-secondary — see Theme::StyleSheet
    QFont hintFont = Theme::font(Theme::TextStyle::BodySecondary);
    hintFont.setPixelSize(kHintPixelSize);
    hintLabel->setFont(hintFont);
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

void EmptyStatePlaceholder::paintEvent(QPaintEvent*)
{
    // Otherwise unstyled (a plain QWidget, no QSS rule targets it) — left
    // at Qt's default Fusion palette background, a light gray visibly
    // mismatched against the rest of this dark-themed app. surface0, not
    // surface100: per the design mockup (pixel-sampled directly —
    // sidebar/toolbar are #1C1714 = surface100, but the main content
    // area and the track list are both #14100D = surface0), this widget
    // sits in the content area, not the chrome, and needs to match its
    // sibling trackListView_ (see StyleSheet.cpp's QListView rule).
    QPainter(this).fillRect(rect(), Theme::palette().surface0);
}

} // namespace Ui
