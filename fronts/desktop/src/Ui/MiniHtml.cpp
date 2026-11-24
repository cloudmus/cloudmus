#include "MiniHtml.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace Ui {

namespace {

// Escapes markup characters, but lets an entity the source wrote itself
// (&amp;, &lt;, &#8594; ...) through instead of double-escaping it.
QString escapeText(const QString& text)
{
    static const QRegularExpression entity(QStringLiteral(R"(&(?:[a-zA-Z]+|#\d+|#x[0-9a-fA-F]+);)"));
    QString out;
    int pos = 0;
    for (auto it = entity.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(pos, m.capturedStart() - pos).toHtmlEscaped();
        out += m.captured();
        pos = m.capturedEnd();
    }
    out += text.mid(pos).toHtmlEscaped();
    return out;
}

} // namespace

QString miniHtmlToRichText(const QString& text, const QColor& linkColor)
{
    static const QRegularExpression tag(
        QStringLiteral(R"(<(/?)(a|b|i|code|br)\b([^>]*)>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression href(
        QStringLiteral(R"re(\bhref\s*=\s*(?:"([^"]*)"|'([^']*)'))re"), QRegularExpression::CaseInsensitiveOption);

    QString out;
    QStringList open; // tags opened and not yet closed, innermost last
    int pos = 0;
    for (auto it = tag.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += escapeText(text.mid(pos, m.capturedStart() - pos));
        pos = m.capturedEnd();

        const bool closing = !m.captured(1).isEmpty();
        const QString name = m.captured(2).toLower();
        if (name == QLatin1String("br")) {
            out += QStringLiteral("<br>");
            continue;
        }
        if (closing) {
            // Only a tag that's open: unwinds anything opened inside it.
            const qsizetype at = open.lastIndexOf(name);
            if (at < 0)
                continue;
            while (open.size() > at)
                out += QStringLiteral("</%1>").arg(open.takeLast());
            continue;
        }
        if (name == QLatin1String("a")) {
            const QRegularExpressionMatch h = href.match(m.captured(3));
            const QUrl url(h.hasMatch() ? (h.captured(1).isEmpty() ? h.captured(2) : h.captured(1)) : QString());
            // Only somewhere a browser should go.
            if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
                continue;
            out += QStringLiteral("<a href=\"%1\" style=\"color: %2\">")
                       .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(), linkColor.name());
        } else {
            out += QStringLiteral("<%1>").arg(name);
        }
        open.append(name);
    }
    out += escapeText(text.mid(pos));
    while (!open.isEmpty())
        out += QStringLiteral("</%1>").arg(open.takeLast());
    return out;
}

} // namespace Ui
