#pragma once

#include <QJsonValue>
#include <QObject>
#include <QString>

#include "Coro.h"
#include "Models.h"
#include "RowsModel.h"

namespace App {
class Core;
}

namespace ViewModel {

class SourceSettings;

// One source's page of settings: whether it runs, how it connects, what its
// radio repeats, and the settings it describes itself, as rows of plain values
// for a form. A change applies at once and restarts the source if it has to.
class SourceSettingsScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceId READ sourceId NOTIFY sourceIdChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString description READ description NOTIFY descriptionChanged)
    // Why it isn't running, or that it's about to; empty when it is.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool usesNetwork READ usesNetwork NOTIFY usesNetworkChanged)
    Q_PROPERTY(QString connectionHint READ connectionHint NOTIFY connectionHintChanged)
    Q_PROPERTY(bool hasRadio READ hasRadio NOTIFY hasRadioChanged)
    Q_PROPERTY(int radioRepeatGap READ radioRepeatGap NOTIFY radioRepeatGapChanged)
    Q_PROPERTY(int radioRepeatDays READ radioRepeatDays NOTIFY radioRepeatDaysChanged)
    Q_PROPERTY(bool downloadsStreams READ downloadsStreams NOTIFY downloadsStreamsChanged)
    Q_PROPERTY(int downloadConnections READ downloadConnections NOTIFY downloadConnectionsChanged)
    // Its own settings: on their way, or why they didn't come.
    Q_PROPERTY(bool fieldsLoading READ fieldsLoading NOTIFY fieldsLoadingChanged)
    Q_PROPERTY(QString fieldsError READ fieldsError NOTIFY fieldsErrorChanged)

public:
    SourceSettingsScreen(App::Core& core, SourceSettings& sourceSettings, QObject* parent = nullptr);

    RowsModel& connectionRows() { return connectionRows_; }
    RowsModel& fieldRows() { return fieldRows_; }

    QString sourceId() const { return sourceId_; }
    QString name() const;
    QString description() const;
    QString status() const;
    bool enabled() const;
    bool usesNetwork() const;
    QString connectionHint() const;
    bool hasRadio() const;
    int radioRepeatGap() const;
    int radioRepeatDays() const;
    bool downloadsStreams() const;
    int downloadConnections() const;
    bool fieldsLoading() const { return fieldsLoading_; }
    QString fieldsError() const { return fieldsError_; }

public slots:
    // Starts it, if it was left unstarted: its settings are only known then.
    void open(const QString& sourceId);
    void setEnabled(bool enabled);
    void setConnection(const QString& connection);
    void setRadioRepeatGap(int tracks);
    void setRadioRepeatDays(int days);
    void setDownloadConnections(int connections);
    // One of its own settings, by its type: a QVariant doesn't get across to the
    // core. What the source says against it shows in the field's row.
    void setFieldText(const QString& key, const QString& value);
    void setFieldNumber(const QString& key, int value);
    void setFieldBool(const QString& key, bool value);

signals:
    void sourceIdChanged();
    void nameChanged();
    void descriptionChanged();
    void statusChanged();
    void enabledChanged();
    void usesNetworkChanged();
    void connectionHintChanged();
    void hasRadioChanged();
    void radioRepeatGapChanged();
    void radioRepeatDaysChanged();
    void downloadsStreamsChanged();
    void downloadConnectionsChanged();
    void fieldsLoadingChanged();
    void fieldsErrorChanged();

private:
    void setField(const QString& key, QJsonValue value);
    void refresh();
    void rebuildConnections();
    void rebuildFields();
    QString connection() const;
    Rpc::Task<void> loadFields(QString sourceId);
    Rpc::Task<void> updateField(QString sourceId, QString key, QJsonValue value);

    App::Core& core_;
    SourceSettings& sourceSettings_;
    RowsModel connectionRows_;
    RowsModel fieldRows_;
    QString sourceId_;
    // The process the fields came from: a new one is asked again.
    const void* fieldsClient_ = nullptr;
    SettingsDescription fields_;
    QHash<QString, QString> fieldErrors_;
    bool fieldsLoading_ = false;
    QString fieldsError_;
};

} // namespace ViewModel
