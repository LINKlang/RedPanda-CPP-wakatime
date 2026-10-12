#ifndef DISCORDPRESENCEMANAGER_H
#define DISCORDPRESENCEMANAGER_H

#include "discordpresence.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class MainWindow;
class EditorActivityCollector;
class DiscordRpcClient;

class DiscordPresenceManager: public QObject {
    Q_OBJECT
public:
    DiscordPresenceManager(MainWindow *mainWindow, EditorActivityCollector *collector);
    void reloadSettings();
    void shutdown();
    DiscordRpcClient *client() const { return mClient; }
private:
    void refresh();
    void scheduleRefresh();
    void scheduleIdle();
    MainWindow *mMainWindow;
    EditorActivityCollector *mCollector;
    DiscordRpcClient *mClient;
    DiscordPresenceOptions mOptions;
    QElapsedTimer mClock;
    qint64 mLastInteraction {0};
    qint64 mSessionStartedAt;
    QTimer mRefreshTimer;
    QTimer mIdleTimer;
    bool mShutdown {false};
};
#endif
