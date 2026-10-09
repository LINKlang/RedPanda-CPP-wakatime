#include "test_wakatimeheartbeat.h"

#include "src/wakatimeheartbeat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

void TestWakaTimeHeartbeat::testActivityChanges()
{
    QVERIFY(isEditorActivity(QSynedit::StatusChange::CaretX));
    QVERIFY(isEditorActivity(QSynedit::StatusChange::CaretY));
    QVERIFY(isEditorActivity(QSynedit::StatusChange::Selection));
    QVERIFY(isEditorActivity(QSynedit::StatusChange::Modified));
    QVERIFY(!isEditorActivity(QSynedit::StatusChange::LeftPos));
    QVERIFY(!isEditorActivity(QSynedit::StatusChange::TopPos));
    QVERIFY(!isEditorActivity(QSynedit::StatusChange::ReadOnlyChanged));
    QVERIFY(!isEditorActivity(QSynedit::StatusChange::Custom0));
}

void TestWakaTimeHeartbeat::testHeartbeatIntervalAndFileChanges()
{
    WakaTimeHeartbeatPolicy policy;
    const qint64 start = 1000;

    QVERIFY(policy.shouldAppend("A.cpp", start, 1, 1, false, WakaTimeCategory::Coding));
    QVERIFY(!policy.shouldAppend("A.cpp", start + 60000, 1, 2, false,
                                 WakaTimeCategory::Coding));
    QVERIFY(!policy.shouldAppend("A.cpp",
                                 start + WakaTimeHeartbeatPolicy::HEARTBEAT_INTERVAL_MS,
                                 1, 3, false, WakaTimeCategory::Coding));
    QVERIFY(policy.shouldAppend("A.cpp",
                                start + WakaTimeHeartbeatPolicy::HEARTBEAT_INTERVAL_MS + 1,
                                1, 4, false, WakaTimeCategory::Coding));

    policy.reset();
    QVERIFY(policy.shouldAppend("A.cpp", start, 1, 1, false, WakaTimeCategory::Coding));
    QVERIFY(policy.shouldAppend("B.cpp", start + 1000, 1, 1, false,
                                WakaTimeCategory::Coding));
    QVERIFY(policy.shouldAppend("A.cpp", start + 2000, 1, 1, false,
                                WakaTimeCategory::Coding));
}

void TestWakaTimeHeartbeat::testWriteDedupeAndSlidingWindow()
{
    WakaTimeHeartbeatPolicy policy;
    const qint64 start = 1000;

    QVERIFY(policy.shouldAppend("A.cpp", start, 3, 5, true, WakaTimeCategory::Coding));
    QVERIFY(!policy.shouldAppend("A.cpp", start + 1000, 3, 5, true,
                                 WakaTimeCategory::Coding));
    QVERIFY(policy.shouldAppend("A.cpp", start + 2000, 3, 6, true,
                                WakaTimeCategory::Coding));
    QVERIFY(!policy.shouldAppend("A.cpp", start + 3000, 3, 6, true,
                                 WakaTimeCategory::Coding));
    QVERIFY(!policy.shouldAppend("A.cpp",
                                 start + 3000
                                     + WakaTimeHeartbeatPolicy::WRITE_DEDUPE_INTERVAL_MS - 1,
                                 3, 6, true, WakaTimeCategory::Coding));
    QVERIFY(policy.shouldAppend("A.cpp",
                                start + 3000
                                    + 2 * WakaTimeHeartbeatPolicy::WRITE_DEDUPE_INTERVAL_MS,
                                3, 6, true, WakaTimeCategory::Coding));

    QVERIFY(!policy.shouldAppend("A.cpp",
                                 start + 3000
                                     + 2 * WakaTimeHeartbeatPolicy::WRITE_DEDUPE_INTERVAL_MS
                                     + 1000,
                                 4, 1, false, WakaTimeCategory::Coding));
}

void TestWakaTimeHeartbeat::testCategoryPriorityAndChanges()
{
    QCOMPARE(wakaTimeCategory(false, false), WakaTimeCategory::Coding);
    QCOMPARE(wakaTimeCategory(true, false), WakaTimeCategory::Building);
    QCOMPARE(wakaTimeCategory(false, true), WakaTimeCategory::Debugging);
    QCOMPARE(wakaTimeCategory(true, true), WakaTimeCategory::Debugging);

    WakaTimeHeartbeatPolicy policy;
    QVERIFY(policy.shouldAppend("A.cpp", 1000, 1, 1, false, WakaTimeCategory::Coding));
    QVERIFY(policy.shouldAppend("A.cpp", 2000, 1, 1, false, WakaTimeCategory::Building));
    QVERIFY(policy.shouldAppend("A.cpp", 3000, 1, 1, false, WakaTimeCategory::Debugging));
    QVERIFY(policy.shouldAppend("A.cpp", 4000, 1, 1, false, WakaTimeCategory::Coding));
}

