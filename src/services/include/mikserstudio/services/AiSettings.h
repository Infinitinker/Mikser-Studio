// The AI settings: the addresses of the AI tools the backends connect to, kept in a JSON file.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <expected>

namespace mikserstudio::services {

struct AiSettings {
    QUrl ollamaUrl{QStringLiteral("http://127.0.0.1:11434")};
    QUrl comfyuiUrl{QStringLiteral("http://127.0.0.1:8188")};

    friend bool operator==(const AiSettings&, const AiSettings&) = default;
};

// {"ollamaUrl": "...", "comfyuiUrl": "..."}
[[nodiscard]] QJsonObject toJson(const AiSettings& settings);

// The settings in `object`, on top of `base`: a key that is missing keeps the base's value. An
// address must be an http or https URL with a host; the error names the setting.
[[nodiscard]] std::expected<AiSettings, QString> aiSettingsFromJson(const QJsonObject& object,
                                                                    const AiSettings& base = {});

// The settings saved in `path`; the defaults when the file is missing or cannot be read (logged).
[[nodiscard]] AiSettings loadAiSettings(const QString& path);
[[nodiscard]] std::expected<void, QString> saveAiSettings(const AiSettings& settings, const QString& path);

}  // namespace mikserstudio::services
