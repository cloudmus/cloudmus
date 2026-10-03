#pragma once

#include <QObject>
#include <QRectF>

class QPainter;
class QWidget;

namespace Theme {

// Keyboard focus is shown only while the user navigates by keyboard (like
// CSS :focus-visible): a click on a button must not leave a ring behind.
// One app-wide filter tracks that mode — Tab/Backtab/shortcut focus turns it
// on, any mouse press turns it off — and repaints the focused widget when
// it flips.
class FocusRing : public QObject {
    Q_OBJECT
public:
    explicit FocusRing(QObject* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;
};

// The mode flag, and whether `widget` is the one to draw a ring on right
// now. Windows of native dialogs (QFileDialog, ...) are out of scope, so
// they keep Fusion's own look.
bool keyboardFocusActive();
bool focusVisible(const QWidget* widget);

// The ring itself: an accent outline that hugs `shape`, drawn by every
// painter (the style's PE_FrameFocusRect, the slider, the row delegates)
// so they all look alike.
// `onAccent` is for a control filled with the accent color itself (primary
// and play buttons), where an accent ring would vanish into the fill.
void paintFocusRing(QPainter* painter, const QRectF& shape, qreal radius, bool onAccent = false);

} // namespace Theme
