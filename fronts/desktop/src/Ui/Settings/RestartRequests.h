#pragma once

#include <QSet>
#include <QString>

namespace Ui::Settings {

// Sources to restart once one Apply is through with every page — so a
// source whose proxy's details and whose choice of proxy both changed in
// the same Apply restarts once, with both in effect, not twice. See
// SettingsDialog::applyAllAsync().
struct RestartRequests {
    QSet<QString> sourceIds;
};

} // namespace Ui::Settings
