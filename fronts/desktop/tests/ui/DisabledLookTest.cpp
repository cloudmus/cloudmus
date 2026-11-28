#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "Icons.h"
#include "StyleSheet.h"
#include "Tokens.h"

namespace Tests {

// A control that can't be used has to look it. The stylesheet gives most
// controls explicit colors, which the palette's disabled ones then don't
// reach — each needs a :disabled rule of its own.
class DisabledLookTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        qApp->setStyleSheet(QString());
        Theme::setModeOverride(std::nullopt);
    }

    void aDisabledControlLooksDifferent_data()
    {
        using Make = std::function<QWidget*(QWidget*)>;
        QTest::addColumn<Make>("make");
        const auto add = [](const char* name, Make make) {
            for (const Theme::Mode mode : { Theme::Mode::Light, Theme::Mode::Dark }) {
                const QByteArray tag = QByteArray(name) + (mode == Theme::Mode::Dark ? " (dark)" : " (light)");
                QTest::newRow(tag.constData()) << make;
            }
        };
        add("check box", [](QWidget* parent) { return new QCheckBox(QStringLiteral("Option"), parent); });
        add("checked check box", [](QWidget* parent) {
            auto* box = new QCheckBox(parent); // no text: the box alone has to show it
            box->setChecked(true);
            return box;
        });
        add("unchecked check box", [](QWidget* parent) { return new QCheckBox(parent); });
        add("primary button", [](QWidget* parent) {
            auto* button = new QPushButton(parent); // no text: the fill alone
            button->setProperty("variant", "primary");
            return button;
        });
        add("secondary button", [](QWidget* parent) {
            auto* button = new QPushButton(QStringLiteral("Browse"), parent);
            button->setProperty("variant", "secondary");
            return button;
        });
        add("icon button", [](QWidget* parent) {
            auto* button = new QToolButton(parent);
            button->setProperty("variant", "icon");
            button->setIcon(Theme::icon(QStringLiteral("refresh"), Theme::IconColor::InkSecondary, 14));
            return button;
        });
        add("line edit", [](QWidget* parent) { return new QLineEdit(QStringLiteral("Text"), parent); });
        add("combo box", [](QWidget* parent) {
            auto* combo = new QComboBox(parent);
            combo->addItem(QStringLiteral("Item"));
            return combo;
        });
        add("spin box", [](QWidget* parent) {
            auto* spin = new QSpinBox(parent);
            spin->setValue(5);
            return spin;
        });
        add("hint", [](QWidget* parent) {
            auto* label = new QLabel(QStringLiteral("Hint"), parent);
            label->setProperty("hint", true);
            return label;
        });
    }

    void aDisabledControlLooksDifferent()
    {
        QFETCH(std::function<QWidget*(QWidget*)>, make);
        const bool dark = QByteArray(QTest::currentDataTag()).endsWith("(dark)");
        const Theme::Mode mode = dark ? Theme::Mode::Dark : Theme::Mode::Light;
        Theme::setModeOverride(mode);
        qApp->setStyleSheet(Theme::buildStyleSheet(mode));

        QDialog dialog;
        dialog.setProperty("themed", true);
        auto* layout = new QVBoxLayout(&dialog);
        QWidget* control = make(&dialog);
        layout->addWidget(control);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        // Alone in the dialog it has the focus, and losing that with being
        // disabled would be a difference too.
        control->setFocusPolicy(Qt::NoFocus);
        control->clearFocus();
        QCoreApplication::processEvents();
        const QImage enabled = control->grab().toImage();
        control->setEnabled(false);
        QCoreApplication::processEvents();
        QVERIFY(control->grab().toImage() != enabled);
    }

    void aDisabledMenuItemLooksDifferent()
    {
        qApp->setStyleSheet(Theme::buildStyleSheet(Theme::Mode::Dark));
        const auto grabMenu = [](bool enabled) {
            QMenu menu;
            menu.addAction(QStringLiteral("Loading"))->setEnabled(enabled);
            menu.ensurePolished();
            menu.resize(menu.sizeHint());
            return menu.grab().toImage();
        };
        QVERIFY(grabMenu(false) != grabMenu(true));
    }
};

QObject* makeDisabledLookTest() { return new DisabledLookTest; }

} // namespace Tests

#include "DisabledLookTest.moc"
