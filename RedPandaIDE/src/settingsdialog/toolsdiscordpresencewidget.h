#ifndef TOOLSDISCORDPRESENCEWIDGET_H
#define TOOLSDISCORDPRESENCEWIDGET_H

#include "settingswidget.h"
#include <QPointer>
class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;
class DiscordRpcClient;
class DiscordPresenceSettings;

class ToolsDiscordPresenceWidget: public SettingsWidget {
    Q_OBJECT
public:
    ToolsDiscordPresenceWidget(const QString &name, const QString &group,
                               IconsManager *iconsManager, DiscordPresenceSettings *settings,
                               DiscordRpcClient *client, QWidget *parent = nullptr);
signals:
    void settingsApplied();
protected:
    void doLoad() override;
    void doSave() override;
private:
    void testConnection();
    QCheckBox *mEnabled;
    QCheckBox *mCursor;
    QCheckBox *mPrivacy;
    QCheckBox *mWebsite;
    QSpinBox *mIdleMinutes;
    QLineEdit *mApplicationId;
    QLabel *mStatus;
    QPushButton *mTest;
    QPointer<DiscordRpcClient> mTestClient;
    QPointer<DiscordRpcClient> mActiveClient;
    DiscordPresenceSettings *mSettings;
};
#endif
