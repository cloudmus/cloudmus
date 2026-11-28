#pragma once

#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QWidget>

#include <functional>

#include "Models.h"

class QLabel;

namespace Ui::Settings {

// A source's own settings as a form, built from nothing but its
// settings.describe answer (docs/protocol.md §7.7) — one control per field
// by type, grouped under their group's heading, each with its description
// and room for an error below. Knows which values the user changed since
// the last describe or markApplied().
class SettingsForm : public QWidget {
    Q_OBJECT

public:
    explicit SettingsForm(const SettingsDescription& description, QWidget* parent = nullptr);

    // key -> new value, for settings.update: only what differs from what
    // the source reported, and a secret only once something was typed.
    QMap<QString, QJsonValue> changedValues() const;
    bool isDirty() const { return !changedValues().isEmpty(); }
    // Whether any of `keys` is marked restartRequired.
    bool needsRestart(const QList<QString>& keys) const;

    // The source took `values`: they're the new baseline.
    void markApplied(const QMap<QString, QJsonValue>& values);
    // The source refused `key`; empty `key` clears every error.
    void showError(const QString& key, const QString& message);

signals:
    void changed();

private:
    struct Row {
        SettingField field;
        std::function<QJsonValue()> read;
        // Puts a value back (after markApplied(), for a secret: clears it).
        std::function<void(const QJsonValue&)> write;
        QJsonValue baseline;
        QLabel* error = nullptr;
    };

    QWidget* makeEditor(Row& row, QWidget* parent);

    QList<Row> rows_;
};

} // namespace Ui::Settings
