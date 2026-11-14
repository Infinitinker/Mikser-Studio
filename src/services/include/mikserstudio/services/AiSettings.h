// The AI settings: the address of the AI tool the backend connects to, kept in a JSON file. Ollama's
// address is the Ollama plugin's setting.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <expected>

namespace mikserstudio::services {

struct AiSettings {
    QUrl comfyuiUrl{QStringLiteral("http://127.0.0.1:8188")};

    friend bool operator==(const AiSettings&, const AiSettings&) = default;
};

// {"comfyuiUrl": "..."}
[[nodiscard]] QJsonObject toJson(const AiSettings& settings);

// The settings in `object`, on top of `base`: a key that is missing keeps the base's value, and
// other keys, such as an "ollamaUrl" saved before, are ignored. An address must be an http or https
// URL with a host; the error names the setting.
[[nodiscard]] std::expected<AiSettings, QString> aiSettingsFromJson(const QJsonObject& object,
                                                                    const AiSettings& base = {});

// The settings saved in `path`; the defaults when the file is missing or cannot be read (logged).
[[nodiscard]] AiSettings loadAiSettings(const QString& path);
[[nodiscard]] std::expected<void, QString> saveAiSettings(const AiSettings& settings, const QString& path);

}  // namespace mikserstudio::services
