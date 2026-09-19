#include "Typography.h"

namespace Theme {

namespace {

struct StyleSpec {
    int pixelSize;
    QFont::Weight weight;
    bool letterSpacing = false; // label-upper only
};

const StyleSpec& specFor(TextStyle style)
{
    // clang-format off
    static const StyleSpec kDisplay       { 22, QFont::Bold };
    static const StyleSpec kTitle         { 14, QFont::DemiBold };
    static const StyleSpec kBody          { 13, QFont::Medium };
    static const StyleSpec kBodySecondary { 12, QFont::Normal };
    static const StyleSpec kCaption       { 11, QFont::Medium };
    static const StyleSpec kLabelUpper    { 11, QFont::Bold, /*letterSpacing=*/true };
    static const StyleSpec kButton        { 13, QFont::DemiBold };
    // clang-format on

    switch (style) {
        case TextStyle::Display:
            return kDisplay;
        case TextStyle::Title:
            return kTitle;
        case TextStyle::Body:
            return kBody;
        case TextStyle::BodySecondary:
            return kBodySecondary;
        case TextStyle::Caption:
            return kCaption;
        case TextStyle::LabelUpper:
            return kLabelUpper;
        case TextStyle::Button:
            return kButton;
    }
    return kBody;
}

} // namespace

QFont font(TextStyle style, const QFont& base)
{
    const StyleSpec& spec = specFor(style);
    QFont f = base;
    f.setFamily(QStringLiteral("Manrope"));
    f.setPixelSize(spec.pixelSize);
    f.setWeight(spec.weight);
    if (spec.letterSpacing) {
        // 0.06em, from the design system's label-upper token.
        f.setLetterSpacing(QFont::PercentageSpacing, 106.0);
    }
    return f;
}

QFont tabularFont(TextStyle style, const QFont& base)
{
    QFont f = font(style, base);
    // QFont::setFeature/QFont::Tag are Qt 6.7+ API — unconditional now that
    // the AppImage build's Qt floor is 6.9.3 (see packaging/appimage/).
    f.setFeature(QFont::Tag("tnum"), 1);
    return f;
}

} // namespace Theme
