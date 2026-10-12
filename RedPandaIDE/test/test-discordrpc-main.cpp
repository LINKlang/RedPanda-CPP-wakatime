#include "src/discordrpcclient.h"
#include "src/discordpresence.h"
#include <QCoreApplication>
#include <QApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>
#include <QTest>
#include <cstring>
#include <memory>
#include "test_discordrpc.h"

int main(int argc, char *argv[])
{
    bool live = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--live") == 0) live = true;
    std::unique_ptr<QCoreApplication> app;
    std::unique_ptr<QWidget> window;
    if (!live) {
        app = std::make_unique<QCoreApplication>(argc, argv);
        TestDiscordRpc test;
        return QTest::qExec(&test, argc, argv);
    }
    app = std::make_unique<QApplication>(argc, argv);
    static_cast<QApplication *>(app.get())->setQuitOnLastWindowClosed(false);
    window = std::make_unique<QWidget>();
    window->setWindowTitle(QStringLiteral("RedPanda C++ RPC verification"));
    QVBoxLayout *layout = new QVBoxLayout(window.get());
    QLabel *hint = new QLabel(QStringLiteral("Publishes neutral test text with artwork and Learn More.\n"
        "The activity will be cleared automatically after 60 seconds."), window.get());
    QLabel *statusLabel = new QLabel(QStringLiteral("Connecting"), window.get());
    QPushButton *stop = new QPushButton(QStringLiteral("Clear and close"), window.get());
    layout->addWidget(hint);
    layout->addWidget(statusLabel);
    layout->addWidget(stop);
    window->resize(480, 160);
    window->show();
    DiscordRpcClient client;
    QObject::connect(&client, &DiscordRpcClient::statusChanged, statusLabel, &QLabel::setText);
    QObject::connect(stop, &QPushButton::clicked, &client, &DiscordRpcClient::stop);
    QObject::connect(static_cast<QApplication *>(app.get()), &QApplication::lastWindowClosed,
                     &client, &DiscordRpcClient::stop);
    qInfo() << "Live probe PID:" << QCoreApplication::applicationPid();
    bool published = false;
    QObject::connect(&client, &DiscordRpcClient::statusChanged, [](const QString &status) {
        qInfo().noquote() << status;
    });
    QObject::connect(&client, &DiscordRpcClient::errorOccurred, [](const QString &error) {
        qWarning().noquote() << error;
    });
    QObject::connect(&client, &DiscordRpcClient::ready, [&] {
        if (published) return;
        EditorActivitySnapshot snapshot;
        snapshot.hasEditor = true;
        snapshot.filename = QStringLiteral("RPC check.cpp");
        snapshot.language = QStringLiteral("CppSource");
        QJsonObject activity = makeDiscordPresence(snapshot, DiscordPresenceOptions{},
                                                   false, QDateTime::currentSecsSinceEpoch());
        activity.insert(QStringLiteral("details"), QStringLiteral("Testing RedPanda C++ RPC"));
        activity.insert(QStringLiteral("state"), QStringLiteral("Connection check"));
        client.setPresence(QStringLiteral("1558031759533146213"), activity);
    });
    QObject::connect(&client, &DiscordRpcClient::activityConfirmed,
                     [&](const QJsonValue &activity, quint64) {
        if (activity.isObject() && !published) {
            published = true;
            qInfo() << "SET_ACTIVITY acknowledged; neutral presence with artwork and Learn More will be cleared in 60 seconds.";
            QTimer::singleShot(60000, &client, &DiscordRpcClient::stop);
        }
    });
    QObject::connect(&client, &DiscordRpcClient::stopped, [&](bool confirmed) {
        app->exit(published && confirmed ? 0 : 1);
    });
    QTimer::singleShot(90000, &client, [&] {
        qWarning() << "Live probe deadline reached.";
        client.stop();
    });
    client.start(QStringLiteral("1558031759533146213"));
    return app->exec();
}
