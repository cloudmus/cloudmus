#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

// Who makes CloudMus, for the About pages: the desktop's Ui::AboutDialog and
// the mobile front's AboutPage. Header-only, Qt Core alone: the mobile front's
// UI process has none of the core to link.
namespace About {

struct Person {
    QString name;
    QString email; // may be empty
    QString url; // a page of theirs (GitHub); may be empty
};

namespace Detail {
// resources/people.json (embedded as :/about/people.json): its list `key`, in
// its order.
inline QList<Person> people(QLatin1String key)
{
    QFile file(QStringLiteral(":/about/people.json"));
    if (!file.open(QIODevice::ReadOnly))
        return { };
    QList<Person> list;
    for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll()).object().value(key).toArray()) {
        const QJsonObject object = value.toObject();
        Person person { object.value(QLatin1String("name")).toString(), object.value(QLatin1String("email")).toString(),
            object.value(QLatin1String("url")).toString() };
        if (!person.name.isEmpty())
            list.append(person);
    }
    return list;
}
} // namespace Detail

// The app's authors.
inline QList<Person> authors() { return Detail::people(QLatin1String("authors")); }
// Those who helped: add a new one to resources/people.json.
inline QList<Person> contributors() { return Detail::people(QLatin1String("contributors")); }

// What a link to `url` reads as: the address without the scheme
// ("github.com/derfenix").
inline QString shownUrl(const QString& url)
{
    for (const char* scheme : { "https://", "http://" }) {
        if (url.startsWith(QLatin1String(scheme)))
            return url.mid(int(qstrlen(scheme)));
    }
    return url;
}

} // namespace About
