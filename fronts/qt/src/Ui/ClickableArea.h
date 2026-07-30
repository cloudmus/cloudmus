#pragma once

#include <QMouseEvent>
#include <QWidget>

namespace Ui {

// A plain QWidget that emits clicked() on a left-button release inside its
// bounds — used to make a composite area (cover + title/artist in
// NowPlayingBar) clickable without needing a QPushButton's chrome.
class ClickableArea : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            emit clicked();
        }
        QWidget::mouseReleaseEvent(event);
    }
};

} // namespace Ui
