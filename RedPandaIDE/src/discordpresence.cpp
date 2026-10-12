#include "discordpresence.h"

#include <QFileInfo>
#include <QJsonArray>

QString DiscordPresenceOptions::applicationId() const
{
    const QString custom = customApplicationId.trimmed();
    return custom.isEmpty() ? QStringLiteral("1558031759533146213") : custom;
}

QString discordPresenceText(const QString &text)
{
    if (text.toUtf8().size() <= 128) return text;
    QString result;
    int bytes = 0;
    for (int i = 0; i < text.size(); ++i) {
        QString character(text.at(i));
        if (text.at(i).isHighSurrogate() && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate())
            character.append(text.at(++i));
        const int count = character.toUtf8().size();
        if (bytes + count > 125) break;
        result.append(character);
        bytes += count;
    }
    return result + QStringLiteral("...");
}

bool discordPresenceIdle(qint64 inactiveMs, int timeoutMinutes)
{
    return inactiveMs >= qint64(qBound(1, timeoutMinutes, 60)) * 60 * 1000;
}

QJsonObject makeDiscordPresence(const EditorActivitySnapshot &activity,
                               const DiscordPresenceOptions &options,
                               bool idle, qint64 sessionStartedAtSeconds)
{
    QString language = QStringLiteral("text");
    QString image = QStringLiteral("text");
    if (activity.language == QStringLiteral("CSource")) {
        language = QStringLiteral("C");
        image = QStringLiteral("c");
    } else if (activity.language == QStringLiteral("CppSource")
               || activity.language == QStringLiteral("CCppHeader")) {
        language = QStringLiteral("C++");
        image = QStringLiteral("cpp");
    }
    QString details;
    QString state = activity.projectName.isEmpty()
        ? QStringLiteral("Standalone file") : QStringLiteral("In %1").arg(activity.projectName);
    QString largeText = language;
    QString smallImage = QStringLiteral("redpanda");
    QString smallText = QStringLiteral("RedPanda C++");
    bool elapsed = true;
    bool cursor = false;
    if (idle) {
        details = QStringLiteral("Idling");
        state.clear();
        image = QStringLiteral("redpanda");
        largeText = QStringLiteral("RedPanda C++");
        smallImage = QStringLiteral("idle");
        smallText = QStringLiteral("Idling");
        elapsed = false;
    } else if (activity.debugging) {
        details = QStringLiteral("Debugging code");
        image = QStringLiteral("debugging");
        largeText = QStringLiteral("Debugging");
    } else if (activity.building) {
        details = QStringLiteral("Building code");
        image = QStringLiteral("building");
        largeText = QStringLiteral("Building");
    } else if (!activity.hasEditor) {
        details = QStringLiteral("Ready to code");
        state = QStringLiteral("No file open");
        image = QStringLiteral("redpanda");
        largeText = QStringLiteral("RedPanda C++");
        smallImage.clear();
        smallText.clear();
        elapsed = false;
    } else if (activity.isNew) {
        details = QStringLiteral("Editing an unsaved %1 file").arg(language);
        cursor = true;
    } else {
        details = QStringLiteral("Editing %1").arg(QFileInfo(activity.filename).fileName());
        cursor = true;
    }
    if (options.privacyMode && !idle && activity.hasEditor) {
        const QString code = language == QStringLiteral("text")
            ? QStringLiteral("code") : language + QStringLiteral(" code");
        details = activity.debugging ? QStringLiteral("Debugging %1").arg(code)
            : activity.building ? QStringLiteral("Building %1").arg(code)
            : QStringLiteral("Editing %1").arg(code);
        state = QStringLiteral("Focused on code");
    } else if (options.privacyMode && (activity.building || activity.debugging) && !idle) {
        state = QStringLiteral("Focused on code");
    }
    if (options.showCursor && cursor && activity.lineNumber > 0 && activity.cursorPosition > 0)
        state += QStringLiteral(" · Ln %1, Col %2").arg(activity.lineNumber).arg(activity.cursorPosition);
    QJsonObject assets{{QStringLiteral("large_image"), image},
                       {QStringLiteral("large_text"), discordPresenceText(largeText)}};
    if (!smallImage.isEmpty()) {
        assets.insert(QStringLiteral("small_image"), smallImage);
        assets.insert(QStringLiteral("small_text"), discordPresenceText(smallText));
    }
    QJsonObject result{{QStringLiteral("type"), 0},
                       {QStringLiteral("details"), discordPresenceText(details)},
                       {QStringLiteral("assets"), assets}};
    if (!state.isEmpty()) result.insert(QStringLiteral("state"), discordPresenceText(state));
    if (elapsed)
        result.insert(QStringLiteral("timestamps"), QJsonObject{
            {QStringLiteral("start"), static_cast<double>(sessionStartedAtSeconds)}});
    if (options.showWebsite)
        result.insert(QStringLiteral("buttons"), QJsonArray{QJsonObject{
            {QStringLiteral("label"), QStringLiteral("Learn More")},
            {QStringLiteral("url"), QStringLiteral("https://github.com/LINKlang/RedPanda-CPP-wakatime")}}});
    return result;
}
