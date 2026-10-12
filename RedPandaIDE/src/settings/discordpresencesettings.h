#ifndef DISCORDPRESENCESETTINGS_H
#define DISCORDPRESENCESETTINGS_H

#include "basesettings.h"
#include "../discordpresence.h"

class DiscordPresenceSettings: public BaseSettings {
public:
    explicit DiscordPresenceSettings(SettingsPersistor *persistor);
    DiscordPresenceOptions &options() { return mOptions; }
    const DiscordPresenceOptions &options() const { return mOptions; }
protected:
    void doSave() override;
    void doLoad() override;
private:
    DiscordPresenceOptions mOptions;
};
#endif
