#pragma once

#include <mikserstudio/plugin_api.h>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>

namespace mikserstudio::core {

class CommandBus;

// Collects a string the other side of the ABI writes into a MikserStudioStringSink.
class SinkBuffer {
public:
    SinkBuffer() = default;
    SinkBuffer(const SinkBuffer&) = delete;
    SinkBuffer& operator=(const SinkBuffer&) = delete;

    [[nodiscard]] const MikserStudioStringSink* sink() const { return &m_sink; }
    [[nodiscard]] const QByteArray& data() const { return m_data; }
    [[nodiscard]] QString text() const { return QString::fromUtf8(m_data); }

private:
    static void write(void* context, const char* utf8, size_t length);

    QByteArray m_data;
    MikserStudioStringSink m_sink{this, &SinkBuffer::write};
};

// Writes `data` to a sink the plugin passed in, if it gave one.
void writeToSink(const MikserStudioStringSink* sink, const QByteArray& data);

// The host side of MikserStudioHostApiV1 for one active plugin. It enforces the "<plugin-id>." prefix
// on registered commands and remembers them so they can be unregistered on deactivation.
class PluginHost {
public:
    using SettingsProvider = std::function<QJsonObject()>;

    PluginHost(CommandBus& bus, QString pluginId, QString category, SettingsProvider settings);
    ~PluginHost();  // Unregisters the plugin's commands.
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    [[nodiscard]] const MikserStudioHostApiV1* api() const { return &m_api; }
    [[nodiscard]] const QStringList& commands() const { return m_commands; }
    void unregisterCommands();

private:
    static PluginHost& self(void* host);
    static void log(void* host, MikserStudioLogLevel level, const char* message, size_t length);
    static int registerCommand(void* host, const char* specJson, size_t specLength, MikserStudioCommandFn fn,
                               void* userData, const MikserStudioStringSink* error);
    static int executeCommand(void* host, const char* id, size_t idLength, const char* paramsJson, size_t paramsLength,
                              const MikserStudioStringSink* result, const MikserStudioStringSink* error);
    static void getSettings(void* host, const MikserStudioStringSink* settingsJson);

    std::expected<void, QString> addCommand(const QByteArray& specJson, MikserStudioCommandFn fn, void* userData);

    CommandBus& m_bus;
    QString m_pluginId;
    QString m_category;
    SettingsProvider m_settings;
    QStringList m_commands;
    MikserStudioHostApiV1 m_api{};
};

}  // namespace mikserstudio::core
