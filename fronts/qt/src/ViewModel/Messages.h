#pragma once

#include <QObject>
#include <QString>

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

    void info(const QString& text) { emit posted(Kind::Info, text); }
    void success(const QString& text) { emit posted(Kind::Success, text); }
    void error(const QString& text) { emit posted(Kind::Error, text); }

signals:
    void posted(ViewModel::Messages::Kind kind, const QString& text);
};

} // namespace ViewModel
