#include "discordrpcclient.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QLocalSocket>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
Q_LOGGING_CATEGORY(discordRpcLog, "redpanda.discordrpc", QtWarningMsg)
}

QStringList discordIpcEndpoints(bool windows, const QProcessEnvironment &environment)
{
    QStringList result;
    if (windows) {
        for (int n = 0; n < 10; ++n) result.append(QStringLiteral("discord-ipc-%1").arg(n));
        return result;
    }
    QStringList directories;
    QSet<QString> seen;
    for (const QString &key: {QStringLiteral("XDG_RUNTIME_DIR"), QStringLiteral("TMPDIR"),
                             QStringLiteral("TMP"), QStringLiteral("TEMP")}) {
        const QString value = environment.value(key);
        if (value.isEmpty() || !QDir::isAbsolutePath(value)) continue;
        const QString directory = QDir::cleanPath(value);
        if (!seen.contains(directory)) {
            seen.insert(directory);
            directories.append(directory);
        }
    }
    if (!seen.contains(QStringLiteral("/tmp"))) directories.append(QStringLiteral("/tmp"));
    for (const QString &directory: directories)
        for (int n = 0; n < 10; ++n)
            result.append(QDir(directory).filePath(QStringLiteral("discord-ipc-%1").arg(n)));
    return result;
}

QByteArray discordRpcFrame(quint32 opcode, const QByteArray &payload)
{
    if (payload.size() > DiscordRpcClient::MAX_PAYLOAD_BYTES) return {};
    QByteArray frame;
    frame.reserve(8 + payload.size());
    const quint32 op = qToLittleEndian(opcode);
    const quint32 length = qToLittleEndian(static_cast<quint32>(payload.size()));
    frame.append(reinterpret_cast<const char *>(&op), 4);
    frame.append(reinterpret_cast<const char *>(&length), 4);
    frame.append(payload);
    return frame;
}

bool isDiscordApplicationId(const QString &id)
{
    if (id.isEmpty()) return false;
    for (const QChar ch: id)
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
    bool valid = false;
    const qulonglong number = id.toULongLong(&valid);
    return valid && number != 0;
}

DiscordRpcClient::DiscordRpcClient(QObject *parent, const QStringList &endpoints):
    QObject(parent), mEndpointOverride(endpoints)
{
    mClock.start();
    for (QTimer *timer: {&mConnectionTimer, &mRequestTimer, &mUpdateTimer,
                        &mRetryTimer, &mCleanupTimer}) timer->setSingleShot(true);
    connect(&mConnectionTimer, &QTimer::timeout, this, &DiscordRpcClient::connectionTimedOut);
    connect(&mRequestTimer, &QTimer::timeout, this, &DiscordRpcClient::requestTimedOut);
    connect(&mUpdateTimer, &QTimer::timeout, this, &DiscordRpcClient::pump);
    connect(&mRetryTimer, &QTimer::timeout, this, [this] {
        mEndpointIndex = 0;
        tryNextEndpoint();
    });
    connect(&mCleanupTimer, &QTimer::timeout, this, &DiscordRpcClient::cleanupTimedOut);
    setStatus(tr("Disabled"));
}

DiscordRpcClient::~DiscordRpcClient() { disconnectSocket(); }

void DiscordRpcClient::setStatus(const QString &status)
{
    if (mStatus == status) return;
    mStatus = status;
    emit statusChanged(status);
}

void DiscordRpcClient::reportError(const QString &message)
{
    mLastError = message;
    emit errorOccurred(message);
}

void DiscordRpcClient::invalidate()
{
    ++mGeneration;
    mUpdateTimer.stop();
    mFailedRevision = 0;
    if (mInFlight.active && !mInFlight.submitted) {
        mInFlight = Request{};
        mRequestTimer.stop();
        mRequestDeadline = -1;
    }
    if (mInFlight.active) {
        mRequestDeadline = std::min(mRequestDeadline, mClock.elapsed() + 500);
        mRequestTimer.start(static_cast<int>(std::max<qint64>(1, mRequestDeadline - mClock.elapsed())));
    }
}

void DiscordRpcClient::start(const QString &applicationId) { configure(applicationId.trimmed()); }

void DiscordRpcClient::setPresence(const QString &applicationId, const QJsonValue &activity,
                                   bool invalidatePrevious)
{
    if (invalidatePrevious) invalidate();
    if (!mHasDesired || mDesired != activity || invalidatePrevious) {
        mDesired = activity;
        mHasDesired = true;
        ++mRevision;
    }
    if (invalidatePrevious || activity.isNull()) {
        mUrgentDesired = invalidatePrevious;
        mClearBarrier = true;
        mAcknowledgedKnown = false;
    }
    configure(applicationId.trimmed());
    pump();
}

