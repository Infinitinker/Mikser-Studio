// Converting job states to and from their names in the AI service's API.
#include "mikserstudio/services/AiTypes.h"

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

constexpr std::array kStates{
    std::pair{AiJobState::Queued, u"queued"},       std::pair{AiJobState::Running, u"running"},
    std::pair{AiJobState::Succeeded, u"succeeded"}, std::pair{AiJobState::Failed, u"failed"},
    std::pair{AiJobState::Canceled, u"canceled"},
};

}  // namespace

QString toString(AiJobState state)
{
    for (const auto& [value, name] : kStates) {
        if (value == state)
            return QString(name);
    }
    return u"failed"_s;
}

std::optional<AiJobState> aiJobStateFromString(QStringView text)
{
    for (const auto& [value, name] : kStates) {
        if (text == QStringView(name))
            return value;
    }
    return std::nullopt;
}

bool isFinished(AiJobState state)
{
    return state == AiJobState::Succeeded || state == AiJobState::Failed || state == AiJobState::Canceled;
}

}  // namespace mikserstudio::services
