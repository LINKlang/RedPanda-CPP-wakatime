#include "wakatimesettings.h"

#include "../settings.h"
#include "../utils.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

const QString WakaTimeSettings::DEFAULT_API_URL = QStringLiteral("https://api.wakatime.com/api/v1");

static QString sanitizeWakaTimeConfigValue(QString value)
{
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    return value;
}

WakaTimeSettings::WakaTimeSettings(SettingsPersistor *persistor):
    BaseSettings{persistor, SETTING_WAKATIME},
    mEnabled(false),
    mApiUrl(DEFAULT_API_URL),
    mApiUrlsEnabled(false),
    mDebugEnabled(false)
{
}

bool WakaTimeSettings::enabled() const
{
    return mEnabled;
}

void WakaTimeSettings::setEnabled(bool newEnabled)
{
    mEnabled = newEnabled;
}

const QString &WakaTimeSettings::cliPath() const
{
    return mCliPath;
}

void WakaTimeSettings::setCliPath(const QString &newCliPath)
{
    mCliPath = newCliPath;
}

const QString &WakaTimeSettings::apiKey() const
{
    return mApiKey;
}

void WakaTimeSettings::setApiKey(const QString &newApiKey)
{
    mApiKey = newApiKey;
}

const QString &WakaTimeSettings::apiUrl() const
{
    return mApiUrl;
}

void WakaTimeSettings::setApiUrl(const QString &newApiUrl)
{
    mApiUrl = newApiUrl.trimmed().isEmpty() ? DEFAULT_API_URL : newApiUrl.trimmed();
}

bool WakaTimeSettings::apiUrlsEnabled() const
{
    return mApiUrlsEnabled;
}

void WakaTimeSettings::setApiUrlsEnabled(bool newApiUrlsEnabled)
{
    mApiUrlsEnabled = newApiUrlsEnabled;
}

const QList<WakaTimeApiUrlRule> &WakaTimeSettings::apiUrlRules() const
{
    return mApiUrlRules;
}

void WakaTimeSettings::setApiUrlRules(const QList<WakaTimeApiUrlRule> &newApiUrlRules)
{
    mApiUrlRules = newApiUrlRules;
}

bool WakaTimeSettings::debugEnabled() const
{
    return mDebugEnabled;
}

void WakaTimeSettings::setDebugEnabled(bool newDebugEnabled)
{
    mDebugEnabled = newDebugEnabled;
}

QString WakaTimeSettings::managedConfigFileName() const
{
    return getFilePath(pSettings->dirs().config(), QStringLiteral("wakatime.cfg"));
}

void WakaTimeSettings::saveManagedConfig() const
{
    QFileInfo configInfo(managedConfigFileName());
    QDir().mkpath(configInfo.absolutePath());

    QFile file(configInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw FileError(QObject::tr("Can't open file '%1' for write.").arg(configInfo.absoluteFilePath()));
    }

    const QByteArray content = managedConfigText().toUtf8();
    if (file.write(content) != content.size()) {
        throw FileError(QObject::tr("Can't write file '%1'.").arg(configInfo.absoluteFilePath()));
    }
}

void WakaTimeSettings::doSave()
{
    saveValue("enabled", mEnabled);
    saveValue("cli_path", mCliPath);
    saveValue("api_key", mApiKey);
    saveValue("api_url", mApiUrl);
    saveValue("api_urls_enabled", mApiUrlsEnabled);
    saveValue("debug_enabled", mDebugEnabled);

    QJsonArray rules;
    for (const WakaTimeApiUrlRule &rule: mApiUrlRules) {
        QJsonObject ruleObject;
        ruleObject.insert(QStringLiteral("pattern"), rule.pattern);
        ruleObject.insert(QStringLiteral("api_url"), rule.apiUrl);
        ruleObject.insert(QStringLiteral("api_key"), rule.apiKey);
        rules.append(ruleObject);
    }
    saveValue("api_url_rules", QString::fromUtf8(QJsonDocument(rules).toJson(QJsonDocument::Compact)));
    saveManagedConfig();
}

void WakaTimeSettings::doLoad()
{
    mEnabled = boolValue("enabled", false);
    mCliPath = stringValue("cli_path", "");
    mApiKey = stringValue("api_key", "");
    setApiUrl(stringValue("api_url", DEFAULT_API_URL));
    mApiUrlsEnabled = boolValue("api_urls_enabled", false);
    mDebugEnabled = boolValue("debug_enabled", false);

    mApiUrlRules.clear();
    const QJsonDocument rulesDoc = QJsonDocument::fromJson(stringValue("api_url_rules", "[]").toUtf8());
    if (!rulesDoc.isArray()) {
        return;
    }

    for (const QJsonValue &value: rulesDoc.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject ruleObject = value.toObject();
        WakaTimeApiUrlRule rule;
        rule.pattern = ruleObject.value(QStringLiteral("pattern")).toString();
        rule.apiUrl = ruleObject.value(QStringLiteral("api_url")).toString();
        rule.apiKey = ruleObject.value(QStringLiteral("api_key")).toString();
        mApiUrlRules.append(rule);
    }
}

QString WakaTimeSettings::managedConfigText() const
{
    QString result;
    QTextStream stream(&result);
    stream << "[settings]\n";
    stream << "sync_ai_disabled = true\n";
    if (!mApiUrlsEnabled) {
        stream << "api_key = " << sanitizeWakaTimeConfigValue(mApiKey) << "\n";
        stream << "api_url = " << sanitizeWakaTimeConfigValue(mApiUrl) << "\n";
    } else {
        stream << "\n";
        stream << "[api_urls]\n";
        for (const WakaTimeApiUrlRule &rule: mApiUrlRules) {
            const QString pattern = sanitizeWakaTimeConfigValue(rule.pattern).trimmed();
            if (pattern.isEmpty()) {
                continue;
            }

            const QString apiUrl = sanitizeWakaTimeConfigValue(rule.apiUrl).trimmed();
            const QString apiKey = sanitizeWakaTimeConfigValue(rule.apiKey).trimmed();
            stream << pattern << " =";
            if (!apiUrl.isEmpty() || !apiKey.isEmpty()) {
                stream << " " << apiUrl << "|" << apiKey;
            }
            stream << "\n";
        }
    }
    return result;
}
