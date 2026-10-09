#include "wakatimeheartbeat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

QString wakaTimeCategoryName(WakaTimeCategory category)
{
    switch (category) {
    case WakaTimeCategory::Building:
        return QStringLiteral("building");
    case WakaTimeCategory::Debugging:
        return QStringLiteral("debugging");
    case WakaTimeCategory::Coding:
    default:
        return QStringLiteral("coding");
    }
}

WakaTimeCategory wakaTimeCategory(bool building, bool debugging)
{
    if (debugging) {
        return WakaTimeCategory::Debugging;
    }
    if (building) {
        return WakaTimeCategory::Building;
    }
    return WakaTimeCategory::Coding;
}

WakaTimeHeartbeat makeWakaTimeHeartbeat(const EditorActivitySnapshot &activity, bool isWrite)
{
    WakaTimeHeartbeat heartbeat;
    heartbeat.entity = activity.filename;
    heartbeat.timeMs = activity.timeMs;
    heartbeat.lineNumber = activity.lineNumber;
    heartbeat.cursorPosition = activity.cursorPosition;
    heartbeat.linesInFile = activity.linesInFile;
    heartbeat.isWrite = isWrite;
    heartbeat.category = wakaTimeCategory(activity.building, activity.debugging);
    heartbeat.alternateProject = activity.projectName;
    heartbeat.projectFolder = activity.projectFolder;
    return heartbeat;
}

bool WakaTimeHeartbeatPolicy::shouldAppend(const QString &entity,
                                           qint64 timeMs,
                                           int lineNumber,
                                           int cursorPosition,
                                           bool isWrite,
                                           WakaTimeCategory category)
{
    const bool candidate = isWrite
        || mLastHeartbeatAt < 0
        || mLastHeartbeatAt + HEARTBEAT_INTERVAL_MS < timeMs
        || mLastFile != entity
        || mLastCategory != category;
    if (!candidate) {
        return false;
    }

    mLastFile = entity;
    mLastHeartbeatAt = timeMs;
    mLastCategory = category;

    if (!isWrite) {
        return true;
    }

    pruneWriteDedupe(timeMs);
    const auto existing = mWriteDedupe.constFind(entity);
    const bool duplicate = existing != mWriteDedupe.constEnd()
        && existing->lastHeartbeatAt + WRITE_DEDUPE_INTERVAL_MS > timeMs
        && existing->lineNumber == lineNumber
        && existing->cursorPosition == cursorPosition;

    mWriteDedupe.insert(entity, WriteDedupeEntry{timeMs, lineNumber, cursorPosition});
    return !duplicate;
}

void WakaTimeHeartbeatPolicy::reset()
{
    mLastFile.clear();
    mLastHeartbeatAt = -1;
    mLastCategory = WakaTimeCategory::Coding;
    mWriteDedupe.clear();
}

void WakaTimeHeartbeatPolicy::pruneWriteDedupe(qint64 timeMs)
{
    for (auto it = mWriteDedupe.begin(); it != mWriteDedupe.end();) {
        if (it->lastHeartbeatAt + WRITE_DEDUPE_INTERVAL_MS <= timeMs) {
            it = mWriteDedupe.erase(it);
        } else {
            ++it;
        }
    }
}

WakaTimeCliCapabilities WakaTimeCliCapabilities::fromHelpOutput(const QByteArray &output)
{
    WakaTimeCliCapabilities result;
    result.extraHeartbeats = output.contains("--extra-heartbeats");
    result.linesInFile = output.contains("--lines-in-file");
    result.alternateProject = output.contains("--alternate-project");
    result.projectFolder = output.contains("--project-folder");
    return result;
}

static QString heartbeatTime(qint64 timeMs)
{
    return QString::number(timeMs / 1000.0, 'f', 3);
}

static QJsonObject heartbeatToJson(const WakaTimeHeartbeat &heartbeat,
                                   const WakaTimeCliCapabilities &capabilities)
{
    QJsonObject result;
    result.insert(QStringLiteral("entity"), heartbeat.entity);
    result.insert(QStringLiteral("time"), heartbeat.timeMs / 1000.0);
    result.insert(QStringLiteral("is_write"), heartbeat.isWrite);
    result.insert(QStringLiteral("lineno"), heartbeat.lineNumber);
    result.insert(QStringLiteral("cursorpos"), heartbeat.cursorPosition);
    result.insert(QStringLiteral("category"), wakaTimeCategoryName(heartbeat.category));
    if (capabilities.linesInFile) {
        result.insert(QStringLiteral("lines_in_file"), heartbeat.linesInFile);
    }
    if (capabilities.alternateProject && !heartbeat.alternateProject.isEmpty()) {
        result.insert(QStringLiteral("alternate_project"), heartbeat.alternateProject);
    }
    if (capabilities.projectFolder && !heartbeat.projectFolder.isEmpty()) {
        result.insert(QStringLiteral("project_folder"), heartbeat.projectFolder);
    }
    return result;
}

