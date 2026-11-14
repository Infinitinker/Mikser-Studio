// A plugin runtime for the core tests, which starts nothing by itself: the test says when a plugin
// has started or failed.
#pragma once

#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/PluginRuntime.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>
#include <ranges>

namespace mikserstudio::tests {

// One start of a plugin, as the fake runtime saw it.
struct FakeStart {
    core::PluginManifest manifest;
    core::PluginActivation activation;
    bool running = true;
    QJsonObject settings;
};

class FakePluginInstance final : public core::PluginInstance {
public:
    explicit FakePluginInstance(std::shared_ptr<FakeStart> start)
        : m_start(std::move(start))
    {
    }
    ~FakePluginInstance() override { m_start->running = false; }
    FakePluginInstance(const FakePluginInstance&) = delete;
    FakePluginInstance& operator=(const FakePluginInstance&) = delete;
    void setSettings(const QJsonObject& settings) override { m_start->settings = settings; }

private:
    std::shared_ptr<FakeStart> m_start;
};

class FakePluginRuntime final : public core::PluginRuntime {
public:
    std::expected<std::unique_ptr<core::PluginInstance>, QString> activate(const core::PluginManifest& manifest,
                                                                           core::PluginActivation activation) override
    {
        if (!refusal.isEmpty())
            return std::unexpected(refusal);
        auto start = std::make_shared<FakeStart>(
            FakeStart{.manifest = manifest, .activation = std::move(activation), .running = true, .settings = {}});
        start->settings = start->activation.settings;
        starts.append(start);
        return std::make_unique<FakePluginInstance>(start);
    }

    // Says that every running plugin has started.
    void startAll() const
    {
        for (const auto& start : starts) {
            if (start->running)
                start->activation.started();
        }
    }

    [[nodiscard]] std::shared_ptr<FakeStart> last(const QString& id) const
    {
        for (const auto& start : std::views::reverse(starts)) {
            if (start->manifest.id == id)
                return start;
        }
        return nullptr;
    }

    QString refusal;  // When set, activating fails at once with it.
    QList<std::shared_ptr<FakeStart>> starts;
};

}  // namespace mikserstudio::tests
