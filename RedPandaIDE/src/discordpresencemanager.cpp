#include "discordpresencemanager.h"
#include "discordrpcclient.h"
#include "editor.h"
#include "editoractivitycollector.h"
#include "mainwindow.h"
#include "settings.h"
#include <QDateTime>

DiscordPresenceManager::DiscordPresenceManager(MainWindow *mainWindow, EditorActivityCollector *collector):
    QObject(mainWindow), mMainWindow(mainWindow), mCollector(collector),
    mClient(new DiscordRpcClient(this)), mSessionStartedAt(QDateTime::currentSecsSinceEpoch())
{
    mClock.start();
    mRefreshTimer.setSingleShot(true);
    mIdleTimer.setSingleShot(true);
    connect(&mRefreshTimer, &QTimer::timeout, this, &DiscordPresenceManager::refresh);
    connect(&mIdleTimer, &QTimer::timeout, this, &DiscordPresenceManager::refresh);
    connect(mCollector, &EditorActivityCollector::activityCollected, this,
            [this](Editor *, const EditorActivitySnapshot &, EditorActivityKind kind) {
        if (mShutdown) return;
        if (kind == EditorActivityKind::UserInteraction) {
            mLastInteraction = mClock.elapsed();
            scheduleIdle();
        }
        scheduleRefresh();
    });
    connect(mClient, &DiscordRpcClient::errorOccurred, this, [this](const QString &message) {
        mMainWindow->logToolsOutput(QStringLiteral("[Discord RPC] %1").arg(message));
    });
}

void DiscordPresenceManager::reloadSettings()
{
    if (mShutdown) return;
    const DiscordPresenceOptions next = pSettings->discordPresence().options();
    const bool boundary = next.privacyMode != mOptions.privacyMode
        || next.applicationId() != mOptions.applicationId();
    mOptions = next;
    if (!mOptions.enabled) {
        mRefreshTimer.stop();
        mIdleTimer.stop();
        if (mClient->isRunning()) mClient->stop();
        return;
    }
    const EditorActivitySnapshot activity = mCollector->currentActivity();
    const bool idle = discordPresenceIdle(mClock.elapsed() - mLastInteraction, mOptions.idleTimeoutMinutes);
    if (activity.restoringFiles) {
        if (!boundary) {
            mClient->start(mOptions.applicationId());
            scheduleIdle();
            return;
        }
        // Apply policy immediately, even while the editor context is being restored.
        mClient->setPresence(mOptions.applicationId(),
            makeDiscordPresence(EditorActivitySnapshot{}, mOptions, idle, mSessionStartedAt), boundary);
    } else {
        mClient->setPresence(mOptions.applicationId(),
            makeDiscordPresence(activity, mOptions, idle, mSessionStartedAt), boundary);
    }
    scheduleIdle();
}

void DiscordPresenceManager::scheduleRefresh()
{
    if (mOptions.enabled && !mShutdown && !mRefreshTimer.isActive()) mRefreshTimer.start(0);
}

void DiscordPresenceManager::scheduleIdle()
{
    if (!mOptions.enabled || mShutdown) return;
    const qint64 remaining = qint64(mOptions.idleTimeoutMinutes) * 60000
        - (mClock.elapsed() - mLastInteraction);
    if (remaining > 0) mIdleTimer.start(static_cast<int>(remaining));
    else mIdleTimer.stop();
}

void DiscordPresenceManager::refresh()
{
    if (!mOptions.enabled || mShutdown) return;
    const EditorActivitySnapshot activity = mCollector->currentActivity();
    if (activity.restoringFiles) return;
    mClient->setPresence(mOptions.applicationId(), makeDiscordPresence(activity, mOptions,
        discordPresenceIdle(mClock.elapsed() - mLastInteraction, mOptions.idleTimeoutMinutes),
        mSessionStartedAt));
    scheduleIdle();
}

void DiscordPresenceManager::shutdown()
{
    if (mShutdown) return;
    mShutdown = true;
    mRefreshTimer.stop();
    mIdleTimer.stop();
    mClient->shutdown();
}