void DiscordRpcClient::configure(const QString &id)
{
    if (mRunning && !mStopping && mTargetApplicationId == id) {
        if (!mSocket && !mRetryTimer.isActive()) scheduleReconnect(true);
        return;
    }
    const bool oldContext = !mApplicationId.isEmpty()
        && mApplicationId != id && (mSocket || mMayHaveActivity);
    invalidate();
    mRunning = true;
    mStopping = false;
    mTargetApplicationId = id;
    mCleanupTimer.stop();
    mCleanupDeadline = -1;
    if (oldContext) {
        mUrgentDesired = true;
        mSwitching = true;
        mClearBarrier = true;
        mAcknowledgedKnown = false;
        mCleanupDeadline = mClock.elapsed() + 2000;
        mCleanupTimer.start(2000);
        if (!mSocket) scheduleReconnect(true);
        pump();
        return;
    }
    mSwitching = false;
    disconnectSocket();
    mApplicationId = id;
    if (!isDiscordApplicationId(id)) {
        mRunning = false;
        setStatus(tr("Invalid application ID"));
        reportError(tr("Application ID must be a positive decimal identifier."));
        return;
    }
    mEndpoints = mEndpointOverride.isEmpty() ? discordIpcEndpoints(
#ifdef Q_OS_WIN
        true,
#else
        false,
#endif
        QProcessEnvironment::systemEnvironment()) : mEndpointOverride;
    mEndpointIndex = 0;
    tryNextEndpoint();
}

void DiscordRpcClient::tryNextEndpoint()
{
    if ((!mRunning && !mStopping && !mSwitching) || mSocket) return;
    if (mEndpointIndex >= mEndpoints.size()) {
        setStatus(tr("Discord is not available"));
        scheduleReconnect(false);
        return;
    }
    const QString endpoint = mEndpoints.at(mEndpointIndex++);
    const quint64 session = ++mSession;
    QLocalSocket *socket = new QLocalSocket(this);
    mSocket = socket;
    socket->setReadBufferSize(MAX_FRAME_BYTES);
    connect(socket, &QLocalSocket::connected, this, [this, session] {
        if (session == mSession) beginHandshake(session);
    });
    connect(socket, &QLocalSocket::readyRead, this, [this, session] {
        if (session == mSession) readFrames(session);
    });
    connect(socket, &QLocalSocket::disconnected, this, [this, session] {
        if (session == mSession) connectionLost(tr("Discord disconnected."));
    });
    connect(socket, &QLocalSocket::errorOccurred, this,
            [this, session](QLocalSocket::LocalSocketError) {
        if (session != mSession || !mSocket) return;
        const QString message = mSocket->errorString();
        if (!mReady) {
            disconnectSocket();
            QTimer::singleShot(0, this, [this, session] {
                if (mSession == session + 1) tryNextEndpoint();
            });
        } else connectionLost(message);
    });
    setStatus(tr("Connecting"));
    mConnectionDeadline = mClock.elapsed() + 500;
    mConnectionTimer.start(500);
#ifdef Q_OS_WIN
    const QString pipe = endpoint.startsWith(QStringLiteral("\\\\"))
        ? endpoint : QStringLiteral("\\\\?\\pipe\\") + endpoint;
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(pipe.utf16()),
                               GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        disconnectSocket();
        QTimer::singleShot(0, this, [this, session] {
            if (mSession == session + 1) tryNextEndpoint();
        });
        return;
    }
    if (!socket->setSocketDescriptor(reinterpret_cast<qintptr>(handle),
                                    QLocalSocket::ConnectedState, QIODevice::ReadWrite)) {
        CloseHandle(handle);
        disconnectSocket();
        QTimer::singleShot(0, this, [this, session] {
            if (mSession == session + 1) tryNextEndpoint();
        });
        return;
    }
    beginHandshake(session);
#else
    socket->connectToServer(endpoint);
#endif
}

void DiscordRpcClient::beginHandshake(quint64 session)
{
    if (session != mSession || !mSocket) return;
    mConnectionDeadline = mClock.elapsed() + 2000;
    mConnectionTimer.start(2000);
    setStatus(tr("Handshaking"));
    writeFrame(0, QJsonDocument(QJsonObject{{QStringLiteral("v"), 1},
        {QStringLiteral("client_id"), mApplicationId}}).toJson(QJsonDocument::Compact));
}

