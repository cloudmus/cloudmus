#pragma once

#include <QObject>
#include <QTimer>

class QWidget;

namespace Ui {

// Replaces Qt's native QToolTip entirely: QSS border-radius doesn't
// actually round QToolTip's corners (a real Qt limitation confirmed in
// practice — its native popup window doesn't get the transparent
// corner-masking QStyleSheetStyle applies to e.g. QMenu, so a QSS
// border-radius just draws a rounded shape inside a still-rectangular,
// opaque window). Same "native mechanism isn't flexible enough, paint it
// ourselves" pattern already used this session by SmoothScroller/
// OverlayScrollBar/ThemedSplitter.
//
// One global instance for the whole application, not attached to any
// specific widget — construct once (see main.cpp) after
// Theme::applyGlobalStyleSheet(); every widget with setToolTip() text
// gets the themed popup automatically via a global event filter.
class ThemedToolTip : public QObject {
    Q_OBJECT

public:
    explicit ThemedToolTip(QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showFor(QWidget* watched);
    void hidePopup();

    // Concrete popup type lives entirely in the .cpp, same as
    // OverlayScrollBar's Handle — an implementation detail, not part of
    // the public API.
    QWidget* popup_ = nullptr;
    QWidget* activeWidget_ = nullptr;
    QTimer safetyTimer_; // hides the popup even if a Leave event is ever missed
};

} // namespace Ui
