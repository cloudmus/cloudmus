#include "CopyButton.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>

#include "Icons.h"

namespace Ui {

namespace {
// A square icon button the size of the line it sits on: twice the
// [compact="true"] radius in StyleSheet.cpp's buttonsBlock(), so a circle.
constexpr int kSide = 20;
constexpr int kIconSide = 12;
constexpr int kCheckMs = 1500;
} // namespace

CopyButton::CopyButton(QWidget* parent)
    : QToolButton(parent)
    , revert_(new QTimer(this))
{
    setProperty("variant", "icon");
    setProperty("filled", true);
    setProperty("compact", true);
    setFixedSize(kSide, kSide);
    setIconSize(QSize(kIconSide, kIconSide));
    revert_->setSingleShot(true);
    revert_->setInterval(kCheckMs);
    connect(revert_, &QTimer::timeout, this, [this]() { setCopied(false); });
    Theme::followTheme(this, [this]() { setCopied(isCopied()); });
    connect(this, &QToolButton::clicked, this, [this]() {
        if (!source_)
            return;
        QGuiApplication::clipboard()->setText(source_());
        // The confirmation is right on the button.
        setCopied(true);
        revert_->start();
    });
}

void CopyButton::setSource(std::function<QString()> source) { source_ = std::move(source); }

bool CopyButton::isCopied() const { return property("copied").toBool(); }

void CopyButton::setCopied(bool copied)
{
    setProperty("copied", copied);
    setIcon(Theme::icon(copied ? QStringLiteral("check") : QStringLiteral("content_copy"),
        copied ? Theme::IconColor::Accent : Theme::IconColor::InkSecondary, kIconSide));
}

// Shown again, it's for something else (a new code).
void CopyButton::hideEvent(QHideEvent* event)
{
    QToolButton::hideEvent(event);
    revert_->stop();
    setCopied(false);
}

} // namespace Ui
