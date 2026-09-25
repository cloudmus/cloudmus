#include "Settings/SettingsForm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <limits>

#include "PasswordReveal.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui::Settings {

namespace {

QLabel* makeLabel(const QString& text, const char* property, Theme::TextStyle style, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty(property, true);
    label->setFont(Theme::font(style));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText); // source-supplied text, not markup
    return label;
}

} // namespace

SettingsForm::SettingsForm(const SettingsDescription& description, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space2);

    // The untitled group (fields without one) first, then the titled
    // ones in the source's order.
    QList<std::optional<SettingsGroup>> groups { std::nullopt };
    for (const SettingsGroup& group : description.groups)
        groups.append(group);

    for (const std::optional<SettingsGroup>& group : groups) {
        const QString groupId = group ? group->id : QString();
        QList<SettingField> fields;
        for (const SettingField& field : description.fields) {
            if (field.group.value_or(QString()) == groupId)
                fields.append(field);
        }
        if (fields.isEmpty())
            continue;

        if (group) {
            auto* title = makeLabel(group->title.toUpper(), "hint", Theme::TextStyle::LabelUpper, this);
            title->setContentsMargins(0, Theme::Spacing::space3, 0, 0);
            layout->addWidget(title);
            if (group->description)
                layout->addWidget(makeLabel(*group->description, "hint", Theme::TextStyle::BodySecondary, this));
        }

        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(Theme::Spacing::space3);
        form->setVerticalSpacing(Theme::Spacing::space3);
        for (const SettingField& field : fields) {
            Row row;
            row.field = field;
            // Under the control: its description, then (hidden until
            // showError()) what the source didn't like about it.
            auto* cell = new QWidget(this);
            auto* cellLayout = new QVBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(Theme::Spacing::space1);
            cellLayout->addWidget(makeEditor(row, cell));
            if (field.description)
                cellLayout->addWidget(makeLabel(*field.description, "hint", Theme::TextStyle::BodySecondary, cell));
            row.error = makeLabel(QString(), "error", Theme::TextStyle::BodySecondary, cell);
            row.error->hide();
            cellLayout->addWidget(row.error);

            // A checkbox carries its own label; everything else gets one
            // in the form's label column.
            if (field.type == QStringLiteral("boolean"))
                form->addRow(cell);
            else
                form->addRow(field.label + QLatin1Char(':'), cell);
            row.baseline = row.read();
            rows_.append(row);
        }
        layout->addLayout(form);
    }
}

