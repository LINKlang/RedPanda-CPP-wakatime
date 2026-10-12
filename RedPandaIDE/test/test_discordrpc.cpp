#include "test_discordrpc.h"
#include "src/discordrpcclient.h"
#include "src/discordpresence.h"
#include "src/settings/discordpresencesettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QtEndian>
#include <functional>
#include <memory>
#include <cstring>
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
const QString APP_A = QStringLiteral("1558031759533146213");
const QString APP_B = QStringLiteral("1558031759533146214");
QJsonObject activity(const QString &name) { return {{QStringLiteral("details"), name}}; }

struct Received {
    quint32 opcode;
    QByteArray payload;
    QJsonObject json;
    QPointer<QLocalSocket> socket;
};

class Peer: public QObject {
public:
    Peer()
    {
        const QString unique = QStringLiteral("redpanda-rpc-test-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef Q_OS_WIN
        endpoint = unique;
#else
        endpoint = QDir::temp().filePath(unique);
#endif
        listening = server.listen(endpoint);
        connect(&server, &QLocalServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                QLocalSocket *socket = server.nextPendingConnection();
                sockets.append(socket);
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QLocalSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    while (bytes->size() >= 8) {
                        const auto *header = reinterpret_cast<const uchar *>(bytes->constData());
                        const quint32 opcode = qFromLittleEndian<quint32>(header);
                        const quint32 size = qFromLittleEndian<quint32>(header + 4);
                        if (size > DiscordRpcClient::MAX_PAYLOAD_BYTES || bytes->size() < int(size + 8)) return;
                        const QByteArray payload = bytes->mid(8, int(size));
                        bytes->remove(0, int(size + 8));
                        Received entry{opcode, payload, QJsonDocument::fromJson(payload).object(), socket};
                        messages.append(entry);
                        if (opcode == 0) {
                            if (autoReady) ready(socket);
                        } else if (opcode == 1) {
                            if (onCommand) onCommand(entry);
                            else if (autoAck) acknowledge(entry);
                        }
                    }
                });
            }
        });
    }
    ~Peer() override
    {
        for (const QPointer<QLocalSocket> &socket: sockets)
            if (socket) socket->disconnect(this);
    }
    void ready(QLocalSocket *socket, bool split = false)
    {
        const QByteArray frame = discordRpcFrame(1, QJsonDocument(QJsonObject{
            {QStringLiteral("cmd"), QStringLiteral("DISPATCH")},
            {QStringLiteral("evt"), QStringLiteral("READY")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("v"), 1}}}
        }).toJson(QJsonDocument::Compact));
        if (!split) socket->write(frame);
        else {
            socket->write(frame.left(3));
            QTimer::singleShot(10, socket, [socket, frame] { socket->write(frame.mid(3)); });
        }
    }
    void acknowledge(const Received &entry, const QJsonObject &error = {}, QLocalSocket *overrideSocket = nullptr)
    {
        QLocalSocket *socket = overrideSocket ? overrideSocket : entry.socket.data();
        if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
        QJsonObject reply{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
                          {QStringLiteral("nonce"), entry.json.value(QStringLiteral("nonce"))},
                          {QStringLiteral("evt"), error.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                  : QJsonValue(QStringLiteral("ERROR"))},
                          {QStringLiteral("data"), error}};
        socket->write(discordRpcFrame(1, QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    }
    QList<Received> commands() const
    {
        QList<Received> result;
        for (const Received &entry: messages) if (entry.opcode == 1) result.append(entry);
        return result;
    }
    QList<Received> publications() const
    {
        QList<Received> result;
        for (const Received &entry: commands())
            if (entry.json.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).isObject())
                result.append(entry);
        return result;
    }
    QJsonValue content(const Received &entry) const
    {
        return entry.json.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity"));
    }
    QLocalServer server;
    QString endpoint;
    bool listening {false}, autoReady {true}, autoAck {true};
    QList<QPointer<QLocalSocket>> sockets;
    QList<Received> messages;
    std::function<void(const Received &)> onCommand;
};
}

