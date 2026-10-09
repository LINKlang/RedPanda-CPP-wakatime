#ifndef WAKATIMEMANAGER_H
#define WAKATIMEMANAGER_H

#include "wakatimeheartbeat.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

class Editor;
class EditorActivityCollector;
class MainWindow;
class QProcess;

class WakaTimeManager: public QObject {
    Q_OBJECT
public:
    WakaTimeManager(MainWindow *mainWindow, EditorActivityCollector *activityCollector);

    void reloadSettings();
    void shutdown(int timeoutMs = 2000);

private:
    void onActivityCollected(Editor *editor, const EditorActivitySnapshot &activity,
                             EditorActivityKind kind);
    void debounceEditorActivity(Editor *editor);
    void appendHeartbeat(const EditorActivitySnapshot &activity, bool isWrite);
    bool shouldSkipFile(const QString &filename) const;

    void beginCapabilityProbe();
    void finishCapabilityProbe(QProcess *process, const QByteArray &output);
    void stopCapabilityProbe();

    void enqueueHeartbeat(const WakaTimeHeartbeat &heartbeat);
    void scheduleFlush();
    void flushPending();
    void finishSendProcess(QProcess *process, bool requeueInFlight);
    bool canSendPending() const;

    void logDebug(const QString &message) const;
    QString stderrSummary(const QByteArray &stderrOutput) const;

private:
    MainWindow *mMainWindow;
    EditorActivityCollector *mActivityCollector;
    bool mEnabled;
    bool mDebugEnabled;
    bool mDraining {false};
    bool mBatchDrainActive {false};
    bool mShuttingDown {false};
    bool mProbeComplete {false};
    QString mCliPath;
    QString mConfigFileName;
    QString mPlugin;

    WakaTimeHeartbeatPolicy mPolicy;
    WakaTimeHeartbeatBuffer mBuffer;
    QList<WakaTimeHeartbeat> mInFlightHeartbeats;
    WakaTimeCliCapabilities mCliCapabilities;
    QHash<QString, WakaTimeCliCapabilities> mCapabilityCache;

    QTimer mActivityDebounceTimer;
    QTimer mFlushTimer;
    QTimer mRetryTimer;
    QPointer<Editor> mPendingActivityEditor;
    QProcess *mSendProcess {nullptr};
    QProcess *mProbeProcess {nullptr};
    qint64 mLastSentAt {0};
    int mRetryDelayMs {1000};
};

#endif // WAKATIMEMANAGER_H
