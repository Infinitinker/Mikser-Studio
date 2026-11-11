#pragma once

#include <QString>
#include <QStringList>

#include <expected>

namespace mikserstudio::engine {

// The process-wide MLT factory. MLT is initialised once and never shut down while Mikser Studio runs.
//
// The module folder (repository) is looked up in this order: $MIKSERSTUDIO_MLT_REPOSITORY,
// <exe>/../lib/mlt(-7), <exe>/lib/mlt(-7), the MLT install Mikser Studio was built against, MLT's own
// default. The data folder (profiles, presets, loader rules) likewise: $MIKSERSTUDIO_MLT_DATA, $MLT_DATA,
// <exe>/../share/mlt(-7), <exe>/share/mlt(-7), the build-time install, MLT's default.
class MltRuntime final {
public:
    // Initialises MLT on the first call; later calls return the same result. Thread-safe.
    // Fails with a user-facing message when the modules or required services are missing.
    static std::expected<const MltRuntime*, QString> initialize();

    [[nodiscard]] QString repositoryPath() const { return m_repositoryPath; }
    [[nodiscard]] QString repositorySource() const { return m_repositorySource; }  // How it was found.
    [[nodiscard]] QString dataPath() const { return m_dataPath; }
    [[nodiscard]] QString dataSource() const { return m_dataSource; }

    [[nodiscard]] bool hasProducer(const QString& name) const { return m_producers.contains(name); }
    [[nodiscard]] bool hasConsumer(const QString& name) const { return m_consumers.contains(name); }
    [[nodiscard]] bool hasTransition(const QString& name) const { return m_transitions.contains(name); }

    // The transition used to composite an upper video track over the tracks below it:
    // "qtblend", else "frei0r.cairoblend", else "composite".
    [[nodiscard]] QString videoCompositor() const { return m_videoCompositor; }

private:
    MltRuntime() = default;
    static std::expected<const MltRuntime*, QString> create();

    QString m_repositoryPath;
    QString m_repositorySource;
    QString m_dataPath;
    QString m_dataSource;
    QStringList m_producers;
    QStringList m_consumers;
    QStringList m_transitions;
    QString m_videoCompositor;
};

}  // namespace mikserstudio::engine