void TestDiscordRpc::endpointsAndFrames()
{
    const QStringList windows = discordIpcEndpoints(true, {});
    QCOMPARE(windows.size(), 10);
    QCOMPARE(windows.first(), QStringLiteral("discord-ipc-0"));
    QCOMPARE(windows.last(), QStringLiteral("discord-ipc-9"));
    QProcessEnvironment environment;
    environment.insert("XDG_RUNTIME_DIR", "/runtime");
    environment.insert("TMPDIR", "/runtime/");
    environment.insert("TMP", "relative");
    environment.insert("TEMP", "/custom");
    const QStringList unixPaths = discordIpcEndpoints(false, environment);
    QCOMPARE(unixPaths.size(), 30);
    QCOMPARE(unixPaths.at(0), QStringLiteral("/runtime/discord-ipc-0"));
    QCOMPARE(unixPaths.at(9), QStringLiteral("/runtime/discord-ipc-9"));
    QCOMPARE(unixPaths.at(10), QStringLiteral("/custom/discord-ipc-0"));
    QCOMPARE(unixPaths.at(20), QStringLiteral("/tmp/discord-ipc-0"));
    QVERIFY(isDiscordApplicationId(APP_A));
    QVERIFY(!isDiscordApplicationId(QStringLiteral("0")));
    QVERIFY(!isDiscordApplicationId(QStringLiteral("18446744073709551616")));
    QVERIFY(!isDiscordApplicationId(QStringLiteral("１２３")));
    const QByteArray bytes = QStringLiteral("中文😀").toUtf8();
    const QByteArray frame = discordRpcFrame(1, bytes);
    QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(frame.constData() + 4)),
             quint32(bytes.size()));
    QCOMPARE(frame.mid(8), bytes);
    QVERIFY(discordRpcFrame(1, QByteArray(DiscordRpcClient::MAX_PAYLOAD_BYTES + 1, 'x')).isEmpty());
}

void TestDiscordRpc::presenceFormatting()
{
    DiscordPresenceOptions options;
    QCOMPARE(options.applicationId(), APP_A);
    QVERIFY(!options.enabled);
    QVERIFY(options.showCursor && options.showWebsite && !options.privacyMode);
    EditorActivitySnapshot snapshot;
    snapshot.hasEditor = true;
    snapshot.filename = QStringLiteral("/private/folder/main.cpp");
    snapshot.language = QStringLiteral("CppSource");
    snapshot.projectName = QStringLiteral("Secret Demo");
    snapshot.lineNumber = 34;
    snapshot.cursorPosition = 8;
    const qint64 started = 1791500000;
    QJsonObject result = makeDiscordPresence(snapshot, options, false, started);
    QCOMPARE(result.value("details").toString(), QStringLiteral("Editing main.cpp"));
    QCOMPARE(result.value("state").toString(), QStringLiteral("In Secret Demo · Ln 34, Col 8"));
    QCOMPARE(result.value("timestamps").toObject().value("start").toDouble(), double(started));
    QCOMPARE(result.value("assets").toObject().value("large_image").toString(), QStringLiteral("cpp"));
    QCOMPARE(result.value("buttons").toArray().first().toObject().value("url").toString(),
             QStringLiteral("https://github.com/LINKlang/RedPanda-CPP-wakatime"));
    QVERIFY(!QJsonDocument(result).toJson().contains("/private/"));
    snapshot.isNew = true;
    QCOMPARE(makeDiscordPresence(snapshot, options, false, started).value("details").toString(),
             QStringLiteral("Editing an unsaved C++ file"));
    snapshot.building = true;
    QCOMPARE(makeDiscordPresence(snapshot, options, false, started).value("details").toString(),
             QStringLiteral("Building code"));
    snapshot.debugging = true;
    QCOMPARE(makeDiscordPresence(snapshot, options, false, started).value("details").toString(),
             QStringLiteral("Debugging code"));
    result = makeDiscordPresence(snapshot, options, true, started);
    QCOMPARE(result.value("details").toString(), QStringLiteral("Idling"));
    QVERIFY(!result.contains("state") && !result.contains("timestamps"));
    snapshot = EditorActivitySnapshot{};
    result = makeDiscordPresence(snapshot, options, false, started);
    QCOMPARE(result.value("details").toString(), QStringLiteral("Ready to code"));
    QVERIFY(!result.contains("timestamps"));
    QVERIFY(!result.value("assets").toObject().contains("small_image"));
    snapshot.hasEditor = true;
    snapshot.filename = QStringLiteral("/secret/私密.cpp");
    snapshot.projectName = QStringLiteral("Secret Project");
    snapshot.language = QStringLiteral("CCppHeader");
    options.privacyMode = true;
    options.showWebsite = false;
    result = makeDiscordPresence(snapshot, options, false, started);
    QCOMPARE(result.value("details").toString(), QStringLiteral("Editing C++ code"));
    QCOMPARE(result.value("state").toString(), QStringLiteral("Focused on code"));
    QVERIFY(!QJsonDocument(result).toJson().contains("Secret"));
    QVERIFY(!result.contains("buttons"));
    QCOMPARE(snapshot.filename, QStringLiteral("/secret/私密.cpp"));
    const QString longText = QStringLiteral("中文😀").repeated(40);
    const QString clipped = discordPresenceText(longText);
    QVERIFY(clipped.toUtf8().size() <= 128);
    QVERIFY(clipped.endsWith("..."));
    QVERIFY(!clipped.contains(QChar::ReplacementCharacter));
    QVERIFY(!discordPresenceIdle(299999, 5));
    QVERIFY(discordPresenceIdle(300000, 5));
    QVERIFY(!discordPresenceIdle(0, 5));
}