bool DiscordRpcClient::writeFrame(quint32 opcode, const QByteArray &payload)
{
    const QByteArray frame = discordRpcFrame(opcode, payload);
    if (!mSocket || frame.isEmpty() || mSocket->write(frame) != frame.size()) {
        connectionLost(tr("Unable to submit Discord IPC frame."));
        return false;
    }
    return true;
}

void DiscordRpcClient::readFrames(quint64 session)
{
    if (session != mSession || !mSocket) return;
    mReadBuffer.append(mSocket->readAll());
    while (session == mSession && mSocket && mReadBuffer.size() >= 8) {
        const auto *bytes = reinterpret_cast<const uchar *>(mReadBuffer.constData());
        const quint32 opcode = qFromLittleEndian<quint32>(bytes);
        const quint32 length = qFromLittleEndian<quint32>(bytes + 4);
        if (length > MAX_PAYLOAD_BYTES || opcode > 4 || opcode == 0) {
            connectionLost(tr("Invalid Discord IPC frame."));
            return;
        }
        if (mReadBuffer.size() < static_cast<int>(8 + length)) return;
        const QByteArray payload = mReadBuffer.mid(8, static_cast<int>(length));
        mReadBuffer.remove(0, static_cast<int>(8 + length));
        if (opcode == 3) {
            if (!writeFrame(4, payload)) return;
        } else if (opcode == 2) {
            const QJsonObject error = QJsonDocument::fromJson(payload).object();
            connectionLost(error.value(QStringLiteral("message")).toString(tr("Discord closed IPC.")));
            return;
        } else if (opcode == 1) handleMessage(payload, session);
    }
}

void DiscordRpcClient::handleMessage(const QByteArray &payload, quint64 session)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        connectionLost(tr("Invalid Discord IPC JSON."));
        return;
    }
    const QJsonObject message = document.object();
    const QString cmd = message.value(QStringLiteral("cmd")).toString();
    const QString event = message.value(QStringLiteral("evt")).toString();
    if (!mReady) {
        if (cmd == QStringLiteral("DISPATCH") && event == QStringLiteral("READY")) {
            mReady = true;
            mConnectionTimer.stop();
            mConnectionDeadline = -1;
            mReconnectDelay = 1000;
            mLastError.clear();
            setStatus(tr("Ready"));
            emit ready();
            if (session == mSession) pump();
        } else if (event == QStringLiteral("ERROR"))
            connectionLost(message.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("message")).toString(tr("Discord rejected handshake.")));
        return;
    }
    if (!mInFlight.active || session != mInFlight.session
        || cmd != QStringLiteral("SET_ACTIVITY")
        || message.value(QStringLiteral("nonce")).toString() != mInFlight.nonce) return;
    if (discordRpcLog().isDebugEnabled()) {
        const QJsonObject data = message.value(QStringLiteral("data")).toObject();
        const QJsonObject assets = data.value(QStringLiteral("assets")).toObject();
        QJsonObject summary{{QStringLiteral("evt"), message.value(QStringLiteral("evt"))},
                            {QStringLiteral("nonce"), mInFlight.nonce},
                            {QStringLiteral("data_keys"), QJsonArray::fromStringList(data.keys())},
                            {QStringLiteral("data_is_null"), message.value(QStringLiteral("data")).isNull()}};
        for (const QString &key: {QStringLiteral("application_id"), QStringLiteral("type"),
                                 QStringLiteral("pid")})
            summary.insert(key, data.value(key));
        for (const QString &key: {QStringLiteral("large_image"), QStringLiteral("small_image")})
            summary.insert(key, assets.value(key));
        // Deliberately omit details, state and hover text, which may contain private names.
        qCDebug(discordRpcLog).noquote() << "SET_ACTIVITY response:"
            << QJsonDocument(summary).toJson(QJsonDocument::Compact);
    }
    const Request request = mInFlight;
    mInFlight = Request{};
    mRequestTimer.stop();
    mRequestDeadline = -1;
    const bool current = request.generation == mGeneration;
    if (event == QStringLiteral("ERROR")) {
        const QJsonObject data = message.value(QStringLiteral("data")).toObject();
        const QString text = data.value(QStringLiteral("message")).toString(tr("Discord rejected activity."));
        const bool limited = data.value(QStringLiteral("code")).toInt() == 429
            || (text.contains(QStringLiteral("rate"), Qt::CaseInsensitive)
                && text.contains(QStringLiteral("limit"), Qt::CaseInsensitive));
        if (limited) {
            if (current && request.urgent) mUrgentDesired = true;
            const double retry = data.value(QStringLiteral("retry_after")).toDouble();
            const qint64 delay = retry > 0 && retry < 86400
                ? static_cast<qint64>(retry * 1000) : mRateDelay;
            mRateDelay = std::min(mRateDelay * 2, 30000);
            mCooldownUntil = mClock.elapsed() + std::max<qint64>(1, delay);
        } else if (request.clear) {
            mCooldownUntil = mClock.elapsed() + mRateDelay;
            mRateDelay = std::min(mRateDelay * 2, 30000);
        } else if (current) {
            mFailedRevision = request.revision;
        }
        if (current) {
            mAcknowledgedKnown = false;
            reportError(text);
            setStatus(request.clear ? tr("Clear not confirmed") : tr("Publication failed"));
        }
        pump();
        return;
    }
    if (current) {
        mLastError.clear();
        mAcknowledged = request.activity;
        mAcknowledgedKnown = true;
        mRateDelay = 1000;
        if (request.clear) {
            mMayHaveActivity = false;
            mClearBarrier = false;
            setStatus(tr("Clear confirmed"));
            emit clearFinished(true);
            if (request.generation != mGeneration || session != mSession) return;
            if (mStopping || mSwitching) {
                finishCleanup(true);
                return;
            }
        } else setStatus(request.revision == mRevision ? tr("Publication confirmed") : tr("Update pending"));
        emit activityConfirmed(request.activity, request.revision);
    }
    if (session == mSession) pump();
}