QWidget* SettingsForm::makeEditor(Row& row, QWidget* parent)
{
    const SettingField& field = row.field;
    // What the source reported, or its default if it reported nothing usable.
    const QJsonValue initial = field.value.isUndefined() || field.value.isNull() ? field.default_ : field.value;
    const QString type = field.type;

    if (type == QStringLiteral("boolean")) {
        auto* check = new QCheckBox(field.label, parent);
        check->setFont(Theme::font(Theme::TextStyle::Body));
        check->setChecked(initial.toBool());
        connect(check, &QCheckBox::toggled, this, &SettingsForm::changed);
        row.read = [check]() { return QJsonValue(check->isChecked()); };
        row.write = [check](const QJsonValue& v) { check->setChecked(v.toBool()); };
        return check;
    }

    if (type == QStringLiteral("integer")) {
        auto* spin = new QSpinBox(parent);
        spin->setFont(Theme::font(Theme::TextStyle::Body));
        spin->setRange(
            field.min.value_or(std::numeric_limits<int>::min()), field.max.value_or(std::numeric_limits<int>::max()));
        spin->setValue(initial.toInt());
        connect(spin, &QSpinBox::valueChanged, this, &SettingsForm::changed);
        row.read = [spin]() { return QJsonValue(spin->value()); };
        row.write = [spin](const QJsonValue& v) { spin->setValue(v.toInt()); };
        return spin;
    }

    if (type == QStringLiteral("enum")) {
        auto* combo = new QComboBox(parent);
        combo->setFont(Theme::font(Theme::TextStyle::Body));
        // See DownloadsPage: only a QStyledItemDelegate honors the ::item QSS.
        combo->setItemDelegate(new QStyledItemDelegate(combo));
        for (const SettingOption& option : field.options.value_or(QList<SettingOption>()))
            combo->addItem(option.label, option.value);
        combo->setCurrentIndex(qMax(0, combo->findData(initial.toString())));
        connect(combo, &QComboBox::currentIndexChanged, this, &SettingsForm::changed);
        row.read = [combo]() { return QJsonValue(combo->currentData().toString()); };
        row.write = [combo](const QJsonValue& v) { combo->setCurrentIndex(qMax(0, combo->findData(v.toString()))); };
        return combo;
    }

    auto* edit = new QLineEdit(parent);
    edit->setFont(Theme::font(Theme::TextStyle::Body));
    edit->setPlaceholderText(field.placeholder.value_or(QString()));
    connect(edit, &QLineEdit::textChanged, this, &SettingsForm::changed);

    if (type == QStringLiteral("secret")) {
        // Never shown, never sent back unless replaced: the source keeps it.
        addPasswordReveal(edit);
        if (field.isSet.value_or(false))
            edit->setPlaceholderText(tr("Saved — type to replace"));
        row.read = [edit]() { return QJsonValue(edit->text()); };
        row.write = [edit](const QJsonValue&) {
            edit->clear();
            edit->setPlaceholderText(tr("Saved — type to replace"));
        };
        return edit;
    }

    edit->setText(initial.toString());
    row.read = [edit]() { return QJsonValue(edit->text().trimmed()); };
    row.write = [edit](const QJsonValue& v) { edit->setText(v.toString()); };
    if (type != QStringLiteral("path"))
        return edit;

    auto* browse = new QPushButton(tr("Browse…"), parent);
    browse->setProperty("variant", "secondary");
    browse->setFont(Theme::font(Theme::TextStyle::Button));
    const bool pickFile = field.pathKind.value_or(QString()) == QStringLiteral("file");
    const QString title = field.label;
    connect(browse, &QPushButton::clicked, parent, [edit, pickFile, title]() {
        // Constructed by hand rather than QFileDialog::getExistingDirectory(),
        // to keep it on the system font — see DownloadsPage.
        QFileDialog dialog(edit, title, edit->text());
        dialog.setFileMode(pickFile ? QFileDialog::ExistingFile : QFileDialog::Directory);
        if (!pickFile)
            dialog.setOption(QFileDialog::ShowDirsOnly);
        Theme::useSystemFont(&dialog);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
            edit->setText(dialog.selectedFiles().constFirst());
    });
    auto* container = new QWidget(parent);
    auto* rowLayout = new QHBoxLayout(container);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(Theme::Spacing::space2);
    rowLayout->addWidget(edit, 1);
    rowLayout->addWidget(browse);
    return container;
}

QMap<QString, QJsonValue> SettingsForm::changedValues() const
{
    QMap<QString, QJsonValue> values;
    for (const Row& row : rows_) {
        const QJsonValue value = row.read();
        const bool changed
            = row.field.type == QStringLiteral("secret") ? !value.toString().isEmpty() : value != row.baseline;
        if (changed)
            values.insert(row.field.key, value);
    }
    return values;
}

bool SettingsForm::needsRestart(const QList<QString>& keys) const
{
    for (const Row& row : rows_) {
        if (keys.contains(row.field.key) && row.field.restartRequired.value_or(false))
            return true;
    }
    return false;
}

void SettingsForm::markApplied(const QMap<QString, QJsonValue>& values)
{
    for (Row& row : rows_) {
        if (!values.contains(row.field.key))
            continue;
        row.baseline = values.value(row.field.key);
        if (row.field.type == QStringLiteral("secret"))
            row.write(row.baseline);
    }
    emit changed();
}

void SettingsForm::showError(const QString& key, const QString& message)
{
    for (Row& row : rows_) {
        const bool here = !key.isEmpty() && row.field.key == key;
        row.error->setText(here ? message : QString());
        row.error->setVisible(here);
    }
}

} // namespace Ui::Settings
