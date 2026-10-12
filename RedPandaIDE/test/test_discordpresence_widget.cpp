#include "test_discordpresence_widget.h"
#include "src/settingsdialog/toolsdiscordpresencewidget.h"
#include "src/settings/discordpresencesettings.h"
#include "src/settings/dirsettings.h"
#include "src/discordrpcclient.h"
#include "src/iconsmanager.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

void TestDiscordPresenceWidget::draftAndApply()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsPersistor persistor(directory.filePath("settings.ini"));
    DirSettings directories(&persistor);
    directories.load();
    DiscordPresenceSettings settings(&persistor);
    settings.load();
    IconsManager icons(&directories, QStringLiteral("en"));
    DiscordRpcClient client(nullptr, {QStringLiteral("unused-test-endpoint")});
    ToolsDiscordPresenceWidget widget(QStringLiteral("Discord Rich Presence"),
                                     QStringLiteral("Tools"), &icons, &settings, &client);
    widget.init();
    widget.load();
    QSignalSpy applied(&widget, &ToolsDiscordPresenceWidget::settingsApplied);
    QCheckBox *enabled = widget.findChild<QCheckBox *>("discordEnabled");
    QCheckBox *privacy = widget.findChild<QCheckBox *>("discordPrivacy");
    QCheckBox *cursor = widget.findChild<QCheckBox *>("discordCursor");
    QCheckBox *website = widget.findChild<QCheckBox *>("discordWebsite");
    QLineEdit *id = widget.findChild<QLineEdit *>("discordApplicationId");
    QSpinBox *idle = widget.findChild<QSpinBox *>("discordIdleMinutes");
    QVERIFY(enabled && privacy && cursor && website && id && idle);
    QVERIFY(!enabled->isChecked() && !privacy->isChecked());
    QVERIFY(cursor->isChecked() && website->isChecked());
    QCOMPARE(id->placeholderText(), QStringLiteral("1558031759533146213"));
    QCOMPARE(idle->value(), 5);
    enabled->setChecked(true);
    privacy->setChecked(true);
    cursor->setChecked(false);
    idle->setValue(10);
    QVERIFY(!settings.options().enabled && !settings.options().privacyMode);
    QCOMPARE(applied.count(), 0);
    widget.load(); // Discarding a draft must not alter persisted settings.
    QVERIFY(!enabled->isChecked() && !privacy->isChecked());
    enabled->setChecked(true);
    privacy->setChecked(true);
    cursor->setChecked(false);
    idle->setValue(10);
    widget.save();
    QCOMPARE(applied.count(), 1);
    QVERIFY(settings.options().enabled && settings.options().privacyMode);
    QVERIFY(!settings.options().showCursor);
    QCOMPARE(settings.options().idleTimeoutMinutes, 10);
    QVERIFY(!client.isRunning()); // The widget emits apply; it does not publish directly.
    widget.load();
    QVERIFY(privacy->isChecked());
    widget.resize(560, 460);
    widget.ensurePolished();
    QVERIFY(widget.grab().save(QStringLiteral("discord-settings-preview.png")));
}