void DiscordRpcClient::pump()
{
    if (!mReady || !mSocket || mInFlight.active) return;
    if (!mRunning && !mStopping && !mSwitching) return;
    const bool clear = mClearBarrier || mStopping || mSwitching
        || (mHasDesired && mDesired.isNull());
    if (!clear && (!mHasDesired || mFailedRevision == mRevision
        || (mAcknowledgedKnown && mAcknowledged == mDesired))) return;
    if (clear && !mClearBarrier && !mStopping && !mSwitching
        && mAcknowledgedKnown && mAcknowledged.isNull()) return;
    const qint64 now = mClock.elapsed();
    qint64 due = mCooldownUntil;
    if (!clear && !mUrgentDesired && mLastOrdinaryAt >= 0)
        due = std::max(due, mLastOrdinaryAt + 5000);
    if (due > now) {
        mUpdateTimer.start(static_cast<int>(std::min<qint64>(due - now, 86400000)));
        return;
    }
    Request request;
    request.active = true;
    request.nonce = QString::number(++mNextNonce);
    request.activity = clear ? QJsonValue(QJsonValue::Null) : mDesired;
    request.generation = mGeneration;
    request.revision = mRevision;
    request.session = mSession;
    request.clear = clear;
    request.urgent = !clear && mUrgentDesired;
    const QJsonObject command{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("nonce"), request.nonce},
        {QStringLiteral("args"), QJsonObject{
            {QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid())},
            {QStringLiteral("activity"), request.activity}}}};
    mInFlight = request;
    mRequestDeadline = now + 2000;
    mRequestTimer.start(2000);
    setStatus(clear ? tr("Clearing activity") : tr("Submitting activity"));
    // A status listener may synchronously apply another policy.
    if (mGeneration == request.generation && mSession == request.session) {
        mInFlight.submitted = true;
        if (!clear) {
            mUrgentDesired = false;
            mLastOrdinaryAt = now;
            mMayHaveActivity = true;
        }
        writeFrame(1, QJsonDocument(command).toJson(QJsonDocument::Compact));
    }
}

