#ifndef TEST_DISCORDRPC_H
#define TEST_DISCORDRPC_H
#include <QObject>
class TestDiscordRpc: public QObject {
    Q_OBJECT
private slots:
    void endpointsAndFrames();
    void presenceFormatting();
    void settingsPersistence();
    void handshakeAndBinaryPing();
    void malformedFrames();
    void ordinaryUpdatesCoalesce();
    void privacyBoundaryAndLateReply();
    void privacyTimeoutReconnect();
    void privacyAckAtDeadline();
    void busyEndpointAndHandleRelease();
    void applicationSwitch();
    void rateLimitedClear();
    void shutdownDeadline();
    void shutdownInterruptedByApplicationQuit();
    void reentrantPolicyInvalidatesUnsubmittedFrame();
};
#endif
