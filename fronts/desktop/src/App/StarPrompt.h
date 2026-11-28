#pragma once

#include <QDate>
#include <QObject>

namespace Config {
class Settings;
}

namespace App {

class Analytics;

// When to ask, once, for a star on GitHub: after the player has been used
// on a few distinct days, so the ask comes from someone who kept it. A day
// counts when it's recorded at startup or as a track starts — the latter so
// a player left running in the tray across midnight counts the next day.
// Ui::StarPromptFlow shows the dialog and reports what came of it.
class StarPrompt : public QObject {
    Q_OBJECT

public:
    static constexpr int kDaysBeforeAsking = 3;

    StarPrompt(Config::Settings& settings, Analytics& analytics, QObject* parent = nullptr);

    // Counts `today` if it's a day not counted yet; emits due() when that
    // makes the prompt due.
    void recordUsage(QDate today = QDate::currentDate());
    bool isDue() const;
    // The dialog opened: it's done, however it's closed.
    void markShown();
    // How it was closed — to see in analytics how many go on to star.
    void markAnswered(bool starred);

signals:
    void due();

private:
    Config::Settings& settings_;
    Analytics& analytics_;
};

} // namespace App
