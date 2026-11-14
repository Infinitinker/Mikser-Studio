// Reading, checking and saving the AI settings.
#include "mikserstudio/services/AiSettings.h"

#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

std::expected<QUrl, QString> toolUrl(const QJsonValue& value, const QString& tool)
{
    const QUrl url(value.toString().trimmed(), QUrl::StrictMode);
    const bool web = url.scheme() == u"http" || url.scheme() == u"https";
    if (!value.isString() || !url.isValid() || !web || url.host().isEmpty()) {
        return std::unexpected(
            u"“%1” is not an address of %2. Enter an http address with a host and port, such as http://127.0.0.1:8188."_s
                .arg(value.toString(), tool));
    }
    QUrl address = url.adjusted(QUrl::StripTrailingSlash);
    if (address.path() == u"/")
        address.setPath({});
    return address;
}

}  // namespace

QJsonObject toJson(const AiSettings& settings)
{
    return QJsonObject{
        {u"comfyuiUrl"_s, settings.comfyuiUrl.toString()},
    };
}

std::expected<AiSettings, QString> aiSettingsFromJson(const QJsonObject& object, const AiSettings& base)
{
    AiSettings settings = base;
    if (object.contains(u"comfyuiUrl")) {
        auto url = toolUrl(object.value(u"comfyuiUrl"), u"ComfyUI"_s);
        if (!url)
            return std::unexpected(url.error());
        settings.comfyuiUrl = *url;
    }
    return settings;
}

AiSettings loadAiSettings(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcAi).noquote() << "Could not read the AI settings" << path << "-" << file.errorString();
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (!document.isObject()) {
        qCWarning(lcAi).noquote() << "The AI settings" << path << "are not a JSON object; using the defaults.";
        return {};
    }
    auto settings = aiSettingsFromJson(document.object());
    if (!settings) {
        qCWarning(lcAi).noquote() << "The AI settings" << path
                                  << "are not valid; using the defaults:" << settings.error();
        return {};
    }
    return *settings;
}

std::expected<void, QString> saveAiSettings(const AiSettings& settings, const QString& path)
{
    if (path.isEmpty())
        return {};
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        return std::unexpected(
            u"The AI settings could not be saved to “%1”: %2. Check that the folder can be written to."_s.arg(
                QDir::toNativeSeparators(path), file.errorString()));
    }
    return {};
}

}  // namespace mikserstudio::services
