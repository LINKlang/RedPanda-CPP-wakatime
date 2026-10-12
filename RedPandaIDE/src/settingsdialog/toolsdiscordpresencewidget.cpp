#include "toolsdiscordpresencewidget.h"
#include "../discordrpcclient.h"
#include "../settings/discordpresencesettings.h"
#include "../utils.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

ToolsDiscordPresenceWidget::ToolsDiscordPresenceWidget(const QString &name, const QString &group,
                                                       IconsManager *iconsManager,
                                                       DiscordPresenceSettings *settings,
                                                       DiscordRpcClient *client, QWidget *parent):
    SettingsWidget(name, group, iconsManager, parent),
    mEnabled(new QCheckBox(tr("Enable Discord Rich Presence"), this)),
    mCursor(new QCheckBox(tr("Show line and column"), this)),
    mPrivacy(new QCheckBox(tr("Hide file and project names"), this)),
    mWebsite(new QCheckBox(tr("Show Learn More button"), this)),
    mIdleMinutes(new QSpinBox(this)), mApplicationId(new QLineEdit(this)),
    mStatus(new QLabel(this)), mTest(new QPushButton(tr("Test connection"), this)),
    mActiveClient(client), mSettings(settings)
{
    mEnabled->setObjectName(QStringLiteral("discordEnabled"));
    mCursor->setObjectName(QStringLiteral("discordCursor"));
    mPrivacy->setObjectName(QStringLiteral("discordPrivacy"));
    mWebsite->setObjectName(QStringLiteral("discordWebsite"));
    mIdleMinutes->setObjectName(QStringLiteral("discordIdleMinutes"));
    mApplicationId->setObjectName(QStringLiteral("discordApplicationId"));
    mIdleMinutes->setRange(1, 60);
    mIdleMinutes->setSuffix(tr(" min"));
    mApplicationId->setPlaceholderText(DiscordPresenceOptions{}.applicationId());
    mApplicationId->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,20}")), mApplicationId));
    mStatus->setWordWrap(true);
    QFormLayout *form = new QFormLayout;
    form->addRow(mEnabled);
    form->addRow(mCursor);
    form->addRow(mPrivacy);
    form->addRow(mWebsite);
    form->addRow(tr("Idle after no input:"), mIdleMinutes);
    form->addRow(tr("Custom Application ID:"), mApplicationId);
    QLabel *hint = new QLabel(tr("Leave the ID empty to use the RedPanda C++ application. "
                                "Card text is in English. Full paths are never shared; "
                                "privacy mode hides file and project names."), this);
    hint->setWordWrap(true);
    form->addRow(hint);
    QGroupBox *groupBox = new QGroupBox(tr("Settings"), this);
    groupBox->setLayout(form);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(groupBox);
    layout->addWidget(mTest);
    layout->addWidget(mStatus);
    layout->addStretch();
    connect(mTest, &QPushButton::clicked, this, &ToolsDiscordPresenceWidget::testConnection);
    if (client) {
        connect(client, &DiscordRpcClient::statusChanged, this, [this](const QString &status) {
            if (!mTestClient) mStatus->setText(status);
        });
        connect(client, &DiscordRpcClient::errorOccurred, this, [this](const QString &message) {
            if (!mTestClient) mStatus->setText(message);
        });
    }
}

void ToolsDiscordPresenceWidget::doLoad()
{
    const DiscordPresenceOptions &options = mSettings->options();
    mEnabled->setChecked(options.enabled);
    mCursor->setChecked(options.showCursor);
    mPrivacy->setChecked(options.privacyMode);
    mWebsite->setChecked(options.showWebsite);
    mIdleMinutes->setValue(options.idleTimeoutMinutes);
    mApplicationId->setText(options.customApplicationId);
    if (mActiveClient) mStatus->setText(mActiveClient->status());
}

void ToolsDiscordPresenceWidget::doSave()
{
    DiscordPresenceOptions options;
    options.enabled = mEnabled->isChecked();
    options.showCursor = mCursor->isChecked();
    options.privacyMode = mPrivacy->isChecked();
    options.showWebsite = mWebsite->isChecked();
    options.idleTimeoutMinutes = mIdleMinutes->value();
    options.customApplicationId = mApplicationId->text().trimmed();
    if (!isDiscordApplicationId(options.applicationId()))
        throw FileError(tr("Invalid Discord Application ID."));
    mSettings->options() = options;
    mSettings->save();
    emit settingsApplied();
}

void ToolsDiscordPresenceWidget::testConnection()
{
    if (mTestClient) return;
    DiscordPresenceOptions options;
    options.customApplicationId = mApplicationId->text().trimmed();
    const QString id = options.applicationId();
    if (!isDiscordApplicationId(id)) {
        mStatus->setText(tr("Invalid Discord Application ID."));
        return;
    }
    DiscordRpcClient *active = mActiveClient.data();
    if (active && active->isRunning() && id == mSettings->options().applicationId()) {
        mStatus->setText(active->status() + (active->lastError().isEmpty()
            ? QString() : QStringLiteral("\n") + active->lastError()));
        return;
    }
    DiscordRpcClient *test = new DiscordRpcClient(this);
    mTestClient = test;
    mTest->setEnabled(false);
    mStatus->setText(tr("Connecting"));
    connect(test, &DiscordRpcClient::ready, this, [this, test] {
        mStatus->setText(tr("RPC handshake confirmed. No activity was published."));
        test->stop();
    });
    connect(test, &DiscordRpcClient::errorOccurred, this, [this](const QString &message) {
        mStatus->setText(message);
    });
    connect(test, &DiscordRpcClient::stopped, this, [this, test] {
        mTestClient.clear();
        mTest->setEnabled(true);
        test->deleteLater();
    });
    QTimer::singleShot(5000, test, [this, test] {
        if (!test->isReady()) mStatus->setText(tr("Discord did not complete the handshake."));
        test->stop();
    });
    test->start(id);
}
