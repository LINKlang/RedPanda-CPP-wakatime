#ifndef WAKATIMEHEARTBEAT_H
#define WAKATIMEHEARTBEAT_H

#include <QByteArray>
#include <QFlags>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "editoractivity.h"

enum class WakaTimeCategory {
    Coding,
    Building,
    Debugging,
};

QString wakaTimeCategoryName(WakaTimeCategory category);
WakaTimeCategory wakaTimeCategory(bool building, bool debugging);

struct WakaTimeHeartbeat {
    QString entity;
    qint64 timeMs {0};
    int lineNumber {0};
    int cursorPosition {0};
    int linesInFile {0};
    bool isWrite {false};
    WakaTimeCategory category {WakaTimeCategory::Coding};
    QString alternateProject;
    QString projectFolder;
};

WakaTimeHeartbeat makeWakaTimeHeartbeat(const EditorActivitySnapshot &activity, bool isWrite);

class WakaTimeHeartbeatPolicy {
public:
    static constexpr qint64 HEARTBEAT_INTERVAL_MS = 2 * 60 * 1000;
    static constexpr qint64 WRITE_DEDUPE_INTERVAL_MS = 10 * 60 * 1000;

    bool shouldAppend(const QString &entity,
                      qint64 timeMs,
                      int lineNumber,
                      int cursorPosition,
                      bool isWrite,
                      WakaTimeCategory category);
    void reset();

private:
    struct WriteDedupeEntry {
        qint64 lastHeartbeatAt {0};
        int lineNumber {0};
        int cursorPosition {0};
    };

    void pruneWriteDedupe(qint64 timeMs);

private:
    QString mLastFile;
    qint64 mLastHeartbeatAt {-1};
    WakaTimeCategory mLastCategory {WakaTimeCategory::Coding};
    QHash<QString, WriteDedupeEntry> mWriteDedupe;
};

struct WakaTimeCliCapabilities {
    bool extraHeartbeats {false};
    bool linesInFile {false};
    bool alternateProject {false};
    bool projectFolder {false};

    static WakaTimeCliCapabilities fromHelpOutput(const QByteArray &output);
};

struct WakaTimeCliInvocation {
    QStringList arguments;
    QByteArray standardInput;
};

WakaTimeCliInvocation makeWakaTimeCliInvocation(
    const QList<WakaTimeHeartbeat> &heartbeats,
    const QString &configFileName,
    const QString &plugin,
    const WakaTimeCliCapabilities &capabilities);

class WakaTimeHeartbeatBuffer {
public:
    static constexpr int MAX_BATCH_SIZE = 64;
    static constexpr qint64 SEND_INTERVAL_MS = 30 * 1000;

    void append(const WakaTimeHeartbeat &heartbeat);
    void prepend(const QList<WakaTimeHeartbeat> &heartbeats);
    QList<WakaTimeHeartbeat> takeBatch(bool supportsExtraHeartbeats);
    bool isEmpty() const;
    int size() const;
    void clear();

    bool shouldFlushNow(qint64 timeMs, qint64 lastSentAt) const;
    int nextFlushDelay(qint64 timeMs, qint64 lastSentAt) const;

private:
    QList<WakaTimeHeartbeat> mHeartbeats;
};

#endif // WAKATIMEHEARTBEAT_H
