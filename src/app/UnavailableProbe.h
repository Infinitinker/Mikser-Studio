#pragma once

#include "mikserstudio/core/Project.h"

namespace mikserstudio::app {

// Used when the playback engine could not start: every import fails with the engine's reason.
class UnavailableProbe final : public core::MediaProbe {
public:
    explicit UnavailableProbe(QString reason)
        : m_reason(std::move(reason))
    {
    }

    [[nodiscard]] std::expected<core::MediaInfo, QString> probe(const QString& /*path*/,
                                                                core::Rational /*projectFrameRate*/) override
    {
        return std::unexpected(m_reason);
    }

private:
    QString m_reason;
};

}  // namespace mikserstudio::app