void TestDiscordRpc::settingsPersistence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filename = directory.filePath("settings.ini");
    {
        SettingsPersistor persistor(filename);
        DiscordPresenceSettings settings(&persistor);
        settings.load();
        QCOMPARE(settings.options().applicationId(), APP_A);
        QVERIFY(!settings.options().enabled);
        settings.options().enabled = true;
        settings.options().privacyMode = true;
        settings.options().showCursor = false;
        settings.options().showWebsite = false;
        settings.options().customApplicationId = APP_B;
        settings.options().idleTimeoutMinutes = 10;
        settings.save();
        QCOMPARE(persistor.sync(), QSettings::NoError);
    }
    SettingsPersistor persistor(filename);
    DiscordPresenceSettings settings(&persistor);
    settings.load();
    QCOMPARE(settings.options().applicationId(), APP_B);
    QVERIFY(settings.options().enabled && settings.options().privacyMode);
    QVERIFY(!settings.options().showCursor && !settings.options().showWebsite);
    QCOMPARE(settings.options().idleTimeoutMinutes, 10);
}

void TestDiscordRpc::handshakeAndBinaryPing()
{
    Peer peer;
    QVERIFY(peer.listening);
    peer.autoReady = false;
    DiscordRpcClient client(nullptr, {peer.endpoint + "-missing", peer.endpoint});
    QSignalSpy confirmed(&client, &DiscordRpcClient::activityConfirmed);
    client.setPresence(APP_A, activity("hello"));
    QTRY_COMPARE(peer.messages.size(), 1);
    QVERIFY(!client.isReady());
    QVERIFY(peer.messages.first().json.value("client_id").isString());
    QCOMPARE(peer.messages.first().json.value("client_id").toString(), APP_A);
    QVERIFY(peer.commands().isEmpty());
    peer.ready(peer.messages.first().socket, true);
    QTRY_VERIFY(client.isReady());
    QTRY_COMPARE(confirmed.count(), 1);
    const Received command = peer.commands().first();
    QCOMPARE(command.json.value("args").toObject().value("pid").toDouble(),
             double(QCoreApplication::applicationPid()));
    const QByteArray ping = QByteArray::fromHex("00ff014142");
    command.socket->write(discordRpcFrame(3, ping) + discordRpcFrame(4, QByteArray()));
    QTRY_VERIFY(peer.messages.last().opcode == 4);
    QCOMPARE(peer.messages.last().payload, ping);
    QVERIFY(client.shutdown());
    QVERIFY(peer.content(peer.commands().last()).isNull());
}

