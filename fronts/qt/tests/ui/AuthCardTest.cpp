#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "AuthCard.h"
#include "CoverArtCache.h"
#include "SourcePanel.h"

namespace Tests {

namespace {

// The longest message a source may plausibly give, so it wraps.
const QString kLongMessage = QStringLiteral(
    "Sign in with a Spotify <b>Premium</b> account. The free tier can't be played: the player streams the music "
    "itself, which Spotify allows only on a paid account.");

QLabel* labelContaining(QWidget* root, const QString& text)
{
    for (QLabel* label : root->findChildren<QLabel*>()) {
        if (label->isVisibleTo(root) && label->text().contains(text))
            return label;
    }
    return nullptr;
}

QPushButton* visibleButton(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->isVisibleTo(root) && button->text() == text)
            return button;
    }
    return nullptr;
}

} // namespace

class AuthCardTest : public QObject {
    Q_OBJECT

private slots:
    // A wrapped message gets the height of all its lines — on the source's
    // page, where the card sits in a scrolling column.
    void aLongMessageIsNotCutOff()
    {
        Covers::CoverArtCache covers;
        Ui::SourcePanel panel(&covers);
        panel.resize(700, 500);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.showPrompt({ { QStringLiteral("flow"), QStringLiteral("oauthRedirect") },
            { QStringLiteral("url"), QStringLiteral("https://accounts.example/authorize") },
            { QStringLiteral("message"), kLongMessage } });
        QApplication::processEvents();

        QLabel* message = labelContaining(&panel, QStringLiteral("Premium"));
        QVERIFY(message != nullptr);
        QVERIFY(message->height() >= message->heightForWidth(message->width()));
    }

    // The browser opens only when asked: one Sign in button, and the link
    // for a browser of the user's choosing.
    void aBrowserSignInWaitsForTheUser()
    {
        Ui::AuthCard card(Ui::AuthCard::Look::Card);
        card.show();
        card.showPrompt({ { QStringLiteral("flow"), QStringLiteral("oauthRedirect") },
            { QStringLiteral("url"), QStringLiteral("https://accounts.example/authorize") } });

        QVERIFY(visibleButton(&card, QStringLiteral("Sign in")) != nullptr);
        QVERIFY(visibleButton(&card, QStringLiteral("Start over")) == nullptr);
        QLabel* link = labelContaining(&card, QStringLiteral("https://accounts.example/authorize"));
        QVERIFY(link != nullptr);
        QVERIFY(link->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    }

    // A device code can expire, so that one keeps Start over.
    void aDeviceCodeCanBeStartedOver()
    {
        Ui::AuthCard card(Ui::AuthCard::Look::Card);
        card.show();
        QSignalSpy restart(&card, &Ui::AuthCard::restartRequested);
        card.showPrompt({ { QStringLiteral("flow"), QStringLiteral("deviceCode") },
            { QStringLiteral("url"), QStringLiteral("https://ya.example/device") },
            { QStringLiteral("code"), QStringLiteral("ABCD") } });
        QPushButton* startOver = visibleButton(&card, QStringLiteral("Start over"));
        QVERIFY(startOver != nullptr);
        startOver->click();
        QCOMPARE(restart.count(), 1);
    }
};

QObject* makeAuthCardTest() { return new AuthCardTest; }

} // namespace Tests

#include "AuthCardTest.moc"
