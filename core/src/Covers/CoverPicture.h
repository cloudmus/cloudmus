#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include "MeshAura.h"

namespace Covers {

// A local picture, turned as its EXIF data says and decoded no larger than
// needed for `side` (its shorter side still that long, if it was); the whole of
// it for 0. Null, with a warning in the log, if it can't be read.
QImage readScaled(const QString& path, int side);

// `source` at exactly `target` size without distorting it: a cover that isn't
// the target's shape is fitted inside (aspect kept, centered) over a blurred,
// slightly dimmed copy of itself scaled to fill the whole target — the usual
// "letterbox with its own blur" treatment, so a non-square cover drawn into a
// square slot neither stretches nor gets cropped. Covers already (nearly) the
// target's shape are just scaled. Null for a null `source` or an empty `target`.
//
// `blur`: as for meshAuraImage(). `renderScale` < 1 blurs the background
// smaller and scales it up, for a large image in a hurry.
QImage fitCover(const QImage& source, const QSize& target, const Blur& blur, qreal renderScale = 1.0);

} // namespace Covers
