#pragma once

#include <QObject>
#include <QString>

#include <exception>

namespace ViewModel {

// What the app has to tell the user in passing — an error, a finished
// download, a like — posted from anywhere in the core (services, view
// models) and shown by whichever view is up to show it (the main window's
// toasts, or the Settings dialog's while that's open). Posting doesn't
// depend on any window existing.
class Messages : public QObject {
    Q_OBJECT

public:
    enum class Kind {
        Info, // plain news, neither good nor bad
        Success, // something the user asked for is done
        Error, // something went wrong
    };
    Q_ENUM(Kind)

    explicit Messages(QObject* parent = nullptr);

    // `text` is what the user reads; `details` (optional) is the technical
    // line behind it, which a view shows only when asked (the toast's
    // chevron) — a connection pool's exception is no headline.
    void info(const QString& text, const QString& details = QString()) { emit posted(Kind::Info, text, details); }
    void success(const QString& text, const QString& details = QString()) { emit posted(Kind::Success, text, details); }
    void error(const QString& text, const QString& details = QString()) { emit posted(Kind::Error, text, details); }
    // No network: an error to a view with nothing else to say it, not to one
    // that shows App::Connectivity's state instead (setOfflineShownElsewhere()).
    void offline(const QString& text, const QString& details = QString())
    {
        if (!offlineShownElsewhere_)
            error(text, details);
    }
    // A failed call of a source, told for a person: with no network, "<source>:
    // no network connection" and the exception's own text as the details;
    // any other failure as the source worded it, a long or multi-line one cut
    // to its first line with the whole as the details. `quietOffline`: the
    // no-network kind goes through offline().
    void failure(const QString& sourceName, const std::exception& e, bool quietOffline = false);
    // Same, for text that is already at hand (a notification's message).
    void failure(const QString& sourceName, const QString& message, bool isOffline, bool quietOffline = false);
    void setOfflineShownElsewhere(bool shown) { offlineShownElsewhere_ = shown; }

signals:
    void posted(ViewModel::Messages::Kind kind, const QString& text, const QString& details);

private:
    bool offlineShownElsewhere_ = false;
};

} // namespace ViewModel
