#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace Ui::Settings {

// One group of settings — one entry in the Settings dialog's sidebar and
// one section of its scrolling column (see PageStack). Its widgets are
// built lazily, only once the section nears the viewport, so a page
// reads its current values in createWidget(), not in its constructor: a
// page never scrolled to has nothing to be dirty about and apply() leaves
// its settings alone.
//
// Actions that take effect right away (logging in or out of a source) are
// not part of apply() — they don't wait for Ok/Apply.
class Page : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Stable id for opening the dialog on this page (SettingsDialog's
    // openAt).
    virtual QString id() const = 0;
    virtual QString title() const = 0;
    // A Theme::icon() glyph name for the sidebar row; empty for none.
    virtual QString iconName() const { return { }; }
    // Or a monochrome SVG file (a backend manifest's icon), tinted the same
    // way; wins over iconName().
    virtual QString iconPath() const { return { }; }
    // Pages sharing a non-empty section are listed in the sidebar under a
    // heading of that name (e.g. "Sources"), after the pages without one.
    virtual QString sidebarSection() const { return { }; }
    // Stand-in height of the section until createWidget() runs — the
    // closer to the real one, the less the column's length (and the
    // scrollbar) changes as sections materialize.
    virtual int estimatedHeight() const { return 160; }

    // Called at most once; the returned widget is owned by `parent`.
    virtual QWidget* createWidget(QWidget* parent) = 0;

    virtual bool isDirty() const = 0;
    virtual void apply() = 0;

signals:
    void dirtyChanged();
};

} // namespace Ui::Settings
