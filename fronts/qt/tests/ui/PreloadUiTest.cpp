#include <QMouseEvent>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include "PlaybackController.h"
#include "Preload.h"
#include "PreloadCells.h"
#include "Preloader.h"
#include "Settings.h"
#include "Settings/PreloadPage.h"
#include "SourceManager.h"
#include "TestSupport.h"

namespace Tests {

// The views of ViewModel::Preload: the cells after the seek bar and the
// Preloading settings page.
class PreloadUiTest : public QObject {
    Q_OBJECT

private:
    // The playing track and `ahead` more, none of them downloading: enough
    // for the cells to have something to lay out.
    void want(int ahead)
    {
        QVector<Playback::Preloader::Item> items;
        for (int i = 0; i <= ahead; ++i) {
            const QString name = QStringLiteral("t%1").arg(i);
            items.append({ QStringLiteral("src"), name, name, std::nullopt, false });
        }
        playback_->preloader().setWindow(items);
    }

private slots:
    void init()
    {
        settings_ = std::make_unique<Config::Settings>(dir_.filePath(QStringLiteral("config.ini")));
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        preload_ = std::make_unique<ViewModel::Preload>(*playback_, *settings_);
        // Told at once, not eased into: what's laid out is what's tested.
        preload_->setProgressMode(ViewModel::Preload::ProgressMode::Stepped);
        host_ = std::make_unique<QWidget>();
        host_->resize(600, 100);
        host_->show();
    }

    void cleanup()
    {
        host_.reset();
        preload_.reset();
        playback_.reset();
        sourceManager_.reset();
        settings_.reset();
        QFile::remove(dir_.filePath(QStringLiteral("config.ini")));
    }

    void theCellsTakeNoRoomWithNothingAhead()
    {
        Ui::PreloadCells cells(*preload_, host_.get());
        QVERIFY(cells.isHidden());
        want(3);
        QVERIFY(!cells.isHidden());
        want(0); // only the playing track
        QVERIFY(cells.isHidden());
    }

    void eachTrackHasACellWhileTheyFit()
    {
        Ui::PreloadCells cells(*preload_, host_.get());
        want(2);
        const int two = cells.sizeHint().width();
        want(4);
        QCOMPARE(cells.layout(), Ui::PreloadCells::Layout::Cells);
        QVERIFY(cells.sizeHint().width() > two);

        // Each cell answers for its own track; between them, for all.
        cells.resize(cells.sizeHint());
        QVERIFY(cells.toolTip().startsWith(QStringLiteral("Downloaded ahead: 0 of 4")));
        QMouseEvent overFirst(QEvent::MouseMove, QPointF(1, 1), cells.mapToGlobal(QPointF(1, 1)), Qt::NoButton,
            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&cells, &overFirst);
        QCOMPARE(cells.toolTip(), QStringLiteral("t1 — waiting"));
        const QPointF last(cells.width() - 2, 1);
        QMouseEvent overLast(
            QEvent::MouseMove, last, cells.mapToGlobal(last), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&cells, &overLast);
        QCOMPARE(cells.toolTip(), QStringLiteral("t4 — waiting"));
    }

    // However many tracks are wanted ahead, the bar keeps its seek slider.
    void manyTracksShareTheSameWidth()
    {
        Ui::PreloadCells cells(*preload_, host_.get());
        want(6);
        QCOMPARE(cells.layout(), Ui::PreloadCells::Layout::Cells);
        const int widest = cells.sizeHint().width();

        want(12);
        QCOMPARE(cells.layout(), Ui::PreloadCells::Layout::Ticks);
        QCOMPARE(cells.sizeHint().width(), widest);

        want(ViewModel::Preload::kMaxTracks);
        QCOMPARE(cells.layout(), Ui::PreloadCells::Layout::Bar);
        QCOMPARE(cells.sizeHint().width(), widest);
        QVERIFY(cells.toolTip().startsWith(QStringLiteral("Downloaded ahead: 0 of 50")));
        // Painting all three ways is at least survivable.
        cells.resize(cells.sizeHint());
        cells.grab();
    }

    void theSettingsPageShowsAndSavesWhatTheViewModelHas()
    {
        Ui::Settings::PreloadPage page(*preload_);
        QVERIFY(!page.isDirty()); // not built yet: nothing to be dirty about
        page.createWidget(host_.get());
        const QList<QSpinBox*> spins = host_->findChildren<QSpinBox*>();
        QCOMPARE(spins.size(), 3);
        QCOMPARE(spins.at(0)->value(), 3);
        QCOMPARE(spins.at(1)->value(), 2);
        QCOMPARE(spins.at(2)->value(), 1024);
        QCOMPARE(spins.at(0)->maximum(), ViewModel::Preload::kMaxTracks);
        QVERIFY(!page.isDirty());

        spins.at(0)->setValue(8);
        spins.at(1)->setValue(0);
        spins.at(2)->setValue(256);
        QVERIFY(page.isDirty());
        QCOMPARE(preload_->playlistTracks(), 3); // not before Apply
        QVERIFY(await(page.apply()));
        QVERIFY(!page.isDirty());
        QCOMPARE(preload_->playlistTracks(), 8);
        QCOMPARE(preload_->radioTracks(), 0);
        QCOMPARE(preload_->cacheMaxMb(), 256);
        QCOMPARE(settings_->preloadPlaylistTracks(), 8);
    }

private:
    QTemporaryDir dir_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<ViewModel::Preload> preload_;
    std::unique_ptr<QWidget> host_;
};

QObject* makePreloadUiTest() { return new PreloadUiTest; }

} // namespace Tests

#include "PreloadUiTest.moc"
