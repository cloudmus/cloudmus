#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QHBoxLayout;
class QProgressBar;

namespace Ui {

// Thin bar between the title bar and the content area — not a modal dialog
// — mirroring #auth-banner in fronts/tui/cloudmus_tui/app.py. Renders
// whichever of the three auth.flow shapes (deviceCode/usernamePassword/
// oauthRedirect) the source sent, from the raw auth/prompt payload (see
// Rpc::RpcClient::onAuthPromptRaw's doc comment for why it's raw JSON, not
// a generated struct).
class AuthBanner : public QWidget {
    Q_OBJECT

public:
    explicit AuthBanner(QWidget* parent = nullptr);

    void showPrompt(const QString& sourceId, const QJsonObject& params);
    void showAuthenticated(const QString& sourceId);
    void showError(const QString& sourceId, const QString& message);
    void setSubmitBusy(bool busy);

signals:
    void submitRequested(QString sourceId, QJsonObject fields);

private:
    void clearFormFields();

    QString currentSourceId_;
    QLabel* messageLabel_ = nullptr;
    QHBoxLayout* formLayout_ = nullptr;
    QPushButton* submitButton_ = nullptr;
    QProgressBar* busyIndicator_ = nullptr;
    QHash<QString, QLineEdit*> fieldEdits_;
};

} // namespace Ui
