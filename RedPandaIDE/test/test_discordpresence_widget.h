#ifndef TEST_DISCORDPRESENCE_WIDGET_H
#define TEST_DISCORDPRESENCE_WIDGET_H
#include <QObject>
class TestDiscordPresenceWidget: public QObject {
    Q_OBJECT
private slots:
    void draftAndApply();
};
#endif
