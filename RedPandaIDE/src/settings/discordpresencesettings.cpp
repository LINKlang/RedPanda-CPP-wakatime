#include "discordpresencesettings.h"

DiscordPresenceSettings::DiscordPresenceSettings(SettingsPersistor *persistor):
    BaseSettings(persistor, QStringLiteral("DiscordRichPresence"))
{
}

void DiscordPresenceSettings::doLoad()
{
    mOptions.enabled = boolValue("enabled", false);
    mOptions.showCursor = boolValue("show_cursor", true);
    mOptions.privacyMode = boolValue("privacy_mode", false);
    mOptions.showWebsite = boolValue("show_website", true);
    mOptions.idleTimeoutMinutes = qBound(1, intValue("idle_timeout_minutes", 5), 60);
    mOptions.customApplicationId = stringValue("application_id", "").trimmed();
}

void DiscordPresenceSettings::doSave()
{
    saveValue("enabled", mOptions.enabled);
    saveValue("show_cursor", mOptions.showCursor);
    saveValue("privacy_mode", mOptions.privacyMode);
    saveValue("show_website", mOptions.showWebsite);
    saveValue("idle_timeout_minutes", qBound(1, mOptions.idleTimeoutMinutes, 60));
    saveValue("application_id", mOptions.customApplicationId.trimmed());
}
