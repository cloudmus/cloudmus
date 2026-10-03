#include "KeyActivation.h"

#include <QAbstractItemView>
#include <QKeyEvent>

namespace Ui {

namespace {

class KeyActivation : public QObject {
public:
    KeyActivation(QAbstractItemView* view, std::function<void(const QModelIndex&, Qt::Key)> activate)
        : QObject(view)
        , view_(view)
        , activate_(std::move(activate))
    {
        view->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != view_ || event->type() != QEvent::KeyPress)
            return false;
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter && key->key() != Qt::Key_Space)
            return false;
        if (key->modifiers() != Qt::NoModifier && key->modifiers() != Qt::KeypadModifier)
            return false;
        const QModelIndex index = view_->currentIndex();
        if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled))
            return false;
        // Held down, the key must not replay the track over and over.
        if (!key->isAutoRepeat())
            activate_(index, key->key() == Qt::Key_Space ? Qt::Key_Space : Qt::Key_Return);
        return true;
    }

private:
    QAbstractItemView* view_;
    std::function<void(const QModelIndex&, Qt::Key)> activate_;
};

} // namespace

void activateCurrentOnKey(QAbstractItemView* view, std::function<void(const QModelIndex&, Qt::Key)> activate)
{
    new KeyActivation(view, std::move(activate));
}

} // namespace Ui
