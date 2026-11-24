#include "CoverPlaceholder.h"

#include <QFont>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QRect>

#include "Icons.h"
#include "Radius.h"
#include "Tokens.h"
#include "Typography.h"

namespace Theme {

namespace {

struct Tone {
    QColor Palette::* background;
    QColor Palette::* glyph;
};

// Fixed rotation, not random: the same track must always render the same
// tone across every repaint (scrolling a list back and forth must not
// flicker) and across app restarts. qHash(..., /*seed=*/0) is used instead
// of the bare 1-arg qHash(QString) overload, which Qt seeds randomly per
// process (QHashSeed) — this hash needs to stay stable, not just consistent
// within one run.
int toneIndex(const QString& stableId) { return static_cast<int>(qHash(stableId, 0) % 3); }

const Tone& toneFor(int index)
{
    static const Tone kTones[3] = {
        { &Palette::accent, &Palette::onAccent },
        { &Palette::inkSecondary, &Palette::surface100 },
        { &Palette::surface400, &Palette::ink },
    };
    return kTones[index];
}

} // namespace

void paintCoverPlaceholder(QPainter* painter, const QRect& rect, const QString& stableId, const QString& genreTag)
{
    const Palette& pal = palette();

    painter->save();
    QPainterPath clip;
    clip.addRoundedRect(rect, Radius::md, Radius::md);
    painter->setClipPath(clip);

    if (!genreTag.isEmpty()) {
        painter->fillRect(rect, pal.surface300);
        QFont captionFont = font(TextStyle::LabelUpper);
        captionFont.setPixelSize(10);
        painter->setFont(captionFont);
        painter->setPen(pal.inkSecondary);
        painter->drawText(rect.adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, genreTag.toUpper());
    } else {
        const Tone& tone = toneFor(toneIndex(stableId));
        painter->fillRect(rect, pal.*tone.background);

        const int side = qMax(1, qRound(qMin(rect.width(), rect.height()) * 0.5));
        const QRect glyphRect(rect.center().x() - side / 2, rect.center().y() - side / 2, side, side);
        const QIcon glyph = iconWithColor(QStringLiteral("music_note"), pal.*tone.glyph, side);
        glyph.paint(painter, glyphRect);
    }

    painter->restore();
}

} // namespace Theme