void DiscordRpcClient::disconnectSocket()
{
    ++mSession;
    mReady = false;
    mConnectionTimer.stop();
    mConnectionDeadline = -1;
    mRequestTimer.stop();
    mRequestDeadline = -1;
    mInFlight = Request{};
    mReadBuffer.clear();
    if (mSocket) {
        QLocalSocket *socket = mSocket;
        mSocket = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

void DiscordRpcClient::connectionLost(const QString &message, bool immediate)
{
    mUrgentDesired = true;
    if (mMayHaveActivity || mStopping || mSwitching) mClearBarrier = true;
    mAcknowledgedKnown = false;
    disconnectSocket();
    reportError(message);
    setStatus(tr("Disconnected; activity state unknown"));
    scheduleReconnect(immediate);
}

void DiscordRpcClient::scheduleReconnect(bool immediate)
{
    if ((!mRunning && !mStopping && !mSwitching) || mRetryTimer.isActive()) return;
    const qint64 cooldown = std::max<qint64>(0, mCooldownUntil - mClock.elapsed());
    const int delay = static_cast<int>(std::max<qint64>(immediate ? 0 : mReconnectDelay, cooldown));
    mReconnectDelay = std::min(mReconnectDelay * 2, 30000);
    mRetryTimer.start(delay);
}

void DiscordRpcClient::connectionTimedOut()
{
    if (!mSocket || mConnectionDeadline < 0) return;
    const qint64 remaining = mConnectionDeadline - mClock.elapsed();
    if (remaining > 0) { mConnectionTimer.start(static_cast<int>(remaining)); return; }
    disconnectSocket();
    tryNextEndpoint();
}

void DiscordRpcClient::requestTimedOut()
{
    if (!mInFlight.active || mRequestDeadline < 0) return;
    const qint64 remaining = mRequestDeadline - mClock.elapsed();
    if (remaining > 0) { mRequestTimer.start(static_cast<int>(remaining)); return; }
    connectionLost(tr("Discord did not confirm the request."), true);
}

void DiscordRpcClient::clearActivity()
{
    invalidate();
    mDesired = QJsonValue(QJsonValue::Null);
    mHasDesired = true;
    ++mRevision;
    mClearBarrier = true;
    mAcknowledgedKnown = false;
    pump();
}

void DiscordRpcClient::stop()
{
    if (mStopping) return;
    invalidate();
    mRunning = false;
    mSwitching = false;
    mStopping = true;
    mDesired = QJsonValue(QJsonValue::Null);
    mHasDesired = true;
    ++mRevision;
    mClearBarrier = true;
    mCleanupDeadline = mClock.elapsed() + 2000;
    mCleanupTimer.start(2000);
    // A handshake-only connection has never submitted an activity.
    if (!mMayHaveActivity && !mInFlight.active) {
        mCleanupTimer.stop();
        mCleanupDeadline = -1;
        mRetryTimer.stop();
        mUpdateTimer.stop();
        disconnectSocket();
        mStopping = false;
        setStatus(tr("Disabled"));
        emit stopped(mAcknowledgedKnown && mAcknowledged.isNull());
        return;
    }
    if (!mSocket) scheduleReconnect(true);
    pump();
}

void DiscordRpcClient::cleanupTimedOut()
{
    if ((!mStopping && !mSwitching) || mCleanupDeadline < 0) return;
    const qint64 remaining = mCleanupDeadline - mClock.elapsed();
    if (remaining > 0) { mCleanupTimer.start(static_cast<int>(remaining)); return; }
    finishCleanup(false);
}

void DiscordRpcClient::finishCleanup(bool confirmed)
{
    const bool switching = mSwitching;
    mCleanupTimer.stop();
    mCleanupDeadline = -1;
    mRetryTimer.stop();
    mUpdateTimer.stop();
    disconnectSocket();
    mStopping = false;
    mSwitching = false;
    mAcknowledgedKnown = confirmed;
    if (confirmed) {
        mAcknowledged = QJsonValue(QJsonValue::Null);
        mMayHaveActivity = false;
    } else {
        reportError(tr("Activity clear was not confirmed before the deadline."));
        emit clearFinished(false);
    }
    if (switching) {
        mApplicationId.clear();
        mClearBarrier = true;
        const QString target = mTargetApplicationId;
        mRunning = false;
        configure(target);
    } else {
        mRunning = false;
        setStatus(confirmed ? tr("Disabled; clear confirmed") : tr("Disabled; clear not confirmed"));
        emit stopped(confirmed);
    }
}

bool DiscordRpcClient::shutdown(int timeoutMs)
{
    stop();
    if (mStopping) {
        mCleanupDeadline = std::min(mCleanupDeadline, mClock.elapsed() + std::max(0, timeoutMs));
        mCleanupTimer.start(static_cast<int>(std::max<qint64>(0, mCleanupDeadline - mClock.elapsed())));
        QEventLoop loop;
        connect(this, &DiscordRpcClient::stopped, &loop, &QEventLoop::quit);
        if (mStopping) loop.exec();
        // Application quit can end the nested wait before its cleanup deadline.
        if (mStopping) finishCleanup(false);
    }
    return mAcknowledgedKnown && mAcknowledged.isNull();
}
