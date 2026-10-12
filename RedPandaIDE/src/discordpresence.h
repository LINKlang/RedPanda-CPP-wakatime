#ifndef DISCORDPRESENCE_H
#define DISCORDPRESENCE_H

#include "editoractivity.h"
#include <QJsonObject>

struct DiscordPresenceOptions {
    bool enabled {false};
    bool showCursor {true};
    bool privacyMode {false};
    bool showWebsite {true};
    int idleTimeoutMinutes {5};
    QString customApplicationId;
    QString applicationId() const;
};

QString discordPresenceText(const QString &text);
bool discordPresenceIdle(qint64 inactiveMs, int timeoutMinutes);
QJsonObject makeDiscordPresence(const EditorActivitySnapshot &activity,
                               const DiscordPresenceOptions &options,
                               bool idle, qint64 sessionStartedAtSeconds);

#endif
