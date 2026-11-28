#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

class QFont;
class QPainter;

// Word-wrapped text capped at a number of lines, the last one elided with
// "…" when the text doesn't fit — for custom-painted titles (HeroPanel,
// TrackHoverCard) that must neither overflow their width nor grow without
// bound. Measuring (wrappedHeight) and drawing (drawWrapped) share one
// line-breaking pass, so what's laid out is exactly what's painted.
namespace Ui::TextLayout {

// The lines `text` breaks into at `width` — at most `maxLines`, the last
// elided if anything was left over. Empty for empty text.
QStringList wrapLines(const QFont& font, const QString& text, int width, int maxLines);

int wrappedHeight(const QFont& font, const QString& text, int width, int maxLines);

// Draws the wrapped lines from rect.top() down, each aligned
// horizontally within rect per `hAlign` (Qt::AlignLeft/HCenter/Right).
void drawWrapped(
    QPainter& painter, const QFont& font, const QRect& rect, Qt::Alignment hAlign, const QString& text, int maxLines);

} // namespace Ui::TextLayout
