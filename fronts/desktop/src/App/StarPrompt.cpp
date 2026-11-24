#include "StarPrompt.h"

#include "Analytics.h"
#include "Settings.h"

namespace App {

StarPrompt::StarPrompt(Config::Settings& settings, Analytics& analytics, QObject* parent)
    : QObject(parent)
    , settings_(settings)
    , analytics_(analytics)
{
}

void StarPrompt::recordUsage(QDate today)
{
    if (settings_.starPromptDone() || today == settings_.starPromptLastUsageDay())
        return;
    settings_.setStarPromptUsage(settings_.starPromptUsageDays() + 1, today);
    if (isDue())
        emit due();
}

bool StarPrompt::isDue() const
{
    return !settings_.starPromptDone() && settings_.starPromptUsageDays() >= kDaysBeforeAsking;
}

void StarPrompt::markShown()
{
    settings_.setStarPromptDone();
    analytics_.recordStarPrompt(Analytics::StarPromptAction::Shown);
}

void StarPrompt::markAnswered(bool starred)
{
    analytics_.recordStarPrompt(starred ? Analytics::StarPromptAction::Star : Analytics::StarPromptAction::Dismiss);
}

} // namespace App
