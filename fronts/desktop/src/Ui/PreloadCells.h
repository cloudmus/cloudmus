#pragma once

#include <QVector>
#include <QWidget>

namespace ViewModel {
class Preload;
}

namespace Ui {

class FrameTicker;

// The tracks downloaded ahead, right after the seek bar and drawn like a
// continuation of it: the bar shows how much of the playing track is
// there, each cell here one of the tracks after it, filling as it comes.
//
// It never grows past a few cells' width. More tracks than fit share that
// width as thinner cells, and past the point where a cell would be a
// hairline they become one bar filled by how much of them all is there.
// What to show — the tracks, their progress, the texts — is all
// ViewModel::Preload's; only the drawing is decided here.
class PreloadCells : public QWidget {
    Q_OBJECT

public:
    explicit PreloadCells(ViewModel::Preload& preload, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    // How the tracks are drawn at the moment — for tests.
    enum class Layout {
        Cells,
        Ticks,
        Bar,
    };
    Layout layout() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void reload();
    void onFrame();
    // Width of one cell and the gap between two, for the layout in use.
    qreal cellWidth() const;
    qreal cellGap() const;
    int cellAt(qreal x) const;
    void updateToolTip();

    ViewModel::Preload& preload_;
    // What's painted: follows the model at once, or eases toward it (see
    // ViewModel::Preload::ProgressMode).
    QVector<qreal> shown_;
    QVector<qreal> target_;
    qreal shownOverall_ = 0;
    FrameTicker* ticker_ = nullptr;
    int hovered_ = -1;
};

} // namespace Ui
