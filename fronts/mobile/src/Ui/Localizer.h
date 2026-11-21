#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include "Translator.h"

namespace Ui {

class Backend;

// The language of the views: the translator of this process (the core's is
// its own, in the playback service on Android), the choice offered to QML
// and the choice kept in step with the core's, which is where it is saved.
// The last choice is also kept here, so the first frame is already in it
// while a remote core is still coming up.
class Localizer : public QObject {
    Q_OBJECT
    // What the settings list shows: { code, name }, English and the rest in
    // their own names. No "Auto": that is the empty setting.
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    // A code, or empty: follow the system.
    Q_PROPERTY(QString setting READ setting NOTIFY settingChanged)

public:
    Localizer(Backend& backend, QQmlEngine& engine, QObject* parent = nullptr);

    QVariantList languages() const;
    QString setting() const { return setting_; }

public slots:
    void setSetting(const QString& code);

signals:
    void settingChanged();

private slots:
    void syncFromCore();

private:
    void adopt(const QString& code);
    void connectCore();

    Backend& backend_;
    QQmlEngine& engine_;
    I18n::Translator translator_;
    QString setting_;
    QPointer<QObject> screen_;
};

} // namespace Ui
