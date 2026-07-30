#include "AuthBanner.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Ui {

AuthBanner::AuthBanner(QWidget* parent)
    : QWidget(parent)
{
    messageLabel_ = new QLabel(this);
    messageLabel_->setWordWrap(true);

    formLayout_ = new QHBoxLayout;
    submitButton_ = new QPushButton(tr("Submit"), this);
    submitButton_->hide();
    connect(submitButton_, &QPushButton::clicked, this, [this]() {
        QJsonObject fields;
        for (auto it = fieldEdits_.constBegin(); it != fieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->text());
        }
        emit submitRequested(currentSourceId_, fields);
    });

    busyIndicator_ = new QProgressBar(this);
    busyIndicator_->setRange(0, 0); // indeterminate
    busyIndicator_->setFixedWidth(80);
    busyIndicator_->setMaximumHeight(6);
    busyIndicator_->hide();

    auto* row = new QHBoxLayout;
    row->addWidget(messageLabel_, 1);
    row->addLayout(formLayout_);
    row->addWidget(submitButton_);
    row->addWidget(busyIndicator_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 4, 8, 4);
    root->addLayout(row);

    hide();
}

void AuthBanner::clearFormFields()
{
    qDeleteAll(fieldEdits_);
    fieldEdits_.clear();
    QLayoutItem* item = nullptr;
    while ((item = formLayout_->takeAt(0)) != nullptr) {
        delete item;
    }
}

void AuthBanner::showPrompt(const QString& sourceId, const QJsonObject& params)
{
    currentSourceId_ = sourceId;
    clearFormFields();
    setSubmitBusy(false);

    const QString flow = params.value(QStringLiteral("flow")).toString();
    if (flow == QStringLiteral("deviceCode")) {
        const QString url = params.value(QStringLiteral("url")).toString();
        const QString code = params.value(QStringLiteral("code")).toString();
        messageLabel_->setText(tr("%1: open %2 and enter code %3").arg(sourceId, url, code));
        submitButton_->hide();
    } else if (flow == QStringLiteral("usernamePassword")) {
        messageLabel_->setText(tr("%1: sign in").arg(sourceId));
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        for (const QJsonValue& v : fields) {
            const QJsonObject field = v.toObject();
            const QString name = field.value(QStringLiteral("name")).toString();
            auto* edit = new QLineEdit(this);
            if (field.value(QStringLiteral("secret")).toBool()) {
                edit->setEchoMode(QLineEdit::Password);
            }
            edit->setPlaceholderText(name);
            formLayout_->addWidget(edit);
            fieldEdits_.insert(name, edit);
        }
        submitButton_->show();
    } else if (flow == QStringLiteral("oauthRedirect")) {
        const QString url = params.value(QStringLiteral("url")).toString();
        QDesktopServices::openUrl(QUrl(url));
        messageLabel_->setText(tr("%1: continue in your browser").arg(sourceId));
        submitButton_->hide();
    } else {
        messageLabel_->setText(tr("%1: authentication required").arg(sourceId));
        submitButton_->hide();
    }
    show();
}

void AuthBanner::showAuthenticated(const QString& sourceId)
{
    if (sourceId != currentSourceId_)
        return;
    hide();
}

void AuthBanner::showError(const QString& sourceId, const QString& message)
{
    if (sourceId != currentSourceId_)
        return;
    setSubmitBusy(false);
    messageLabel_->setText(tr("%1: %2").arg(sourceId, message));
}

void AuthBanner::setSubmitBusy(bool busy)
{
    submitButton_->setEnabled(!busy);
    busyIndicator_->setVisible(busy);
}

} // namespace Ui
