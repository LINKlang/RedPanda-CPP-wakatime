#ifndef DISCORDRPCCLIENT_H
#define DISCORDRPCCLIENT_H

#include <QElapsedTimer>
#include <QJsonValue>
#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

class QLocalSocket;
QStringList discordIpcEndpoints(bool windows, const QProcessEnvironment &environment);
QByteArray discordRpcFrame(quint32 opcode, const QByteArray &payload);
bool isDiscordApplicationId(const QString &id);

class DiscordRpcClient: public QObject {
    Q_OBJECT
public:
    static constexpr int MAX_FRAME_BYTES = 64 * 1024;
    static constexpr int MAX_PAYLOAD_BYTES = MAX_FRAME_BYTES - 8;
    explicit DiscordRpcClient(QObject *parent = nullptr, const QStringList &endpoints = {});
    ~DiscordRpcClient() override;
    void start(const QString &applicationId);
    void setPresence(const QString &applicationId, const QJsonValue &activity,
                     bool invalidatePrevious = false);
    void clearActivity();
    void stop();
    bool shutdown(int timeoutMs = 2000);
    bool isReady() const { return mReady; }
    bool isRunning() const { return mRunning; }
    const QString &status() const { return mStatus; }
    const QString &lastError() const { return mLastError; }
    bool acknowledgedKnown() const { return mAcknowledgedKnown; }
    const QJsonValue &acknowledgedActivity() const { return mAcknowledged; }
signals:
    void ready();
    void statusChanged(const QString &status);
    void errorOccurred(const QString &message);
    void activityConfirmed(const QJsonValue &activity, quint64 revision);
    void clearFinished(bool confirmed);
    void stopped(bool clearConfirmed);
private:
    struct Request {
        bool active {false};
        bool submitted {false};
        QString nonce;
        QJsonValue activity;
        quint64 generation {0}, revision {0}, session {0};
        bool clear {false};
        bool urgent {false};
    };
    void configure(const QString &applicationId);
    void invalidate();
    void tryNextEndpoint();
    void beginHandshake(quint64 session);
    void readFrames(quint64 session);
    void handleMessage(const QByteArray &payload, quint64 session);
    bool writeFrame(quint32 opcode, const QByteArray &payload);
    void pump();
    void disconnectSocket();
    void connectionLost(const QString &message, bool immediate = false);
    void scheduleReconnect(bool immediate);
    void finishCleanup(bool confirmed);
    void requestTimedOut();
    void connectionTimedOut();
    void cleanupTimedOut();
    void setStatus(const QString &status);
    void reportError(const QString &message);

    QLocalSocket *mSocket {nullptr};
    QStringList mEndpointOverride, mEndpoints;
    int mEndpointIndex {0};
    QByteArray mReadBuffer;
    QString mApplicationId, mTargetApplicationId, mStatus, mLastError;
    QJsonValue mDesired, mAcknowledged;
    bool mHasDesired {false}, mAcknowledgedKnown {false}, mMayHaveActivity {false};
    bool mReady {false}, mRunning {false}, mStopping {false}, mSwitching {false};
    bool mClearBarrier {false};
    bool mUrgentDesired {false};
    quint64 mGeneration {0}, mRevision {0}, mSession {0}, mNextNonce {0}, mFailedRevision {0};
    Request mInFlight;
    QElapsedTimer mClock;
    qint64 mLastOrdinaryAt {-1}, mCooldownUntil {0};
    qint64 mRequestDeadline {-1}, mConnectionDeadline {-1}, mCleanupDeadline {-1};
    int mReconnectDelay {1000}, mRateDelay {1000};
    QTimer mConnectionTimer, mRequestTimer, mUpdateTimer, mRetryTimer, mCleanupTimer;
};
#endif
