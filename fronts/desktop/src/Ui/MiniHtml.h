#pragma once

#include <QColor>
#include <QString>

namespace Ui {

// Text a source supplies with a minimal HTML subset (docs/protocol.md §10:
// <a href="http(s)://...">, <b>, <i>, <code>, <br>) as rich text for a
// QLabel: those tags kept — links in `linkColor`, as the palette's stock
// blue would be foreign to the theme — and everything else, any other tag
// included, escaped to show as the text it is. Unclosed tags are closed.
QString miniHtmlToRichText(const QString& text, const QColor& linkColor);

} // namespace Ui
