#pragma once

#include <QWidget>

namespace Ui {

// Shown in the content area in place of contentSplitter_/sourcePanel_
// until the user's first selection (a playlist, History, or a source) —
// see MainWindow's constructor and showPlaylistAsync()/showHistory()/
// showSourceStatusPanel(). Static: logo + app name + a hint to pick
// something, no interactivity, no generated-cover-art machinery (unlike
// HeroPanel — there's no playlist/track content here to color a
// background from).
class EmptyStatePlaceholder : public QWidget {
    Q_OBJECT

public:
    explicit EmptyStatePlaceholder(QWidget* parent = nullptr);
};

} // namespace Ui
