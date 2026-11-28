#pragma once

#include <QString>
#include <QToolButton>

#include <functional>

class QTimer;

namespace Ui {

// The app's one way of putting something on the clipboard from a button: a
// small round one with the copy glyph beside what it copies, a check mark for a
// moment once pressed. The caller names what it copies in the tooltip.
class CopyButton : public QToolButton {
    Q_OBJECT

public:
    explicit CopyButton(QWidget* parent = nullptr);

    // Asked at each click, so it copies what's shown then.
    void setSource(std::function<QString()> source);
    bool isCopied() const;

protected:
    void hideEvent(QHideEvent* event) override;

private:
    // The check mark while true; also the "copied" property, for the tests.
    void setCopied(bool copied);

    std::function<QString()> source_;
    QTimer* revert_;
};

} // namespace Ui