void TestWakaTimeHeartbeat::testCapabilities()
{
    const WakaTimeCliCapabilities full = WakaTimeCliCapabilities::fromHelpOutput(
        "--extra-heartbeats --lines-in-file --alternate-project --project-folder");
    QVERIFY(full.extraHeartbeats);
    QVERIFY(full.linesInFile);
    QVERIFY(full.alternateProject);
    QVERIFY(full.projectFolder);

    const WakaTimeCliCapabilities minimal =
        WakaTimeCliCapabilities::fromHelpOutput("--entity --time --write");
    QVERIFY(!minimal.extraHeartbeats);
    QVERIFY(!minimal.linesInFile);
    QVERIFY(!minimal.alternateProject);
    QVERIFY(!minimal.projectFolder);
}

void TestWakaTimeHeartbeat::testCliSerialization()
{
    WakaTimeCliCapabilities capabilities;
    capabilities.extraHeartbeats = true;
    capabilities.linesInFile = true;
    capabilities.alternateProject = true;
    capabilities.projectFolder = true;

    EditorActivitySnapshot activity;
    activity.hasEditor = true;
    activity.filename = "A.cpp";
    activity.timeMs = 1234567;
    activity.lineNumber = 7;
    activity.cursorPosition = 9;
    activity.linesInFile = 42;
    activity.building = true;
    activity.projectName = "Example";
    activity.projectFolder = "/project";
    const WakaTimeHeartbeat first = makeWakaTimeHeartbeat(activity, true);

    activity.filename = "B.cpp";
    activity.timeMs = 1235567;
    activity.debugging = true;
    const WakaTimeHeartbeat second = makeWakaTimeHeartbeat(activity, false);

    const WakaTimeCliInvocation invocation = makeWakaTimeCliInvocation(
        {first, second}, "/config/wakatime.cfg", "redpanda/test", capabilities);
    QVERIFY(invocation.arguments.contains("--extra-heartbeats"));
    QVERIFY(invocation.arguments.contains("--lines-in-file"));
    QVERIFY(invocation.arguments.contains("--alternate-project"));
    QVERIFY(invocation.arguments.contains("--project-folder"));
    QVERIFY(invocation.arguments.contains("--write"));
    QCOMPARE(invocation.arguments.value(invocation.arguments.indexOf("--entity") + 1),
             QString("A.cpp"));
    QCOMPARE(invocation.arguments.value(invocation.arguments.indexOf("--time") + 1),
             QString("1234.567"));
    QCOMPARE(invocation.arguments.value(invocation.arguments.indexOf("--category") + 1),
             QString("building"));

    const QJsonDocument document = QJsonDocument::fromJson(invocation.standardInput);
    QVERIFY(document.isArray());
    QCOMPARE(document.array().size(), 1);
    const QJsonObject extra = document.array().first().toObject();
    QCOMPARE(extra.value("entity").toString(), QString("B.cpp"));
    QCOMPARE(extra.value("time").toDouble(), 1235.567);
    QCOMPARE(extra.value("lineno").toInt(), 7);
    QCOMPARE(extra.value("cursorpos").toInt(), 9);
    QCOMPARE(extra.value("lines_in_file").toInt(), 42);
    QCOMPARE(extra.value("is_write").toBool(), false);
    QCOMPARE(extra.value("category").toString(), QString("debugging"));
    QCOMPARE(extra.value("alternate_project").toString(), QString("Example"));
    QCOMPARE(extra.value("project_folder").toString(), QString("/project"));
}

void TestWakaTimeHeartbeat::testHeartbeatBuffer()
{
    WakaTimeHeartbeatBuffer buffer;
    WakaTimeHeartbeat heartbeat;
    heartbeat.entity = "A.cpp";

    QVERIFY(buffer.isEmpty());
    buffer.append(heartbeat);
    QVERIFY(buffer.shouldFlushNow(1000, 0));
    QCOMPARE(buffer.nextFlushDelay(1000, 0), 0);

    QList<WakaTimeHeartbeat> firstBatch = buffer.takeBatch(true);
    QCOMPARE(firstBatch.size(), 1);
    QVERIFY(buffer.isEmpty());

    for (int i = 0; i < 3; ++i) {
        heartbeat.entity = QString::number(i);
        buffer.append(heartbeat);
    }
    QVERIFY(!buffer.shouldFlushNow(2000, 1000));
    QCOMPARE(buffer.nextFlushDelay(2000, 1000), 29000);
    QCOMPARE(buffer.takeBatch(false).size(), 1);
    QCOMPARE(buffer.size(), 2);

    QList<WakaTimeHeartbeat> remaining = buffer.takeBatch(true);
    QCOMPARE(remaining.size(), 2);
    buffer.prepend(firstBatch);
    QCOMPARE(buffer.size(), 1);

    for (int i = 0; i < WakaTimeHeartbeatBuffer::MAX_BATCH_SIZE - 1; ++i) {
        buffer.append(heartbeat);
    }
    QVERIFY(buffer.shouldFlushNow(2000, 1000));
    QCOMPARE(buffer.takeBatch(true).size(), WakaTimeHeartbeatBuffer::MAX_BATCH_SIZE);
    QVERIFY(buffer.isEmpty());
}
