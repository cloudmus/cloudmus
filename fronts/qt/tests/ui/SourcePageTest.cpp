#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include "AuthStates.h"
#include "BackendManifest.h"
#include "Settings.h"
#include "Settings/RestartRequests.h"
#include "Settings/SourcePage.h"
#include "SourceManager.h"
#include "TestSupport.h"
#include "ToastNotifier.h"

namespace Tests {

// A source's page in Settings, for a source that isn't running.
class SourcePageTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        settings_ = std::make_unique<Config::Settings>(dir_.filePath(QStringLiteral("config.ini")));
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        widget_ = std::make_unique<QWidget>();
        toasts_ = std::make_unique<Ui::ToastNotifier>(widget_.get());
        Rpc::BackendManifest manifest;
        manifest.id = QStringLiteral("fake");
        manifest.name = QStringLiteral("Fake");
        page_ = std::make_unique<Ui::Settings::SourcePage>(
            *settings_, *sourceManager_, authStates_, *toasts_, restarts_, manifest);
        // Says dirtyChanged() while still building itself, and is asked
        // isDirty() right then — as the Settings dialog does.
        connect(page_.get(), &Ui::Settings::Page::dirtyChanged, this, [this]() { page_->isDirty(); });
        page_->createWidget(widget_.get());
    }

    void cleanup()
    {
        page_.reset();
        toasts_.reset();
        widget_.reset();
        sourceManager_.reset();
        settings_.reset();
        QFile::remove(dir_.filePath(QStringLiteral("config.ini")));
    }

    void startsAsSaved() { QVERIFY(!page_->isDirty()); }

    void theRadioRepeatGapIsSavedOnApply()
    {
        auto* spin = widget_->findChild<QSpinBox*>();
        QVERIFY(spin != nullptr);
        QCOMPARE(spin->value(), Config::Settings::kDefaultRadioRepeatGap);
        QCOMPARE(spin->maximum(), Config::Settings::kMaxRadioRepeatGap);

        spin->setValue(250);
        QVERIFY(page_->isDirty());
        QVERIFY(await(page_->apply()));
        QCOMPARE(settings_->sourceRadioRepeatGap(QStringLiteral("fake")), 250);
        QVERIFY(!page_->isDirty());
    }

private:
    QTemporaryDir dir_;
    Rpc::AuthStates authStates_;
    Ui::Settings::RestartRequests restarts_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<QWidget> widget_;
    std::unique_ptr<Ui::ToastNotifier> toasts_;
    std::unique_ptr<Ui::Settings::SourcePage> page_;
};

QObject* makeSourcePageTest() { return new SourcePageTest; }

} // namespace Tests

#include "SourcePageTest.moc"