WakaTimeCliInvocation makeWakaTimeCliInvocation(
    const QList<WakaTimeHeartbeat> &heartbeats,
    const QString &configFileName,
    const QString &plugin,
    const WakaTimeCliCapabilities &capabilities)
{
    WakaTimeCliInvocation result;
    if (heartbeats.isEmpty()) {
        return result;
    }

    const WakaTimeHeartbeat &heartbeat = heartbeats.first();
    result.arguments
        << QStringLiteral("--config") << configFileName
        << QStringLiteral("--entity") << heartbeat.entity
        << QStringLiteral("--plugin") << plugin
        << QStringLiteral("--time") << heartbeatTime(heartbeat.timeMs)
        << QStringLiteral("--lineno") << QString::number(heartbeat.lineNumber)
        << QStringLiteral("--cursorpos") << QString::number(heartbeat.cursorPosition)
        << QStringLiteral("--category") << wakaTimeCategoryName(heartbeat.category);
    if (capabilities.linesInFile) {
        result.arguments << QStringLiteral("--lines-in-file")
                         << QString::number(heartbeat.linesInFile);
    }
    if (capabilities.alternateProject && !heartbeat.alternateProject.isEmpty()) {
        result.arguments << QStringLiteral("--alternate-project")
                         << heartbeat.alternateProject;
    }
    if (capabilities.projectFolder && !heartbeat.projectFolder.isEmpty()) {
        result.arguments << QStringLiteral("--project-folder")
                         << heartbeat.projectFolder;
    }
    if (heartbeat.isWrite) {
        result.arguments << QStringLiteral("--write");
    }

    if (capabilities.extraHeartbeats && heartbeats.size() > 1) {
        QJsonArray extraHeartbeats;
        for (int i = 1; i < heartbeats.size(); ++i) {
            extraHeartbeats.append(heartbeatToJson(heartbeats.at(i), capabilities));
        }
        result.arguments << QStringLiteral("--extra-heartbeats");
        result.standardInput = QJsonDocument(extraHeartbeats).toJson(QJsonDocument::Compact);
        result.standardInput.append('\n');
    }
    return result;
}

void WakaTimeHeartbeatBuffer::append(const WakaTimeHeartbeat &heartbeat)
{
    mHeartbeats.append(heartbeat);
}

void WakaTimeHeartbeatBuffer::prepend(const QList<WakaTimeHeartbeat> &heartbeats)
{
    if (heartbeats.isEmpty()) {
        return;
    }
    QList<WakaTimeHeartbeat> combined = heartbeats;
    combined.append(mHeartbeats);
    mHeartbeats = std::move(combined);
}

QList<WakaTimeHeartbeat> WakaTimeHeartbeatBuffer::takeBatch(bool supportsExtraHeartbeats)
{
    const int heartbeatCount = static_cast<int>(mHeartbeats.size());
    const int count = supportsExtraHeartbeats
        ? std::min(MAX_BATCH_SIZE, heartbeatCount)
        : std::min(1, heartbeatCount);
    QList<WakaTimeHeartbeat> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(mHeartbeats.takeFirst());
    }
    return result;
}

bool WakaTimeHeartbeatBuffer::isEmpty() const
{
    return mHeartbeats.isEmpty();
}

int WakaTimeHeartbeatBuffer::size() const
{
    return mHeartbeats.size();
}

void WakaTimeHeartbeatBuffer::clear()
{
    mHeartbeats.clear();
}

bool WakaTimeHeartbeatBuffer::shouldFlushNow(qint64 timeMs, qint64 lastSentAt) const
{
    return !mHeartbeats.isEmpty()
        && (lastSentAt <= 0
            || timeMs - lastSentAt >= SEND_INTERVAL_MS
            || mHeartbeats.size() >= MAX_BATCH_SIZE);
}

int WakaTimeHeartbeatBuffer::nextFlushDelay(qint64 timeMs, qint64 lastSentAt) const
{
    if (mHeartbeats.isEmpty()) {
        return -1;
    }
    if (shouldFlushNow(timeMs, lastSentAt)) {
        return 0;
    }
    return static_cast<int>(std::max<qint64>(1, SEND_INTERVAL_MS - (timeMs - lastSentAt)));
}
