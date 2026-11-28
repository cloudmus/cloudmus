// Draws the macOS app icon's .iconset from the logo, for iconutil to pack
// into CloudMus.app's AppIcon.icns (CMakeLists.txt runs it at build time):
//
//   cloudmus-make-iconset <logo.svg> <out.iconset>
//
// The bundle's icon is what Finder, notifications and Launchpad show; the
// Dock's is drawn at runtime on the same grid (Integration::DockIcon).

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

int main(int argc, char** argv)
{
    // No window is shown: no need for a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QTextStream err(stderr);
    if (args.size() != 2) {
        err << "usage: cloudmus-make-iconset <logo.svg> <out.iconset>\n";
        return 2;
    }
    QSvgRenderer logo(args[0]);
    if (!logo.isValid()) {
        err << "can't read " << args[0] << "\n";
        return 1;
    }
    QDir out(args[1]);
    if (!out.mkpath(QStringLiteral("."))) {
        err << "can't create " << args[1] << "\n";
        return 1;
    }
    // Apple's grid keeps 100px of the 1024 free around the artwork.
    for (int points : { 16, 32, 128, 256, 512 }) {
        for (int scale : { 1, 2 }) {
            const int side = points * scale;
            QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            const qreal margin = side * 100.0 / 1024.0;
            logo.render(&painter, QRectF(margin, margin, side - 2 * margin, side - 2 * margin));
            painter.end();
            const QString name = scale == 1 ? QStringLiteral("icon_%1x%1.png").arg(points)
                                            : QStringLiteral("icon_%1x%1@2x.png").arg(points);
            if (!image.save(out.filePath(name))) {
                err << "can't write " << out.filePath(name) << "\n";
                return 1;
            }
        }
    }
    return 0;
}
