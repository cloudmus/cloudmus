#include "TextLayout.h"

#include <QFontMetrics>
#include <QPainter>
#include <QTextLayout>

namespace Ui::TextLayout {

QStringList wrapLines(const QFont& font, const QString& text, int width, int maxLines)
{
    QStringList lines;
    if (text.isEmpty() || width <= 0 || maxLines <= 0)
        return lines;

    QTextLayout layout(text, font);
    QTextOption option;
    // Word boundaries first, anywhere only for a single word wider than
    // the line (a long unbroken title).
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    QList<QPair<int, int>> ranges; // (start, length) per line
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(width);
        ranges.append({ line.textStart(), line.textLength() });
    }
    layout.endLayout();

    const QFontMetrics metrics(font);
    for (int i = 0; i < ranges.size() && i < maxLines; ++i) {
        const bool lastAllowed = i == maxLines - 1;
        if (lastAllowed && ranges.size() > maxLines) {
            // Everything that didn't fit goes into this last line, elided.
            lines.append(metrics.elidedText(text.mid(ranges[i].first).simplified(), Qt::ElideRight, width));
        } else {
            lines.append(text.mid(ranges[i].first, ranges[i].second).trimmed());
        }
    }
    return lines;
}

int wrappedHeight(const QFont& font, const QString& text, int width, int maxLines)
{
    const int count = int(wrapLines(font, text, width, maxLines).size());
    return count == 0 ? 0 : count * QFontMetrics(font).lineSpacing();
}

void drawWrapped(
    QPainter& painter, const QFont& font, const QRect& rect, Qt::Alignment hAlign, const QString& text, int maxLines)
{
    const QFontMetrics metrics(font);
    painter.setFont(font);
    int y = rect.top();
    for (const QString& line : wrapLines(font, text, rect.width(), maxLines)) {
        painter.drawText(QRect(rect.left(), y, rect.width(), metrics.lineSpacing()), hAlign | Qt::AlignVCenter, line);
        y += metrics.lineSpacing();
    }
}

} // namespace Ui::TextLayout
