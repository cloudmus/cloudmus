#pragma once

#include <QDialog>
#include <QVariantAnimation>

class QLabel;

namespace ViewModel {
class AudioPulse;
class LogoSound;
}

namespace Ui {

class CloudsBackdrop;
class StackTransition;

// Replaces the native QMessageBox::about(...) this used to be — a
// QMessageBox is a stock Qt dialog with no styling hook the app's QSS can
// reach (same reason ThemedToolTip replaces QToolTip and
// Theme::CloudMusStyle hand-paints QMenu: some native Qt popups just
// aren't stylable in place).
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    // `pulse`: the music the clouds and the logo move with; `sound`: the
    // logo's sound, played by a click on it, which they move with too.
    AboutDialog(ViewModel::AudioPulse& pulse, ViewModel::LogoSound& sound, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // Keeps the minimum height layout-driven but correct at the CURRENT
    // width: Qt's minimum for a top-level window ignores height-for-width,
    // counting the word-wrapped description as one line, so the window
    // could be squeezed until margins and the header collapsed.
    void updateMinimumHeight();
    // Takes the window's height to `height` over a moment, not at once: a
    // tab's page may be taller or shorter than the last one. The minimum height
    // is out of the way meanwhile (it's for the page the window is heading
    // for), and is set again at the end.
    void animateHeightTo(int height);

    QLabel* description_ = nullptr;
    CloudsBackdrop* backdrop_ = nullptr;
    StackTransition* transition_ = nullptr;
    QVariantAnimation heightAnimation_;
};

} // namespace Ui