void TestDiscordRpc::malformedFrames()
{
    for (const bool tooLarge: {false, true}) {
        Peer peer;
        QVERIFY(peer.listening);
        DiscordRpcClient client(nullptr, {peer.endpoint});
        QSignalSpy errors(&client, &DiscordRpcClient::errorOccurred);
        client.start(APP_A);
        QTRY_VERIFY(client.isReady());
        if (tooLarge) {
            QByteArray header = discordRpcFrame(1, {});
            const quint32 size = qToLittleEndian(quint32(0xffffffff));
            memcpy(header.data() + 4, &size, 4);
            peer.sockets.last()->write(header);
        } else peer.sockets.last()->write(discordRpcFrame(1, QByteArray("{bad")));
        QTRY_VERIFY(!errors.isEmpty());
        QVERIFY(!client.isReady());
        QVERIFY(!client.acknowledgedKnown());
        client.stop();
    }
}

void TestDiscordRpc::ordinaryUpdatesCoalesce()
{
    Peer peer;
    QVERIFY(peer.listening);
    DiscordRpcClient client(nullptr, {peer.endpoint});
    client.setPresence(APP_A, activity("first"));
    QTRY_VERIFY(client.acknowledgedKnown());
    client.setPresence(APP_A, activity("second"));
    client.setPresence(APP_A, activity("latest"));
    QTest::qWait(100);
    QCOMPARE(peer.publications().size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(peer.publications().size(), 2, 6500);
    QCOMPARE(peer.content(peer.publications().last()).toObject().value("details").toString(),
             QStringLiteral("latest"));
    QVERIFY(client.shutdown());
}

void TestDiscordRpc::privacyBoundaryAndLateReply()
{
    Peer peer;
    QVERIFY(peer.listening);
    peer.autoAck = false;
    DiscordRpcClient client(nullptr, {peer.endpoint});
    QSignalSpy confirmations(&client, &DiscordRpcClient::activityConfirmed);
    client.setPresence(APP_A, activity("private.cpp"));
    QTRY_COMPARE(peer.publications().size(), 1);
    const Received original = peer.publications().first();
    client.setPresence(APP_A, activity("Editing C++ code"), true);
    peer.acknowledge(original);
    QTRY_COMPARE(peer.commands().size(), 2);
    QVERIFY(peer.content(peer.commands().last()).isNull());
    QCOMPARE(confirmations.count(), 0);
    peer.acknowledge(peer.commands().last());
    QTRY_COMPARE(peer.publications().size(), 2);
    const Received anonymous = peer.publications().last();
    QVERIFY(!anonymous.payload.contains("private.cpp"));
    peer.acknowledge(original, {}, anonymous.socket);
    QTest::qWait(20);
    QVERIFY(client.acknowledgedActivity().isNull());
    peer.acknowledge(anonymous);
    QTRY_VERIFY(client.acknowledgedActivity().isObject());
    QCOMPARE(client.acknowledgedActivity().toObject().value("details").toString(),
             QStringLiteral("Editing C++ code"));
    peer.autoAck = true;
    QVERIFY(client.shutdown());
}

void TestDiscordRpc::privacyTimeoutReconnect()
{
    Peer peer;
    QVERIFY(peer.listening);
    peer.onCommand = [&](const Received &entry) {
        if (peer.content(entry).isNull() || !entry.payload.contains("private.cpp"))
            peer.acknowledge(entry);
    };
    DiscordRpcClient client(nullptr, {peer.endpoint});
    client.setPresence(APP_A, activity("private.cpp"));
    QTRY_COMPARE(peer.publications().size(), 1);
    client.setPresence(APP_A, activity("anonymous"), true);
    QTRY_VERIFY_WITH_TIMEOUT(peer.sockets.size() >= 2, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(client.acknowledgedKnown() && client.acknowledgedActivity().isObject(), 1500);
    int privateCount = 0;
    for (const Received &entry: peer.publications())
        if (entry.payload.contains("private.cpp")) ++privateCount;
    QCOMPARE(privateCount, 1);
    QCOMPARE(client.acknowledgedActivity().toObject().value("details").toString(), QStringLiteral("anonymous"));
    QVERIFY(client.shutdown());
}

void TestDiscordRpc::applicationSwitch()
{
    Peer peer;
    QVERIFY(peer.listening);
    DiscordRpcClient client(nullptr, {peer.endpoint});
    client.setPresence(APP_A, activity("old app"));
    QTRY_VERIFY(client.acknowledgedKnown());
    client.setPresence(APP_B, activity("new app"));
    QTRY_VERIFY_WITH_TIMEOUT(peer.sockets.size() >= 2, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(client.acknowledgedKnown() && client.acknowledgedActivity().isObject(), 1500);
    QStringList ids;
    for (const Received &entry: peer.messages)
        if (entry.opcode == 0) ids.append(entry.json.value("client_id").toString());
    QCOMPARE(ids, QStringList({APP_A, APP_B}));
    QCOMPARE(client.acknowledgedActivity().toObject().value("details").toString(), QStringLiteral("new app"));
    QVERIFY(client.shutdown());
}

void TestDiscordRpc::privacyAckAtDeadline()
{
    for (const bool alreadyQueued: {true, false}) {
        Peer peer;
        QVERIFY(peer.listening);
        peer.autoAck = false;
        DiscordRpcClient client(nullptr, {peer.endpoint});
        QSignalSpy confirmations(&client, &DiscordRpcClient::activityConfirmed);
        client.setPresence(APP_A, activity("private.cpp"));
        QTRY_COMPARE(peer.publications().size(), 1);
        const Received original = peer.publications().first();
        client.setPresence(APP_A, activity("anonymous"), true);
        if (alreadyQueued) {
            peer.acknowledge(original);
            original.socket->flush();
        } else {
            QTimer::singleShot(500, &peer, [&peer, original] { peer.acknowledge(original); });
        }
        // Both the old ACK and the policy deadline are ready when the loop resumes.
        QTest::qSleep(510);
        QTRY_VERIFY_WITH_TIMEOUT(peer.commands().size() >= 2, 1500);
        const Received clear = peer.commands().last();
        QVERIFY(peer.content(clear).isNull());
        QCOMPARE(peer.publications().size(), 1);
        QCOMPARE(confirmations.count(), 0);
        // A duplicate old reply on the current socket cannot acknowledge its clear.
        peer.acknowledge(original, {}, clear.socket);
        QTest::qWait(20);
        QVERIFY(!client.acknowledgedKnown());
        peer.acknowledge(clear);
        QTRY_COMPARE_WITH_TIMEOUT(peer.publications().size(), 2, 1500);
        const Received anonymous = peer.publications().last();
        QCOMPARE(peer.content(anonymous).toObject().value("details").toString(),
                 QStringLiteral("anonymous"));
        peer.acknowledge(anonymous);
        QTRY_VERIFY(client.acknowledgedKnown() && client.acknowledgedActivity().isObject());
        peer.autoAck = true;
        QVERIFY(client.shutdown());
    }
}

void TestDiscordRpc::busyEndpointAndHandleRelease()
{
#ifdef Q_OS_WIN
    Peer peer;
    QVERIFY(peer.listening);
    const QString busyEndpoint = peer.endpoint + QStringLiteral("-busy");
    const QString pipeName = QStringLiteral("\\\\.\\pipe\\") + busyEndpoint;
    struct Handles {
        HANDLE server {INVALID_HANDLE_VALUE};
        HANDLE occupant {INVALID_HANDLE_VALUE};
        ~Handles()
        {
            if (occupant != INVALID_HANDLE_VALUE) CloseHandle(occupant);
            if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
        }
    } handles;
    handles.server = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(pipeName.utf16()),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 5000, nullptr);
    QVERIFY(handles.server != INVALID_HANDLE_VALUE);
    handles.occupant = CreateFileW(reinterpret_cast<LPCWSTR>(pipeName.utf16()),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    QVERIFY(handles.occupant != INVALID_HANDLE_VALUE);
    // Connecting the occupant first avoids a blocking server-side wait.
    const BOOL connected = ConnectNamedPipe(handles.server, nullptr);
    QVERIFY(connected || GetLastError() == ERROR_PIPE_CONNECTED);
    const HANDLE probe = CreateFileW(reinterpret_cast<LPCWSTR>(pipeName.utf16()),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    const DWORD error = GetLastError();
    if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
    QVERIFY(probe == INVALID_HANDLE_VALUE);
    QCOMPARE(error, DWORD(ERROR_PIPE_BUSY));
    {
        DiscordRpcClient client(nullptr, {busyEndpoint, peer.endpoint});
        QElapsedTimer elapsed;
        elapsed.start();
        client.start(APP_A);
        QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 1000);
        QVERIFY(elapsed.elapsed() < 1000);
        QCOMPARE(peer.messages.size(), 1);
        QVERIFY(peer.commands().isEmpty());
    }
    // Destroying the client must close the native handle adopted by Qt.
    QTRY_VERIFY(peer.sockets.first()->state() == QLocalSocket::UnconnectedState);
#else
    QSKIP("Windows named-pipe busy handling is platform-specific.");
#endif
}

void TestDiscordRpc::rateLimitedClear()
{
    Peer peer;
    QVERIFY(peer.listening);
    int clears = 0;
    peer.onCommand = [&](const Received &entry) {
        if (peer.content(entry).isNull() && ++clears == 1) {
            peer.acknowledge(entry, {{QStringLiteral("code"), 429},
                {QStringLiteral("message"), QStringLiteral("Rate limited")},
                {QStringLiteral("retry_after"), 0.1}});
        } else peer.acknowledge(entry);
    };
    DiscordRpcClient client(nullptr, {peer.endpoint});
    client.setPresence(APP_A, activity("private"));
    QTRY_VERIFY(client.acknowledgedKnown());
    client.setPresence(APP_A, activity("anonymous"), true);
    QTRY_COMPARE_WITH_TIMEOUT(clears, 2, 1500);
    QTRY_COMPARE_WITH_TIMEOUT(client.acknowledgedActivity().toObject().value("details").toString(),
                             QStringLiteral("anonymous"), 1500);
    QVERIFY(client.shutdown());
}

void TestDiscordRpc::shutdownDeadline()
{
    Peer peer;
    QVERIFY(peer.listening);
    peer.autoAck = false;
    DiscordRpcClient client(nullptr, {peer.endpoint});
    client.setPresence(APP_A, activity("unconfirmed"));
    QTRY_COMPARE(peer.publications().size(), 1);
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!client.shutdown(10000));
    QVERIFY(elapsed.elapsed() < 2500);
    QVERIFY(!client.isRunning());
    QVERIFY(!client.acknowledgedKnown());
    const int count = peer.messages.size();
    QTest::qWait(100);
    QCOMPARE(peer.messages.size(), count);
}

void TestDiscordRpc::reentrantPolicyInvalidatesUnsubmittedFrame()
{
    Peer peer;
    QVERIFY(peer.listening);
    DiscordRpcClient client(nullptr, {peer.endpoint});
    bool replaced = false;
    connect(&client, &DiscordRpcClient::statusChanged, &client, [&](const QString &status) {
        if (!replaced && status == QStringLiteral("Submitting activity")) {
            replaced = true;
            client.setPresence(APP_A, activity("anonymous"), true);
        }
    });
    client.setPresence(APP_A, activity("private.cpp"));
    QTRY_VERIFY(client.acknowledgedKnown() && client.acknowledgedActivity().isObject());
    for (const Received &entry: peer.commands()) QVERIFY(!entry.payload.contains("private.cpp"));
    QVERIFY(client.shutdown());
}

void TestDiscordRpc::shutdownInterruptedByApplicationQuit()
{
    Peer peer;
    QVERIFY(peer.listening);
    DiscordRpcClient client(nullptr, {peer.endpoint});
    client.setPresence(APP_A, activity("confirmed before quit"));
    QTRY_VERIFY(client.acknowledgedKnown());
    peer.autoAck = false;
    QSignalSpy stopped(&client, &DiscordRpcClient::stopped);
    QSignalSpy cleared(&client, &DiscordRpcClient::clearFinished);
    bool confirmed = true;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer::singleShot(0, &client, [&] {
        QTimer::singleShot(50, QCoreApplication::instance(), &QCoreApplication::quit);
        confirmed = client.shutdown();
    });
    // A real application event loop makes quit interrupt all nested event loops.
    QCoreApplication::exec();
    QVERIFY(elapsed.elapsed() < 1000);
    QVERIFY(!confirmed && !client.acknowledgedKnown());
    QCOMPARE(stopped.count(), 1);
    QCOMPARE(stopped.first().first().toBool(), false);
    QCOMPARE(cleared.count(), 1);
    QCOMPARE(cleared.first().first().toBool(), false);
    QVERIFY(!client.lastError().isEmpty());
    const int commands = peer.commands().size();
    QTest::qWait(100);
    QCOMPARE(peer.commands().size(), commands);
}
